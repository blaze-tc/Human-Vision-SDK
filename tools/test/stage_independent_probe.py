"""Stage the independent-runtime probe into the existing private acceptance Demo."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def patch_demo(text):
    hook = "RuntimeIndependentAcceptanceProbe.ConfigureOptions(options);"
    if hook not in text:
        marker = "try { options.Validate(); }"
        if text.count(marker) != 1:
            raise ValueError("Expected exactly one acceptance options validation hook")
        text = text.replace(marker, hook + "\n            " + marker)
    text = text.replace("Application.targetFrameRate = requestedFps == 30 ? 30 : 60;",
                        "Application.targetFrameRate = requestedFps == 15 ? 15 : requestedFps == 30 ? 30 : 60;")
    text = text.replace("if (sdk.TryGetAnalysisInputFrame(-1, out _) && sdk.HasFreshResult",
        "if ((sdk.TryGetAnalysisInputFrame(-1, out _) || RuntimeIndependentAcceptanceProbe.NativeInputReady(sdk)) && sdk.HasFreshResult")
    text = text.replace("(matchedPreviewRequested && showSkeleton && validPresented == 0)",
        "(matchedPreviewRequested && RuntimeIndependentAcceptanceProbe.PreviewEnabled && showSkeleton && validPresented == 0)")
    native_watchdog = """                    if (RuntimeIndependentAcceptanceProbe.TryNativeInput(sdk, out var nativeFrame, out var nativeSource, out var nativeGeneration) &&
                        (nativeFrame != lastAnalysisFrame || nativeSource != analysisSource || nativeGeneration != analysisGeneration)) {
                        lastInputAt = now; lastAnalysisFrame = nativeFrame; analysisSource = nativeSource; analysisGeneration = nativeGeneration;
                    }
"""
    marker = "                    if (sdk.TryGetInputFrame(-1, out var published) &&\n"
    if native_watchdog not in text:
        if text.count(marker) != 1:
            raise ValueError("Expected one acceptance input watchdog hook")
        text = text.replace(marker, native_watchdog + marker)
    return text.replace("stageSeconds = Mathf.Clamp(seconds, 9, ThermalStageSeconds);",
                        "stageSeconds = Mathf.Clamp(seconds, 9, 3600);")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    root = args.project.resolve()
    source = Path(__file__).resolve().parent / "unity"
    demo = Path("Assets/HumanVision/Samples/RuntimePrefab/RuntimePrefabAcceptance.cs")
    if not (root / demo).is_file():
        parser.error("Project does not contain the existing RuntimePrefabAcceptance Demo")
    payloads = {
        demo: patch_demo((root / demo).read_text(encoding="utf-8-sig")).encode(),
        demo.parent / "RuntimeIndependentAcceptanceProbe.cs": (source / "RuntimeIndependentAcceptanceProbe.cs").read_bytes(),
        Path("Assets/Resources/IndependentAcceptanceLoad.shader"): (source / "IndependentAcceptanceLoad.shader").read_bytes(),
        Path("Assets/Editor/RuntimePrefabIndependentApkBuild.cs"): (source / "RuntimePrefabIndependentApkBuild.cs").read_bytes(),
        Path("Assets/Editor/RuntimePrefabIndependentEditorRestart.cs"): (source / "RuntimePrefabIndependentEditorRestart.cs").read_bytes(),
        Path("Assets/Editor/RuntimePrefabIndependentTestRecorder.cs"): (source / "RuntimePrefabIndependentTestRecorder.cs").read_bytes(),
        Path("Assets/Editor/RuntimePrefabIndependentInspect.cs"): (source / "RuntimePrefabIndependentInspect.cs").read_bytes(),
        Path("Assets/Editor/RuntimePrefabRealtimeTestBatch.cs"): (source / "RuntimePrefabRealtimeTestBatch.cs").read_bytes(),
    }
    receipt = {"project": str(root), "applied": args.apply, "files": []}
    for relative, payload in payloads.items():
        target = root / relative
        original = target.read_bytes() if target.exists() else None
        changed = original != payload
        receipt["files"].append({"path": relative.as_posix(), "changed": changed,
            "previous_sha256": hashlib.sha256(original).hexdigest() if original is not None else None,
            "sha256": hashlib.sha256(payload).hexdigest()})
        if args.apply and changed:
            if original is not None:
                backup = args.backup / relative
                backup.parent.mkdir(parents=True, exist_ok=True)
                if backup.exists() and backup.read_bytes() != original:
                    raise ValueError(f"Refusing to overwrite a different backup: {backup}")
                if not backup.exists():
                    shutil.copy2(target, backup)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
    print(json.dumps(receipt, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
