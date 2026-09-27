"""Guard the pinned ncnn AHB API hazard; device parity remains authoritative."""
import argparse
from pathlib import Path
import re


def verify(source: str) -> int:
    calls = list(re.finditer(r"import_pipeline\s*->\s*create\s*\(", source))
    assert calls, "No production AHB import creation found"
    for call in calls:
        depth, start, args = 1, call.end(), []
        for pos in range(start, len(source)):
            char = source[pos]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            if (char == "," and depth == 1) or depth == 0:
                args.append(re.sub(r"\s+", "", source[start:pos]))
                start = pos + 1
            if depth == 0:
                break
        assert len(args) == 6, "Pinned ncnn AHB import requires explicit target dimensions"
        assert args[1:3] == ["import_type", "1"], "Already-oriented producer must import with identity orientation"
        assert 'RequiresPackedAhbImport(generation_.contract.width,generation_.contract.height)?4:1' in source.replace(' ', ''), "Non-aligned planes require contiguous RGBA import"
        assert args[3:5] == ["static_cast<int>(generation_.contract.width)",
                             "static_cast<int>(generation_.contract.height)"], "Import must preserve the retained generation extent"
    return len(calls)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2] /
                        "runtime/plugins/backend/ncnn/ncnn_android_session.cpp")
    args = parser.parse_args()
    print(f"PASS: {verify(args.source.read_text(encoding='utf-8'))} AHB import call sites preserve oriented extent")
