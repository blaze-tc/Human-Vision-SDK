"""Derive a person-only NMS graph; retain the locked detector network unchanged."""
import copy
import json
from pathlib import Path

import numpy as np
import onnx

from tools.reference.contracts import sha256_file
from tools.reference.common import PROJECT_ROOT, write_json


def person_only_nms(model):
    result = copy.deepcopy(model)
    matches = [(i, n) for i, n in enumerate(result.graph.node) if n.op_type == "NonMaxSuppression"]
    if len(matches) != 1 or len(matches[0][1].input) != 5:
        raise ValueError("Expected the locked RTMDet five-input NMS graph")
    index, nms = matches[0]
    scores = nms.input[1]
    result.graph.initializer.append(onnx.numpy_helper.from_array(np.array([0], np.int64), "hv_person_index"))
    nms.input[1] = "hv_person_scores"
    result.graph.node.insert(index, onnx.helper.make_node(
        "Gather", [scores, "hv_person_index"], ["hv_person_scores"], axis=1, name="HV_PersonNmsScores"))
    onnx.checker.check_model(result)
    return result


def main():
    source = PROJECT_ROOT / "models/detector/rtmdet_tiny_640.onnx"
    info = json.loads((source.parent / "model_info.json").read_text())
    # The source manifest remains the authority for network provenance.
    from tools.reference.contracts import validate_model_info
    validate_model_info(info, source)
    target = source.with_name("rtmdet_tiny_person_640.onnx")
    onnx.save(person_only_nms(onnx.load(source)), target)
    write_json(target.with_suffix(".json"), {
        "source": source.relative_to(PROJECT_ROOT).as_posix(), "source_sha256": sha256_file(source),
        "output": target.relative_to(PROJECT_ROOT).as_posix(), "output_sha256": sha256_file(target),
        "change": "Gather COCO class 0 along score axis 1 before NMS; network and original file unchanged",
        "limitations": "Only person detections; global top-300 is now person-only. Validate on application clips.",
    })
    print(target)


if __name__ == "__main__":
    main()
