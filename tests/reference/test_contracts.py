from __future__ import annotations

import hashlib
import importlib
import tempfile
import unittest
from pathlib import Path


class ModelContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp_dir.cleanup)
        self.model_path = Path(self.temp_dir.name) / "fixture.onnx"
        self.model_path.write_bytes(b"human-vision-onnx-fixture")

    def _contracts(self):
        try:
            return importlib.import_module("tools.reference.contracts")
        except ModuleNotFoundError:
            self.fail(
                "tools.reference.contracts is missing; implement the D0.1 artifact "
                "contract before this acceptance test can pass"
            )

    def _complete_info(self) -> dict:
        return {
            "schema_version": 1,
            "model_role": "detector",
            "family": "RTMDet",
            "source": {
                "framework": "MMDetection",
                "framework_version": "3.2.0",
                "config_identifier": "rtmdet_tiny_8xb32-300e_coco",
                "config_path": "configs/rtmdet/rtmdet_tiny_8xb32-300e_coco.py",
                "checkpoint_filename": "rtmdet_tiny_fixture.pth",
                "checkpoint_url": "https://download.openmmlab.com/fixture.pth",
            },
            "export": {
                "tool": "MMDeploy",
                "tool_version": "1.3.1",
                "command": ["python", "tools/deploy.py"],
                "opset": 11,
            },
            "onnx": {
                "filename": self.model_path.name,
                "sha256": hashlib.sha256(self.model_path.read_bytes()).hexdigest(),
                "inputs": [
                    {
                        "name": "input",
                        "shape": [1, 3, 640, 640],
                        "dtype": "float32",
                    }
                ],
                "outputs": [
                    {
                        "name": "dets",
                        "shape": [1, "num_dets", 5],
                        "dtype": "float32",
                    }
                ],
            },
            "preprocessing": {"input_size": [640, 640]},
            "postprocessing": {"class_filter": "person"},
        }

    def test_sha256_file_matches_known_digest(self) -> None:
        contracts = self._contracts()

        actual = contracts.sha256_file(self.model_path)

        expected = hashlib.sha256(b"human-vision-onnx-fixture").hexdigest()
        self.assertEqual(expected, actual)

    def test_complete_model_info_matches_binary(self) -> None:
        contracts = self._contracts()

        errors = contracts.validate_model_info(self._complete_info(), self.model_path)

        self.assertEqual([], errors)

    def test_missing_source_field_is_rejected(self) -> None:
        contracts = self._contracts()
        info = self._complete_info()
        del info["source"]["checkpoint_url"]

        errors = contracts.validate_model_info(info, self.model_path)

        self.assertIn("source.checkpoint_url is required", errors)

    def test_hash_mismatch_is_rejected(self) -> None:
        contracts = self._contracts()
        info = self._complete_info()
        info["onnx"]["sha256"] = "0" * 64

        errors = contracts.validate_model_info(info, self.model_path)

        self.assertIn("onnx.sha256 does not match the model file", errors)


if __name__ == "__main__":
    unittest.main()
