from __future__ import annotations

import hashlib
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any


def sha256_file(path: str | Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _value_at(info: Mapping[str, Any], dotted_path: str) -> Any:
    value: Any = info
    for part in dotted_path.split("."):
        if not isinstance(value, Mapping) or part not in value:
            return None
        value = value[part]
    return value


def _is_non_empty_sequence(value: Any) -> bool:
    return isinstance(value, Sequence) and not isinstance(value, (str, bytes)) and bool(value)


def validate_model_info(info: Mapping[str, Any], model_path: str | Path) -> list[str]:
    errors: list[str] = []
    required_paths = (
        "schema_version",
        "model_role",
        "family",
        "source.framework",
        "source.framework_version",
        "source.config_identifier",
        "source.config_path",
        "source.checkpoint_filename",
        "source.checkpoint_url",
        "export.tool",
        "export.tool_version",
        "export.command",
        "export.opset",
        "onnx.filename",
        "onnx.sha256",
        "onnx.inputs",
        "onnx.outputs",
        "preprocessing",
        "postprocessing",
    )

    for path in required_paths:
        value = _value_at(info, path)
        if value is None or value == "" or value == [] or value == {}:
            errors.append(f"{path} is required")

    for collection_name in ("onnx.inputs", "onnx.outputs"):
        descriptors = _value_at(info, collection_name)
        if not _is_non_empty_sequence(descriptors):
            continue
        for index, descriptor in enumerate(descriptors):
            if not isinstance(descriptor, Mapping):
                errors.append(f"{collection_name}[{index}] must be an object")
                continue
            for field in ("name", "shape", "dtype"):
                value = descriptor.get(field)
                if value is None or value == "" or value == []:
                    errors.append(f"{collection_name}[{index}].{field} is required")

    model_file = Path(model_path)
    if not model_file.is_file():
        errors.append("model file does not exist")
    else:
        expected_hash = _value_at(info, "onnx.sha256")
        if isinstance(expected_hash, str) and expected_hash and expected_hash != sha256_file(model_file):
            errors.append("onnx.sha256 does not match the model file")

    return errors
