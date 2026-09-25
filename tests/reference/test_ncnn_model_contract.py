"""Offline contract gates; fixture tensors are deliberately not model-parity evidence."""

import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import numpy as np

from tools.models.ncnn import model_contract as contract
from tools.models.ncnn.audit_ncnn_graph import audit_param, audit_onnx
from tools.models.ncnn.compare_detector_outputs import compare_detector
from tools.models.ncnn.compare_pose_outputs import compare_pose
from tools.models.ncnn.export_rtmdet_nano import export as export_detector


def conversion_metadata(role="detector"):
    shapes = ({"cls": [1, 2100, 1], "bbox": [1, 2100, 4]} if role == "detector" else
              {"simcc_x": [1, 26, 384], "simcc_y": [1, 26, 512]})
    return {"role": role,
            "source_url": contract.DETECTOR_CHECKPOINT_URL if role == "detector" else contract.POSE_CHECKPOINT_URL,
            "source_revisions": contract.PINNED_REVISIONS,
            "tool_version": "ncnn-test", "opset": 17,
            "export_command": "export --static", "conversion_command": "onnx2ncnn model.onnx",
            "license": "test only",
            "output_contract": {name: {"shape": shape, "download_dtype": "fp32",
                                        "max_bytes": int(np.prod(shape)) * 4}
                                for name, shape in shapes.items()},
            "onnx2ncnn_sha256": "a" * 64, "ncnnoptimize_sha256": "b" * 64}


def write_synthetic_onnx(path: Path, role: str) -> None:
    """Small input-dependent static graph; it is never model-parity evidence."""
    import onnx
    from onnx import TensorProto, helper

    contract_data = conversion_metadata(role)["output_contract"]
    input_data = contract.model_input_contract(role)
    nodes = [helper.make_node("ReduceMean", ["in0"], ["mean4"],
                              axes=[1, 2, 3], keepdims=1),
             helper.make_node("Reshape", ["mean4", "scalar_shape"], ["mean3"])]
    initializers = [helper.make_tensor("scalar_shape", TensorProto.INT64, [3], [1, 1, 1])]
    outputs = []
    for name, item in contract_data.items():
        shape = item["shape"]
        initializers.append(helper.make_tensor(name + "_bias", TensorProto.FLOAT,
                                               shape, [0.0] * int(np.prod(shape))))
        nodes.append(helper.make_node("Add", ["mean3", name + "_bias"], [name]))
        outputs.append(helper.make_tensor_value_info(name, TensorProto.FLOAT, shape))
    graph = helper.make_graph(nodes, "synthetic", [helper.make_tensor_value_info(
        "in0", TensorProto.FLOAT, [1, 3, input_data["height"], input_data["width"]])],
        outputs, initializer=initializers)
    onnx.save(helper.make_model(graph, opset_imports=[helper.make_operatorsetid("", 17)]), str(path))


