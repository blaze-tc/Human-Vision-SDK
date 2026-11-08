"""Stage weight-free desktop ONNX graphs using the bundled NCNN weights."""
import hashlib
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory

from recover_pinned_onnx import recover

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/package"))
from package_live_sdk import metadata

PACKAGE = ROOT / "upm/com.blazetc.humanvision"
RUNTIME = PACKAGE / "RuntimeData"
WEIGHT_PACK = "yolov8n-pose-rectangle512x288-fp32-quality-low-local"
LEVELS = {"low": (512, 288), "medium": (640, 384), "high": (960, 576)}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def stage():
    source = RUNTIME / "modelpacks" / WEIGHT_PACK
    reference = ROOT / "modelpacks/yolov8n-pose-512x288-ort/manifest.json"
    index_path = RUNTIME / "index.json"
    index = json.loads(index_path.read_bytes())
    files = {entry["path"]: entry["sha256"] for entry in index["files"]}
    desktop = {}
    with TemporaryDirectory() as scratch:
        for level, (width, height) in LEVELS.items():
            pack = f"yolov8n-pose-desktop-{level}"
            folder = RUNTIME / "modelpacks" / pack
            folder.mkdir(exist_ok=True)
            graph = folder / "body.onnx"
            candidate = Path(scratch) / f"{level}.onnx"
            recover(source / "yolov8n_pose.ncnn.param", source / "yolov8n_pose.ncnn.bin",
                    candidate, width, height, external_weights=True)
            graph.write_bytes(candidate.read_bytes())
            manifest = json.loads(reference.read_bytes())
            manifest.update(pack_id=pack, pipeline_id="pipeline.yolo.desktop")
            model, = manifest["models"]
            model.update(sha256=sha(graph), source=f"Bundled NCNN ModelPack {WEIGHT_PACK}",
                         conversion_recipe="tools/models/rknn/stage_windows_yolo.py (external initializers)")
            model["input_contract"].update(width=width, height=height)
            rows = sum(width // stride * (height // stride) for stride in (8, 16, 32))
            model["output_contract"].update(rows=rows, max_output_bytes={"out0": rows * 65 * 4, "out1": rows * 51 * 4})
            write_json(folder / "manifest.json", manifest)
            paths = [graph, folder / "manifest.json"]
            for backend in ("cpu", "directml"):
                name = f"windows-yolo-{backend}-{level}"
                profile = RUNTIME / "profiles" / f"{name}.json"
                write_json(profile, {
                    "schema_version": 1, "profile": name,
                    "body": {"pipeline": "pipeline.yolo.desktop", "modelPack": pack},
                    "hands": {"enabled": False, "fps": 15},
                    "body_fps": 30, "output": {"hz": 60},
                    "backend": {"preference": [f"backend.ort.{backend}"], "allow_fallback": False},
                })
                paths.append(profile)
            for path in paths + [folder]:
                meta = Path(str(path) + ".meta")
                if not meta.exists():
                    content = metadata(path.relative_to(PACKAGE).as_posix())
                    if path.is_dir():
                        content = content.replace("DefaultImporter:", "folderAsset: yes\nDefaultImporter:")
                    meta.write_bytes(content.encode("utf-8"))
                if path.is_file():
                    desktop[path.relative_to(RUNTIME).as_posix()] = sha(path)
    overlay = RUNTIME / "windows-yolo-index.json"
    write_json(overlay, {"version": "1", "files": [
        {"path": path, "sha256": digest} for path, digest in sorted(desktop.items())
    ]})
    meta = Path(str(overlay) + ".meta")
    meta.write_bytes(metadata(overlay.relative_to(PACKAGE).as_posix()).encode("utf-8"))
    files.update(desktop)
    index["files"] = [{"path": path, "sha256": digest} for path, digest in sorted(files.items())]
    index["version"] = json.loads((PACKAGE / "package.json").read_bytes())["version"]
    index["binding_correction"].update(
        windows_default_profile="windows-yolo-directml-medium",
        windows_explicit_cpu_profile="windows-yolo-cpu-medium",
        windows_native_sha256=sha(PACKAGE / "Runtime/Plugins/x86_64/humanvision.dll"),
        input_native_sha256=sha(PACKAGE.parent / "com.blazetc.humanvision.input/Runtime/Plugins/Android/arm64-v8a/libhumanvision_input.so"),
    )
    write_json(index_path, index)


if __name__ == "__main__":
    stage()
