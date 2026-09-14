import argparse
import hashlib
import importlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


TOOLS = Path(__file__).resolve().parents[2] / "tools" / "maintenance"
sys.path.insert(0, str(TOOLS))
guard = importlib.import_module("check_architecture_boundaries")


class ArchitectureBoundaryProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for folder in (
            "runtime/host",
            "runtime/services",
            "runtime/plugins/backend",
            "runtime/plugins/pipeline",
            "modelpacks",
            "profiles",
            "docs/maintenance/DECISIONS",
            "tools/package",
        ):
            (self.root / folder).mkdir(parents=True, exist_ok=True)
        for name in (
            "START_HERE",
            "CHANGE_MAP",
            "COMPONENT_INDEX",
            "PLUGIN_DEVELOPMENT",
            "MODEL_PACK_GUIDE",
            "PROFILE_GUIDE",
            "UNITY_STABLE_API",
            "DEBUGGING_GUIDE",
            "RELEASE_GUIDE",
        ):
            (self.root / "docs" / "maintenance" / f"{name}.md").write_text("x" * 101, encoding="utf-8")
        (self.root / "docs" / "maintenance" / "DECISIONS" / "test.md").write_text("decision", encoding="utf-8")
        self.catalog = self.root / "docs" / "maintenance" / "GENERATED_COMPONENT_CATALOG.md"
        self.catalog.write_text("catalog\n", encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def component(self, identity, component_type, capabilities):
        return (
            self.root / "metadata" / identity / "component.json",
            {
                "id": identity,
                "type": component_type,
                "version": "1.0.0",
                "api_version": 1,
                "capabilities": capabilities,
                "dependencies": [],
                "test_targets": [],
            },
        )

    def write_pack(self, capabilities, filename="manifest.json"):
        directory = self.root / "modelpacks" / "pack.fixture"
        directory.mkdir(parents=True, exist_ok=True)
        payload = b"abc"
        (directory / "weights.dat").write_bytes(payload)
        manifest = {
            "schema_version": 1,
            "pack_id": "pack.fixture",
            "pack_version": "1.0.0",
            "pipeline_id": "pipeline.fixture",
            "capabilities": capabilities,
            "max_people": 8,
            "models": [
                {
                    "role": "body",
                    "asset_path": "weights.dat",
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }
            ],
        }
        (directory / filename).write_text(json.dumps(manifest), encoding="utf-8")

    def write_profile(self, requirements, staged=None, backend="backend.fixture"):
        profile = {
            "schema_version": 1,
            "profile": "strict",
            "body": {"pipeline": "pipeline.fixture", "modelPack": "pack.fixture"},
            "required_capabilities": requirements,
            "backend": {"preference": [backend], "allow_fallback": False},
        }
        if staged is not None:
            profile["staged_dependencies"] = staged
        (self.root / "profiles" / "strict.json").write_text(json.dumps(profile), encoding="utf-8")

    def run_guard(self, items, *, release=False, package=False):
        args = argparse.Namespace(release=release, package=package)
        with mock.patch.object(guard, "ROOT", self.root), \
             mock.patch.object(guard, "CATALOG", self.catalog), \
             mock.patch.object(guard, "components", return_value=items), \
             mock.patch.object(guard, "render", return_value="catalog\n"), \
             mock.patch.object(guard, "parse_args", return_value=args), \
             mock.patch.object(guard.subprocess, "run", return_value=SimpleNamespace(returncode=0)):
            guard.main()

    def base_items(self, pipeline_capabilities, backend_capabilities):
        return [
            self.component("pipeline.fixture", "pipeline", pipeline_capabilities),
            self.component("backend.fixture", "backend", backend_capabilities),
        ]

    def assert_guard_error(self, items, *parts, release=False, package=False):
        with self.assertRaises(SystemExit) as raised:
            self.run_guard(items, release=release, package=package)
        message = str(raised.exception)
        for part in parts:
            self.assertIn(part, message)

    def test_missing_pipeline_gpu_input_names_selected_pipeline(self):
        self.write_pack(["body_pose", "multi_person", "gpu_input"])
        self.write_profile(["body_pose", "multi_person", "gpu_input"])
        items = self.base_items(
            ["body_pose", "multi_person"],
            ["tensor_inference", "gpu_input"],
        )
        self.assert_guard_error(items, "pipeline.fixture", "gpu_input")

    def test_missing_modelpack_owned_capability_names_selected_pack(self):
        items = self.base_items(
            ["body_pose", "multi_person", "gpu_input"],
            ["tensor_inference", "gpu_input"],
        )
        for missing in ("gpu_input", "multi_person"):
            with self.subTest(missing=missing):
                capabilities = ["body_pose", "multi_person", "gpu_input"]
                capabilities.remove(missing)
                self.write_pack(capabilities)
                self.write_profile(["body_pose", "multi_person", "gpu_input"])
                self.assert_guard_error(items, "pack.fixture", missing)

    def test_missing_backend_platform_capability_names_selected_backend(self):
        self.write_pack(["body_pose", "multi_person", "vulkan"])
        self.write_profile(["body_pose", "multi_person", "vulkan"])
        items = self.base_items(
            ["body_pose", "multi_person"],
            ["tensor_inference"],
        )
        self.assert_guard_error(items, "backend.fixture", "vulkan")

    def test_unused_staged_backend_cannot_supply_selected_backend_requirement(self):
        self.write_pack(["body_pose", "multi_person"])
        self.write_profile(
            ["body_pose", "multi_person", "external-sync-fd"],
            staged={"backends": {"backend.unused": ["external-sync-fd"]}},
        )
        items = self.base_items(
            ["body_pose", "multi_person"],
            ["tensor_inference"],
        )
        self.assert_guard_error(items, "unused staged backend", "backend.unused")

    def test_selected_staged_components_supply_only_their_owned_requirements(self):
        self.write_profile(
            [
                "body_pose",
                "multi_person",
                "gpu_input",
                "vulkan",
                "fp16-storage",
                "fp16-arithmetic",
                "android-hardware-buffer",
                "external-sync-fd",
            ],
            staged={
                "pipelines": {"pipeline.fixture": ["gpu_input"]},
                "modelPacks": {"pack.fixture": ["body_pose", "multi_person", "gpu_input"]},
                "backends": {
                    "backend.future": [
                        "tensor_inference",
                        "gpu_input",
                        "vulkan",
                        "fp16-storage",
                        "fp16-arithmetic",
                        "android-hardware-buffer",
                        "external-sync-fd",
                    ]
                },
            },
            backend="backend.future",
        )
        items = [self.component("pipeline.fixture", "pipeline", ["body_pose", "multi_person"])]
        self.run_guard(items)

    def test_release_and_package_reject_explicit_pipeline_staging(self):
        self.write_pack(["body_pose", "multi_person", "gpu_input"])
        self.write_profile(
            ["body_pose", "multi_person", "gpu_input"],
            staged={"pipelines": {"pipeline.fixture": ["gpu_input"]}},
        )
        items = self.base_items(
            ["body_pose", "multi_person"],
            ["tensor_inference", "gpu_input"],
        )
        self.run_guard(items)
        self.assert_guard_error(items, "not releasable/packageable", release=True)
        self.assert_guard_error(items, "not releasable/packageable", package=True)

    def test_dual_modelpack_manifest_names_are_ambiguous(self):
        self.write_pack(["body_pose", "multi_person"], "manifest.json")
        self.write_pack(["body_pose", "multi_person"], "modelpack.json")
        self.write_profile(["body_pose", "multi_person"])
        items = self.base_items(
            ["body_pose", "multi_person"],
            ["tensor_inference"],
        )
        self.assert_guard_error(items, "manifest.json", "modelpack.json")


if __name__ == "__main__":
    unittest.main()