class NcnnModelContractTests(unittest.TestCase):
    def test_pinned_vendor_rejects_dirty_tracked_and_untracked_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            revisions = {}
            for name in contract.PINNED_REVISIONS:
                repo = root / name
                repo.mkdir()
                subprocess.run(["git", "init", "-q", str(repo)], check=True)
                (repo / "source.py").write_text("version = 1\n", encoding="utf-8")
                subprocess.run(["git", "-C", str(repo), "add", "source.py"], check=True)
                subprocess.run(["git", "-C", str(repo), "-c", "user.name=C1 Test",
                                "-c", "user.email=c1@example.invalid", "commit", "-qm", "source"], check=True)
                revisions[name] = subprocess.check_output(
                    ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
            with mock.patch.object(contract, "PINNED_REVISIONS", revisions):
                (root / "mmpose" / "shadow.py").write_text("malicious = True\n", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "dirty|untracked|working tree"):
                    contract.activate_pinned_vendor(root)
                (root / "mmpose" / "shadow.py").unlink()
                (root / "mmdetection" / "source.py").write_text("version = 2\n", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "dirty|tracked|working tree"):
                    contract.activate_pinned_vendor(root)
                (root / "mmdetection" / "source.py").write_text("version = 1\n", encoding="utf-8")
                subprocess.run(["git", "-C", str(root / "mmdeploy"), "config", "core.excludesFile",
                                str(root / "ignore-list")], check=True)
                (root / "ignore-list").write_text("ignored_shadow.py\n", encoding="utf-8")
                (root / "mmdeploy" / "ignored_shadow.py").write_text("malicious = True\n", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "dirty|ignored|working tree"):
                    contract.activate_pinned_vendor(root)

    def test_python_tool_entrypoints_run_directly_from_repo_root(self):
        root = Path(__file__).resolve().parents[2]
        for name in ("model_contract", "export_rtmdet_nano", "export_rtmpose",
                     "audit_ncnn_graph", "compare_detector_outputs", "compare_pose_outputs"):
            path = root / "tools" / "models" / "ncnn" / f"{name}.py"
            result = subprocess.run([sys.executable, str(path), "--help"], cwd=root,
                                    capture_output=True, text=True)
            with self.subTest(name=name):
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_source_and_onnx_hashes_are_mandatory(self):
        with tempfile.TemporaryDirectory() as directory:
            source, onnx = Path(directory) / "source.pth", Path(directory) / "raw.onnx"
            source.write_bytes(b"source")
            onnx.write_bytes(b"onnx")
            hashes = {"checkpoint_sha256": hashlib.sha256(b"source").hexdigest(),
                      "onnx_sha256": hashlib.sha256(b"onnx").hexdigest()}
            contract.require_artifact_hashes(source, onnx, hashes)
            hashes["onnx_sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "ONNX.*SHA-256"):
                contract.require_artifact_hashes(source, onnx, hashes)
            hashes.pop("checkpoint_sha256")
            with self.assertRaisesRegex(ValueError, "checkpoint_sha256"):
                contract.require_artifact_hashes(source, onnx, hashes)

    def test_fixed_inputs_and_named_raw_outputs(self):
        detector = contract.model_input_contract("detector")
        pose = contract.model_input_contract("body")
        self.assertEqual((detector["width"], detector["height"]), (320, 320))
        self.assertEqual((pose["width"], pose["height"]), (192, 256))
        self.assertEqual(detector["color_order"], pose["color_order"])
        self.assertEqual(detector["color_order"], "rgb")
        self.assertEqual(detector["tensor_dtype"], "fp16")
        self.assertEqual(detector["elempack"], 4)
        self.assertEqual(detector["normalization"]["mean"], [123.675, 116.28, 103.53])
        self.assertEqual(len(pose["normalization"]["mean"]), 3)
        self.assertEqual(len(pose["normalization"]["norm"]), 3)
        self.assertEqual(contract.output_blobs("detector"), ("cls", "bbox"))
        self.assertEqual(contract.output_blobs("body"), ("simcc_x", "simcc_y"))
        self.assertEqual(pose["crop"], "bbox_affine")
        self.assertEqual(pose["bbox_padding_factor"], 1.25)

    def test_export_refuses_wrong_checkpoint_before_importing_model_stack(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkpoint = root / "checkpoint.pth"
            checkpoint.write_bytes(b"wrong")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                export_detector(checkpoint, root, root / "out.onnx", "0" * 64)
            self.assertFalse((root / "out.onnx").exists())

    def test_manifest_is_deterministic_and_records_each_provenance_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {key: root / key for key in
                     ("checkpoint", "onnx", "param", "bin", "fixture")}
            for key, path in paths.items():
                path.write_bytes(key.encode())
            write_synthetic_onnx(paths["onnx"], "detector")
            metadata = conversion_metadata()
            one = contract.build_provenance(metadata, paths)
            two = contract.build_provenance(metadata, paths)
            self.assertEqual(contract.canonical_json(one), contract.canonical_json(two))
            self.assertEqual(one["artifacts"]["fixture_sha256"],
                             hashlib.sha256(b"fixture").hexdigest())
            self.assertEqual(one["artifacts"]["onnx_sha256"],
                             contract.sha256_file(paths["onnx"]))
            with self.assertRaisesRegex(ValueError, "tool_version"):
                contract.build_provenance({**metadata, "tool_version": ""}, paths)
            for field, wrong in (("input_contract", {**contract.model_input_contract("detector"), "width": 321}),
                                 ("output_blobs", ["cls", "fake"]),
                                 ("output_contract", {**metadata["output_contract"], "bbox": {
                                     "shape": [1, 1, 4], "download_dtype": "fp32", "max_bytes": 16}})):
                with self.subTest(field=field), self.assertRaises(ValueError):
                    contract.build_provenance({**metadata, field: wrong}, paths)
            different = json.loads(json.dumps(metadata["output_contract"]))
            for item in different.values():
                item["shape"][1] += 1
                item["max_bytes"] = int(np.prod(item["shape"])) * 4
            with self.assertRaisesRegex(ValueError, "output contract|shape"):
                contract.build_provenance({**metadata, "output_contract": different}, paths)

    def test_golden_manifest_binds_real_files_to_conversion_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ("checkpoint", "onnx", "param", "bin", "fixture", "reference", "candidate", "repeat")
            paths = {name: root / name for name in names}
            for name, path in paths.items():
                path.write_bytes(name.encode())
            write_synthetic_onnx(paths["onnx"], "detector")
            metadata = conversion_metadata()
            conversion = contract.build_provenance(metadata, paths)
            with mock.patch.object(contract, "DETECTOR_CHECKPOINT_SHA256", contract.sha256_file(paths["checkpoint"])):
                one = contract.record_golden(conversion, paths)
                self.assertEqual(one["artifacts"]["candidate_sha256"], contract.sha256_file(paths["candidate"]))
                self.assertEqual(one["profile_id"], "android-ncnn-vulkan")
                self.assertEqual(one["profile_sha256"], contract.sha256_file(
                    Path(__file__).resolve().parents[2] / "profiles/android-ncnn-vulkan.json"))
                forged = {**one, "profile_sha256": "0" * 64}
                with self.assertRaisesRegex(ValueError, "profile.*SHA-256"):
                    contract.verify_golden_inputs(forged, paths)
                paths["onnx"].write_bytes(b"changed")
                with self.assertRaisesRegex(ValueError, "ONNX|onnx"):
                    contract.record_golden(conversion, paths)

    def test_detector_profile_threshold_is_pinned_and_not_cli_overridable(self):
        from tools.models.ncnn import compare_detector_outputs as comparator
        canonical_path, threshold = contract.load_detector_profile()
        self.assertEqual(canonical_path.name, "android-ncnn-vulkan.json")
        self.assertEqual(threshold, 0.35)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile = root / "android-ncnn-vulkan.json"
            profile.write_text(json.dumps({"schema_version": 1, "profile": "android-ncnn-vulkan",
                                           "detector": {"person_score_threshold": 1.0}}), encoding="utf-8")
            with mock.patch.object(contract, "DETECTOR_PROFILE_PATH", profile, create=True):
                with self.assertRaisesRegex(ValueError, "threshold"):
                    contract.load_detector_profile()

            paths = {name: root / name for name in
                     ("checkpoint", "onnx", "param", "bin", "fixture", "reference", "candidate", "repeat")}
            for name, path in paths.items():
                path.write_bytes(name.encode())
            write_synthetic_onnx(paths["onnx"], "detector")
            paths["reference"].write_text(json.dumps({"detections": [
                {"bbox": [0, 0, 10, 10], "score": 0.9, "person": True}]}), encoding="utf-8")
            for name in ("candidate", "repeat"):
                paths[name].write_text('{"detections": []}', encoding="utf-8")
            conversion = contract.build_provenance(conversion_metadata(), paths)
            manifest_path = root / "golden.json"
            with mock.patch.object(contract, "DETECTOR_CHECKPOINT_SHA256",
                                   contract.sha256_file(paths["checkpoint"])):
                manifest_path.write_text(json.dumps(contract.record_golden(conversion, paths)), encoding="utf-8")
                argv = ["compare_detector_outputs.py", "--manifest", str(manifest_path)]
                for name, path in paths.items():
                    argv += [f"--{name}", str(path)]
                with mock.patch.object(sys, "argv", argv + ["--threshold", "1.0"]):
                    with self.assertRaises(SystemExit):
                        comparator.main()
                with mock.patch.object(sys, "argv", argv):
                    with self.assertRaisesRegex(ValueError, "candidate count|missed"):
                        comparator.main()

    def test_conversion_provenance_requires_complete_pinned_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {name: root / name for name in ("checkpoint", "onnx", "param", "bin", "fixture")}
            for name, path in paths.items():
                path.write_bytes(name.encode())
            write_synthetic_onnx(paths["onnx"], "detector")
            valid = contract.build_provenance(conversion_metadata(), paths)
            for missing in ("source_url", "source_revisions", "license", "tool_version",
                            "opset", "export_command", "conversion_command", "output_contract"):
                forged = dict(valid); forged.pop(missing)
                with self.subTest(missing=missing), self.assertRaisesRegex(ValueError, missing):
                    contract.verify_provenance(forged, paths)
            forged = json.loads(json.dumps(valid))
            forged["input_contract"]["width"] = 321
            forged["input_contract"]["tensor_dtype"] = "fp32"
            forged["input_contract"]["elempack"] = 1
            with self.assertRaisesRegex(ValueError, "input.*contract"):
                contract.verify_provenance(forged, paths)

    def test_export_manifest_rejects_self_consistent_forged_input(self):
        with tempfile.TemporaryDirectory() as directory:
            from tools.models.ncnn.model_contract import validate_export_manifest
            root = Path(directory)
            checkpoint, onnx = root / "checkpoint", root / "model.onnx"
            checkpoint.write_bytes(b"fake checkpoint"); onnx.write_bytes(b"fake onnx")
            manifest = {**conversion_metadata(),
                        "export_tool": "test-exporter",
                        "checkpoint_sha256": contract.sha256_file(checkpoint),
                        "onnx_sha256": contract.sha256_file(onnx),
                        "input_contract": {**contract.model_input_contract("detector"),
                                           "width": 321, "tensor_dtype": "fp32", "elempack": 1},
                        "output_blobs": list(contract.output_blobs("detector"))}
            with self.assertRaisesRegex(ValueError, "pinned|checkpoint|input.*contract"):
                validate_export_manifest(manifest, "detector", checkpoint, onnx)

    def test_export_manifest_accepts_static_observed_output_contract(self):
        import onnx
        from onnx import TensorProto, helper
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkpoint, onnx_path = root / "checkpoint", root / "model.onnx"
            checkpoint.write_bytes(b"test checkpoint")
            shapes = {"cls": [1, 2, 1], "bbox": [1, 2, 4]}  # Synthetic only.
            graph = helper.make_graph(
                [helper.make_node("ReduceMean", ["in0"], ["mean4"], axes=[1, 2, 3], keepdims=1),
                 helper.make_node("Reshape", ["mean4", "mean_shape"], ["mean3"]),
                 helper.make_node("Concat", ["mean3", "mean3"], ["cls"], axis=1),
                 helper.make_node("Concat", ["cls", "cls", "cls", "cls"], ["bbox"], axis=2)],
                "synthetic", [helper.make_tensor_value_info("in0", TensorProto.FLOAT, [1, 3, 320, 320])],
                [helper.make_tensor_value_info(name, TensorProto.FLOAT, shape)
                 for name, shape in shapes.items()],
                initializer=[helper.make_tensor("mean_shape", TensorProto.INT64, [3], [1, 1, 1])])
            onnx.save(helper.make_model(graph, opset_imports=[helper.make_operatorsetid("", 17)]), str(onnx_path))
            declared = {name: {"shape": shape, "download_dtype": "fp32",
                               "max_bytes": int(np.prod(shape)) * 4}
                        for name, shape in shapes.items()}
            manifest = {**conversion_metadata(), "role": "detector", "export_tool": "test",
                        "checkpoint_sha256": contract.sha256_file(checkpoint),
                        "onnx_sha256": contract.sha256_file(onnx_path),
                        "input_contract": contract.model_input_contract("detector"),
                        "output_blobs": ["cls", "bbox"], "output_contract": declared}
            with mock.patch.object(contract, "DETECTOR_CHECKPOINT_SHA256", contract.sha256_file(checkpoint)):
                self.assertEqual(contract.validate_export_manifest(manifest, "detector", checkpoint, onnx_path)
                                 ["output_contract"], declared)

    def test_output_contract_requires_static_shapes_and_bounded_bytes(self):
        from tools.models.ncnn.model_contract import validate_output_contract
        valid = conversion_metadata()["output_contract"]
        validate_output_contract("detector", valid)
        for value in ({"shape": [1, 0], "download_dtype": "fp32", "max_bytes": 0},
                      {"shape": [1, 4], "download_dtype": "fp32", "max_bytes": 15},
                      {"shape": [1, 100000000], "download_dtype": "fp32", "max_bytes": 400000000}):
            wrong = dict(valid); wrong["cls"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_output_contract("detector", wrong)

    def test_output_contract_rejects_semantically_fake_shapes(self):
        for role in ("detector", "body"):
            valid = conversion_metadata(role)["output_contract"]
            bad_shapes = ({"cls": [1, 1, 1], "bbox": [1, 1, 4]} if role == "detector" else
                          {"simcc_x": [1, 26, 1], "simcc_y": [1, 26, 1]})
            for name, shape in bad_shapes.items():
                forged = json.loads(json.dumps(valid))
                forged[name] = {"shape": shape, "download_dtype": "fp32",
                                "max_bytes": int(np.prod(shape)) * 4}
                with self.subTest(role=role, name=name), self.assertRaises(ValueError):
                    contract.validate_output_contract(role, forged)
        valid = conversion_metadata("detector")["output_contract"]
        forged = json.loads(json.dumps(valid))
        forged["bbox"] = {"shape": [1, 2099, 4], "download_dtype": "fp32", "max_bytes": 2099 * 16}
        with self.assertRaises(ValueError):
            contract.validate_output_contract("detector", forged)

    def test_conversion_provenance_requires_both_tool_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {name: root / name for name in ("checkpoint", "onnx", "param", "bin", "fixture")}
            for name, path in paths.items():
                path.write_bytes(name.encode())
            write_synthetic_onnx(paths["onnx"], "detector")
            manifest = contract.build_provenance(conversion_metadata(), paths)
            tools = {name: root / name for name in ("onnx2ncnn", "ncnnoptimize")}
            for name, path in tools.items():
                path.write_bytes(name.encode())
            for field in ("onnx2ncnn_sha256", "ncnnoptimize_sha256"):
                forged = dict(manifest); forged.pop(field, None)
                with mock.patch.object(contract, "DETECTOR_CHECKPOINT_SHA256",
                                       contract.sha256_file(paths["checkpoint"])):
                    with self.subTest(field=field), self.assertRaisesRegex(ValueError, field):
                        contract.verify_provenance(forged, paths)
                metadata = conversion_metadata(); metadata.pop(field)
                with self.subTest(field=field), self.assertRaisesRegex(ValueError, field):
                    contract.build_provenance(metadata, paths)
            manifest["onnx2ncnn_sha256"] = contract.sha256_file(tools["onnx2ncnn"])
            manifest["ncnnoptimize_sha256"] = contract.sha256_file(tools["ncnnoptimize"])
            with mock.patch.object(contract, "DETECTOR_CHECKPOINT_SHA256",
                                   contract.sha256_file(paths["checkpoint"])):
                contract.verify_provenance(manifest, {**paths, **tools})
                tools["onnx2ncnn"].write_bytes(b"changed")
                with self.assertRaisesRegex(ValueError, "onnx2ncnn.*SHA-256 mismatch"):
                    contract.verify_provenance(manifest, {**paths, **tools})

    def test_graph_audit_rejects_custom_dynamic_cast_and_unnamed_output(self):
        valid = "7767517\n2 2\nInput in 0 1 in0 0=320 1=320 2=3\nConvolution conv 1 1 in0 cls 0=1\n"
        audit_param(valid, ("cls",), expected_input_shape=(320, 320, 3),
                    allowed_layers={"Input", "Convolution"})
        numeric_internal = ("7767517\n3 3\nInput in 0 1 in0 0=320 1=320 2=3\n"
                            "Convolution conv1 1 1 in0 7 0=1\nConvolution conv2 1 1 7 cls 0=1\n")
        audit_param(numeric_internal, ("cls",), expected_input_shape=(320, 320, 3),
                    allowed_layers={"Input", "Convolution"})
        for text, outputs, message in (
            (valid.replace("Convolution", "MyCustom"), ("cls",), "unsupported"),
            (valid.replace("0=320", "0=-1"), ("cls",), "dynamic"),
            (valid.replace("Convolution", "Cast"), ("cls",), "cast"),
            (valid.replace("cls", "0"), ("0",), "unnamed"),
        ):
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                audit_param(text, outputs, expected_input_shape=(320, 320, 3),
                            allowed_layers={"Input", "Convolution"})

    def test_ncnn_binaryop_negative_scalar_is_not_a_dynamic_shape(self):
        graph = ("7767517\n2 2\nInput image 0 1 in0 0=320 1=320 2=3\n"
                 "BinaryOp subtract 1 1 in0 cls 0=1 1=1 2=-1\n")
        audit_param(graph, ("cls",), expected_input_shape=(320, 320, 3))
        dynamic = graph.replace("BinaryOp subtract 1 1 in0 cls 0=1 1=1 2=-1",
                                "Reshape reshape 1 1 in0 cls 0=-1")
        with self.assertRaisesRegex(ValueError, "dynamic"):
            audit_param(dynamic, ("cls",), expected_input_shape=(320, 320, 3))

    def test_ncnn_terminal_must_depend_on_image_input(self):
        disconnected = ("7767517\n3 3\nInput image 0 1 in0 0=320 1=320 2=3\n"
                        "Convolution conv 1 1 in0 cls 0=1\n"
                        "MemoryData constant 0 1 bbox 0=4\n")
        with self.assertRaisesRegex(ValueError, "depend|disconnected"):
            audit_param(disconnected, ("cls", "bbox"), expected_input_shape=(320, 320, 3))
        mixed = ("7767517\n3 3\nInput image 0 1 in0 0=320 1=320 2=3\n"
                 "MemoryData constant 0 1 bias 0=1\n"
                 "BinaryOp add 2 1 in0 bias cls 0=0\n")
        audit_param(mixed, ("cls",), expected_input_shape=(320, 320, 3))

    def test_onnx_external_tensor_data_is_rejected_before_sidecar_load(self):
        import onnx
        from onnx import TensorProto, helper, numpy_helper
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "external.onnx"
            tensor = numpy_helper.from_array(np.array([1.0], dtype=np.float32), name="weight")
            graph = helper.make_graph(
                [helper.make_node("Add", ["in0", "weight"], ["cls"])], "external",
                [helper.make_tensor_value_info("in0", TensorProto.FLOAT, [1, 3, 320, 320])],
                [helper.make_tensor_value_info("cls", TensorProto.FLOAT, [1, 3, 320, 320])],
                initializer=[tensor])
            onnx.save_model(helper.make_model(graph, opset_imports=[helper.make_operatorsetid("", 17)]),
                            str(path), save_as_external_data=True, all_tensors_to_one_file=True,
                            location="weights.bin", size_threshold=0)
            model_hash = contract.sha256_file(path)
            (root / "weights.bin").write_bytes(b"mutated without changing ONNX")
            self.assertEqual(contract.sha256_file(path), model_hash)
            with self.assertRaisesRegex(ValueError, "external"):
                audit_onnx(path, ("cls",), (1, 3, 320, 320))

    def test_ncnn_graph_rejects_missing_wrong_dynamic_input_and_undefined_bottom(self):
        valid = "7767517\n2 2\nInput in 0 1 in0 0=320 1=320 2=3\nConvolution conv 1 1 in0 cls 0=1\n"
        cases = (
            (valid.replace("in0", "wrong"), "input blob"),
            (valid.replace("0=320", "0=0"), "positive|input shape"),
            (valid.replace("0=320", "0=321"), "input shape"),
            (valid.replace("2=3", ""), "input shape"),
            (valid.replace("1 1 in0 cls", "1 1 ghost cls"), "undefined bottom"),
            ("7767517\n3 3\nInput in 0 1 in0 0=320 1=320 2=3\n"
             "Input extra 0 1 in1 0=320 1=320 2=3\nConvolution conv 1 1 in0 cls 0=1\n", "single|one Input"),
            ("7767517\n3 3\nInput in 0 1 in0 0=320 1=320 2=3\n"
             "Reshape reshape 1 1 in0 mid 0=-1\nConvolution conv 1 1 mid cls 0=1\n", "dynamic"),
        )
        for graph, expected_error in cases:
            with self.subTest(expected_error=expected_error), self.assertRaisesRegex(ValueError, expected_error):
                audit_param(graph, ("cls",), expected_input_shape=(320, 320, 3))

    def test_onnx_audit_rejects_dynamic_and_custom_nodes(self):
        import onnx
        from onnx import TensorProto, helper
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tiny.onnx"
            def save(input_shape, domain="", operation="Identity", input_name="in0",
                     declared_output_shape=None):
                graph = helper.make_graph(
                    [helper.make_node(operation, [input_name], ["cls"], domain=domain)],
                    "tiny", [helper.make_tensor_value_info(input_name, TensorProto.FLOAT, input_shape)],
                    [helper.make_tensor_value_info("cls", TensorProto.FLOAT,
                                                   declared_output_shape or input_shape)])
                model = helper.make_model(graph, opset_imports=[helper.make_operatorsetid("", 17)])
                onnx.save(model, str(path))
            save([1, 3, 320, 320])
            audit_onnx(path, ("cls",), (1, 3, 320, 320))
            save([1, 3, 320, 320], input_name="wrong")
            with self.assertRaisesRegex(ValueError, "input.*in0"):
                audit_onnx(path, ("cls",), (1, 3, 320, 320))
            save(["N", 3, 320, 320])
            with self.assertRaisesRegex(ValueError, "dynamic"):
                audit_onnx(path, ("cls",), (1, 3, 320, 320))
            save([1, 3, 320, 320], operation="RandomNormalLike")
            with self.assertRaisesRegex(ValueError, "unsupported.*operator"):
                audit_onnx(path, ("cls",), (1, 3, 320, 320))
            save([1, 3, 320, 320])
            with self.assertRaisesRegex(ValueError, "output.*contract"):
                audit_onnx(path, ("cls",), (1, 3, 320, 320),
                           expected_output_contract={"cls": {"shape": [1, 3, 1, 1],
                                                              "download_dtype": "fp32", "max_bytes": 12}})
            save([1, 3, 320, 320], declared_output_shape=[1, 3, 4, 4])
            with self.assertRaisesRegex(ValueError, "declared output shape|shape inference"):
                audit_onnx(path, ("cls",), (1, 3, 320, 320))

    def test_onnx_audit_rejects_integer_image_or_output_dtype(self):
        import onnx
        from onnx import TensorProto, helper
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "wrong-dtype.onnx"
            for image_dtype, output_dtype in ((TensorProto.INT64, TensorProto.INT64),
                                               (TensorProto.FLOAT, TensorProto.INT64)):
                graph = helper.make_graph(
                    [helper.make_node("Identity", ["in0"], ["cls"])], "dtype",
                    [helper.make_tensor_value_info("in0", image_dtype, [1, 3, 320, 320])],
                    [helper.make_tensor_value_info("cls", output_dtype, [1, 3, 320, 320])])
                onnx.save(helper.make_model(graph, opset_imports=[helper.make_operatorsetid("", 17)]), str(path))
                with self.subTest(image_dtype=image_dtype), self.assertRaisesRegex(ValueError, "dtype"):
                    audit_onnx(path, ("cls",), (1, 3, 320, 320))

    def test_onnx_terminal_must_depend_on_image_input(self):
        import onnx
        from onnx import TensorProto, helper
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "disconnected.onnx"
            graph = helper.make_graph(
                [helper.make_node("Constant", [], ["cls"],
                                  value=helper.make_tensor("constant", TensorProto.FLOAT,
                                                           [1, 3, 320, 320], [0.0] * (3 * 320 * 320)))],
                "disconnected",
                [helper.make_tensor_value_info("in0", TensorProto.FLOAT, [1, 3, 320, 320])],
                [helper.make_tensor_value_info("cls", TensorProto.FLOAT, [1, 3, 320, 320])])
            onnx.save(helper.make_model(graph, opset_imports=[helper.make_operatorsetid("", 17)]), str(path))
            with self.assertRaisesRegex(ValueError, "depend|disconnected"):
                audit_onnx(path, ("cls",), (1, 3, 320, 320))

    def test_comparator_clis_refuse_changed_model_asset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {name: root / name for name in
                     ("checkpoint", "onnx", "param", "bin", "fixture", "reference", "candidate", "repeat")}
            for name, path in files.items():
                path.write_bytes(name.encode())
            for role, module in (("detector", "compare_detector_outputs"),
                                 ("body", "compare_pose_outputs")):
                write_synthetic_onnx(files["onnx"], role)
                manifest = contract.build_provenance(conversion_metadata(role), files)
                manifest["artifacts"]["checkpoint_sha256"] = contract.pinned_checkpoint_sha256(role)
                golden = {**manifest, "artifacts": {**manifest["artifacts"],
                          **{f"{name}_sha256": contract.sha256_file(files[name])
                             for name in ("reference", "candidate", "repeat")}}}
                manifest_path = root / f"{role}-golden.json"
                manifest_path.write_text(json.dumps(golden), encoding="utf-8")
                command = [sys.executable, "-m", f"tools.models.ncnn.{module}",
                           "--manifest", str(manifest_path)]
                for name, path in files.items():
                    command += [f"--{name}", str(path)]
                result = subprocess.run(command, capture_output=True, text=True)
                with self.subTest(role=role):
                    self.assertNotEqual(result.returncode, 0)
                    self.assertRegex(result.stderr, "checkpoint.*SHA-256 mismatch")

    def test_converter_fails_before_running_tools_on_forged_manifest(self):
        if not shutil.which("pwsh"):
            self.skipTest("PowerShell 7 unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checkpoint, onnx, fixture = (root / name for name in ("checkpoint", "onnx", "fixture"))
            for path in (checkpoint, onnx, fixture):
                path.write_bytes(path.name.encode())
            marker = root / "converter-ran.txt"
            fake_tool = root / "fake-tool.ps1"
            fake_tool.write_text(f"param($a,$b,$c)\nSet-Content -LiteralPath '{marker}' -Value ran\n", encoding="utf-8")
            manifest = {**conversion_metadata(), "role": "detector", "export_tool": "test",
                        "checkpoint_sha256": contract.sha256_file(checkpoint),
                        "onnx_sha256": contract.sha256_file(onnx),
                        "input_contract": {**contract.model_input_contract("detector"), "width": 321},
                        "output_blobs": list(contract.output_blobs("detector"))}
            manifest_path = root / "forged.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            script = Path(__file__).resolve().parents[2] / "tools/models/ncnn/convert_to_ncnn.ps1"
            command = ["pwsh", "-NoProfile", "-File", str(script), "-Role", "detector",
                       "-Checkpoint", str(checkpoint), "-Onnx", str(onnx),
                       "-ExportManifest", str(manifest_path), "-Fixture", str(fixture),
                       "-FixtureSha256", contract.sha256_file(fixture),
                       "-Onnx2Ncnn", str(fake_tool), "-Onnx2NcnnSha256", contract.sha256_file(fake_tool),
                       "-NcnnOptimize", str(fake_tool), "-NcnnOptimizeSha256", contract.sha256_file(fake_tool),
                       "-NcnnToolVersion", "test", "-OutputDirectory", str(root / "out"),
                       "-Python", sys.executable]
            result = subprocess.run(command, cwd=script.parents[3], capture_output=True,
                                    text=True, encoding="utf-8", errors="replace")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Pinned export manifest preflight failed", result.stderr)
            self.assertFalse(marker.exists())

    def test_detector_gates_candidate_count_iou_score_and_missed_person(self):
        reference = [{"bbox": [0, 0, 10, 10], "score": 0.9, "person": True}]
        compare_detector(reference, [{"bbox": [0, 0, 10, 10], "score": 0.9}], 0.5)
        with self.assertRaisesRegex(ValueError, "candidate count|missed"):
            compare_detector(reference, [], 0.5)
        with self.assertRaisesRegex(ValueError, "IoU"):
            compare_detector(reference, [{"bbox": [2, 2, 12, 12], "score": 0.9}], 0.5)
        with self.assertRaisesRegex(ValueError, "score"):
            compare_detector(reference, [{"bbox": [0, 0, 10, 10], "score": 0.92}], 0.5)
        with self.assertRaisesRegex(ValueError, "person-only|nonperson"):
            compare_detector(reference, [{"bbox": [0, 0, 10, 10], "score": 0.9, "person": False}], 0.5)
        with self.assertRaisesRegex(ValueError, "person-only|nonperson"):
            compare_detector(reference, [reference[0], {"bbox": [20, 20, 30, 30],
                                                      "score": 0.95, "person": False}], 0.5)
        with self.assertRaisesRegex(ValueError, "person-only|nonperson"):
            compare_detector([{**reference[0], "person": False}], [], 0.5)

    def test_detector_uses_feasible_one_to_one_matching(self):
        reference = [{"bbox": [0, 0, 100, 100], "score": 0.9},
                     {"bbox": [2, 0, 102, 100], "score": 0.9}]
        candidate = [{"bbox": [0.9, 0, 100.9, 100], "score": 0.9},
                     {"bbox": [-1.8, 0, 98.2, 100], "score": 0.9}]
        self.assertEqual(compare_detector(reference, candidate, 0.5)["candidate_count"], 2)

    def test_detector_repeat_must_also_match_reference(self):
        ref = [{"bbox": [0, 0, 10, 10], "score": 0.900}]
        candidate = [{"bbox": [0, 0, 10, 10], "score": 0.906}]
        repeat = [{"bbox": [0, 0, 10, 10], "score": 0.912}]
        with self.assertRaisesRegex(ValueError, "score"):
            compare_detector(ref, candidate, 0.5, repeat)

    def test_pose_gates_valid_mask_distance_confidence_and_repeat(self):
        ref = {"points": [[float(i), 0.0] for i in range(26)],
               "scores": [0.9] * 26, "valid": [True] * 26}
        actual = json.loads(json.dumps(ref))
        compare_pose(ref, actual, [0, 0, 100, 100], (100, 100), repeat=actual)
        wrong = json.loads(json.dumps(ref)); wrong["valid"][0] = False
        with self.assertRaisesRegex(ValueError, "mask"):
            compare_pose(ref, wrong, [0, 0, 100, 100], (100, 100))
        wrong = json.loads(json.dumps(ref)); wrong["points"][0][0] += 10
        with self.assertRaisesRegex(ValueError, "distance"):
            compare_pose(ref, wrong, [0, 0, 100, 100], (100, 100))
        wrong = json.loads(json.dumps(ref)); wrong["scores"] = [0.94] * 26
        with self.assertRaisesRegex(ValueError, "confidence"):
            compare_pose(ref, wrong, [0, 0, 100, 100], (100, 100))
        no_valid = json.loads(json.dumps(ref)); no_valid["valid"] = [False] * 26
        with self.assertRaisesRegex(ValueError, "valid|coverage"):
            compare_pose(no_valid, no_valid, [0, 0, 100, 100], (100, 100))

    def test_pose_repeat_must_also_match_reference(self):
        ref = {"points": [[0.0, 0.0] for _ in range(26)],
               "scores": [0.9] * 26, "valid": [True] * 26}
        candidate = json.loads(json.dumps(ref)); repeat = json.loads(json.dumps(ref))
        for point in candidate["points"]:
            point[0] = 0.7
        for point in repeat["points"]:
            point[0] = 1.4
        with self.assertRaisesRegex(ValueError, "distance"):
            compare_pose(ref, candidate, [0, 0, 60, 80], (100, 100), repeat)

    def test_pose_bbox_is_bounded_by_hashed_fixture_image_geometry(self):
        from PIL import Image
        from tools.models.ncnn.compare_pose_outputs import fixture_image_size
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "crop.png"
            Image.new("RGB", (100, 100), (0, 0, 0)).save(fixture)
            geometry = fixture_image_size(fixture)
            ref = {"points": [[0.0, 0.0] for _ in range(26)],
                   "scores": [0.9] * 26, "valid": [True] * 26}
            shifted = json.loads(json.dumps(ref))
            for point in shifted["points"]:
                point[0] = 100.0
            for bbox in ([0, 0, 1_000_000, 1_000_000], [10, 10, 10, 50], [-1, 0, 50, 50]):
                with self.subTest(bbox=bbox), self.assertRaisesRegex(ValueError, "bbox|fixture"):
                    compare_pose(ref, shifted, bbox, geometry)


if __name__ == "__main__":
    unittest.main()
