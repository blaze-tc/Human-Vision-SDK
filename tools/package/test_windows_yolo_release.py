import hashlib
import json
from pathlib import Path
import unittest


RUNTIME = Path(__file__).resolve().parents[2] / "upm/com.blazetc.humanvision/RuntimeData"
LEVELS = {"low": (512, 288), "medium": (640, 384), "high": (960, 576)}


class WindowsYoloPackage(unittest.TestCase):
    def test_desktop_profiles_ship_with_all_quality_contracts(self):
        index = json.loads((RUNTIME / "index.json").read_bytes())
        files = {entry["path"]: entry["sha256"] for entry in index["files"]}
        for backend in ("cpu", "directml"):
            for level, dimensions in LEVELS.items():
                with self.subTest(backend=backend, level=level):
                    name = f"windows-yolo-{backend}-{level}"
                    profile_path = f"profiles/{name}.json"
                    profile = json.loads((RUNTIME / profile_path).read_bytes())
                    self.assertEqual(profile["profile"], name)
                    self.assertEqual(profile["body"]["pipeline"], "pipeline.yolo.desktop")
                    self.assertEqual(profile["backend"], {
                        "preference": [f"backend.ort.{backend}"], "allow_fallback": False
                    })
                    pack = profile["body"]["modelPack"]
                    manifest_path = f"modelpacks/{pack}/manifest.json"
                    manifest = json.loads((RUNTIME / manifest_path).read_bytes())
                    model, = manifest["models"]
                    shape = model["input_contract"]
                    self.assertEqual((shape["width"], shape["height"]), dimensions)
                    graph_path = f"modelpacks/{pack}/{model['asset_path']}"
                    self.assertLess((RUNTIME / graph_path).stat().st_size, 500_000)
                    for relative in (profile_path, manifest_path, graph_path):
                        self.assertEqual(hashlib.sha256((RUNTIME / relative).read_bytes()).hexdigest(),
                                         files[relative])


if __name__ == "__main__":
    unittest.main()
