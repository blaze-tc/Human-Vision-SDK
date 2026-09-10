# Human-Vision-SDK 0.4 Implementation Plan Index

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement these plans task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver Human-Vision-SDK `0.4.0-preview.1` with a model-agnostic Unity package, replaceable native/model layers, real-time multi-person pipelines, stable tracking, real hand extension, 60 Hz game-facing skeleton output, Kinect-style rendering, and a GitHub release.

**Architecture:** The public Unity API and versioned C ABI are frozen first. All model names, tensor layouts, decoder logic, accelerator APIs, and model files remain below the ABI in replaceable native runtime/model-pack components. Runtime work is split into independent plans so Codex can implement and review each subsystem without allowing model-specific code to leak upward.

**Tech Stack:** C++17, C ABI, CMake/Ninja, ONNX Runtime, DirectML, Android NNAPI, optional Qualcomm QNN/HTP, OpenMMLab RTMO/RTMDet/RTMPose model family, Unity 2021.3 C#, UGUI, PowerShell/Python packaging scripts.

**Spec:** `docs/SDK_040_REALTIME_MULTIPERSON_DESIGN.md`

## Global Constraints

- Target release is `0.4.0-preview.1`.
- Windows x64 and Android ARM64 use the same public Unity/C ABI surface.
- Unity public code must contain no RTMO, RTMPose, COCO17, Halpe26, ONNX, QNN, NNAPI, DirectML, RKNN or ncnn-specific behavior.
- Model filenames and tensor shapes must not be mandatory Unity configuration.
- Existing 0.3.x public ABI layouts are not silently changed; additions use versioned structs/new entry points.
- Latest-frame replacement remains mandatory; no unbounded inference queue.
- Unity main thread never waits for inference.
- HandTip and Thumb are invalid when real hand inference is unavailable/stale; do not fabricate them geometrically.
- Codex owns source implementation, non-hardware tests, Windows/Android compilation, package generation, documentation and GitHub publication.
- The user owns phone/camera/RTSP/multi-person/thermal/FPS/latency acceptance. Build success is not runtime acceptance.
- Qualcomm proprietary libraries may only be redistributed when their applicable license permits it; otherwise release tooling must document how the authorized builder supplies them without committing restricted binaries.

---

## Execution order

1. **Plan 01 — Stable contracts and package isolation.** Freeze Unity-facing types/methods, add ABI version/capabilities, introduce model-pack manifests and enforce upward dependency rules.
2. **Plan 02 — Runtime pipelines and inference backends.** Add pipeline/backend registries, RTMDet-nano + RTMPose Body26 precision mode, RTMO multi-person mode, and explicit backend/provider reporting including QNN integration hooks.
3. **Plan 03 — Tracking, hand scheduling, canonical skeleton and temporal output.** Replace model-coupled tracking with stable global assignment, add Hand21 scheduling, map all pipelines into one canonical skeleton, and produce bounded 60 Hz game-facing state.
4. **Plan 04 — Unity renderer, diagnostics, package compatibility and release.** Replace the many-GameObject overlay with a batched UGUI mesh, expand HUD stats, verify the stable package against multiple model fixtures, package artifacts, update docs/version and publish GitHub release.

Do not start a later plan until the earlier plan's automated verification commands pass. Physical-device validation is never a gate for Codex to continue; it is recorded as `USER MANUAL ACCEPTANCE PENDING` in release notes.

## Final automated gate

Run from repository root after all four plans:

```powershell
pwsh -File tools/package/build_live_native.ps1
pwsh -File tools/package/compile_managed.ps1
python tools/package/package_live_sdk.py
python tools/package/package_upm.py
```

For a host configured to build tests:

```powershell
cmake -S . -B build/windows-test -DBUILD_TESTING=ON -DHV_USE_DIRECTML=ON
cmake --build build/windows-test --config Release
ctest --test-dir build/windows-test -C Release --output-on-failure
```

Expected automated result: native/managed compilation succeeds, all native contract/model/mapper/tracker/filter tests pass, UPM/package inspection passes, and no model-specific symbols are present in the stable public Unity assembly or C ABI.

## Final publication gate

Create release/tag `v0.4.0-preview.1` only after the automated gate passes. Release notes must contain separate sections named `Automatically verified` and `User manual acceptance pending`. Never place inferred phone FPS or accuracy under `Automatically verified`.
