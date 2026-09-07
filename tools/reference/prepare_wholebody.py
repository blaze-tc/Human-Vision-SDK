"""Download and pin official S1 candidate artifacts; no runtime installation."""

from __future__ import annotations

import json
import shutil
import urllib.request
import zipfile
from pathlib import Path

import onnx

from tools.reference.common import DOWNLOAD_DIR, MMPOSE_COMMIT, MMPOSE_DIR, PROJECT_ROOT, write_json
from tools.reference.contracts import sha256_file

STEMS = {
    "s": "rtmpose-s_simcc-ucoco_dw-ucoco_270e-256x192-3fd922c8_20230728",
    "m": "rtmpose-m_simcc-ucoco_dw-ucoco_270e-256x192-c8b76419_20230728",
}
BASE_URL = "https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/"
MODEL_DIR = PROJECT_ROOT / "models" / "wholebody"


def download(url: str, path: Path, expected: str | None) -> None:
    if not path.is_file():
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".partial")
        with urllib.request.urlopen(url, timeout=60) as source, temporary.open("wb") as target:
            shutil.copyfileobj(source, target)
        temporary.replace(path)
    if expected and sha256_file(path) != expected:
        raise ValueError(f"Pinned hash mismatch: {path}")


def main() -> int:
    manifest_path = MODEL_DIR / "candidates.json"
    previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {"candidates": []}
    expected_by_size = {item["size"]: item for item in previous["candidates"]}
    candidates = []
    for size, stem in STEMS.items():
        expected = expected_by_size.get(size, {})
        archive = DOWNLOAD_DIR / (stem + ".zip")
        checkpoint = DOWNLOAD_DIR / (stem + ".pth")
        zip_url = BASE_URL + "onnx_sdk/" + archive.name
        checkpoint_url = BASE_URL + checkpoint.name
        print(f"Preparing official wholebody-{size}", flush=True)
        download(zip_url, archive, expected.get("archive_sha256"))
        download(checkpoint_url, checkpoint, expected.get("checkpoint_sha256"))
        MODEL_DIR.mkdir(parents=True, exist_ok=True)
        model_path = MODEL_DIR / f"rtmpose_{size}_133.onnx"
        with zipfile.ZipFile(archive) as bundle:
            models = [item for item in bundle.infolist() if item.filename.endswith(".onnx") and not item.is_dir()]
            if len(models) != 1:
                raise ValueError(f"Expected exactly one ONNX in {archive.name}")
            # Extract only the chosen file to our own fixed destination; no archive paths are used.
            with bundle.open(models[0]) as source, model_path.open("wb") as target:
                shutil.copyfileobj(source, target)
            archive_entries = bundle.namelist()
        model_hash = sha256_file(model_path)
        if expected.get("onnx_sha256") and model_hash != expected["onnx_sha256"]:
            raise ValueError(f"Pinned ONNX hash mismatch: {model_path}")
        graph = onnx.load(str(model_path))
        onnx.checker.check_model(graph)

        def tensor_info(value):
            t = value.type.tensor_type
            return {"name": value.name, "dtype": onnx.TensorProto.DataType.Name(t.elem_type),
                    "shape": [d.dim_param or d.dim_value for d in t.shape.dim]}

        config = MMPOSE_DIR / "projects/rtmpose/rtmpose/wholebody_2d_keypoint" / f"rtmpose-{size}_8xb64-270e_coco-wholebody-256x192.py"
        candidates.append({
            "size": size, "source_commit": MMPOSE_COMMIT,
            "config_path": config.relative_to(PROJECT_ROOT).as_posix(),
            "config_sha256": sha256_file(config),
            "archive_url": zip_url, "archive_sha256": sha256_file(archive),
            "archive_entries": archive_entries,
            "checkpoint_url": checkpoint_url,
            "checkpoint_path": checkpoint.relative_to(PROJECT_ROOT).as_posix(),
            "checkpoint_sha256": sha256_file(checkpoint),
            "onnx_path": model_path.relative_to(PROJECT_ROOT).as_posix(),
            "onnx_sha256": model_hash, "onnx_bytes": model_path.stat().st_size,
            "opsets": [{"domain": item.domain, "version": item.version} for item in graph.opset_import],
            "inputs": [tensor_info(v) for v in graph.graph.input],
            "outputs": [tensor_info(v) for v in graph.graph.output],
            "release_status": "evaluation_only_not_cleared_for_redistribution",
        })
        print(json.dumps(candidates[-1], indent=2), flush=True)
    write_json(manifest_path, {"schema_version": 1, "candidates": candidates})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
