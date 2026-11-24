# Changelog

## 0.4.0 — 2026-11-24

- Add opt-in native independent RTSP execution shared by CPU, GPU and NPU backends, while retaining Unity input compatibility and the existing C ABI.
- Keep ordinary RTSP color textures at the stream's decoded visible resolution. Model-sized analysis and latest live preview use separate outputs.
- Read the latest valid skeleton coordinates directly in Unity `Update`; demonstrate region boxes, skeleton points and lines in one live preview.
- Add runtime backend/model controls, separate source/Texture/inference rates, configuration, regions, lifecycle checks and exact matched-frame photo capture to Runtime Prefab Acceptance.
- Release matched-frame retention after photo copy completion so a completed photo does not keep live preview targets occupied.
- Include Windows x64 CPU/DirectML and Android ARM64 API 26 CPU/Vulkan native builds. Android RKNN and additional local models require the matching private addon; RGA and RK3588 hardware performance remain unverified.

The previous public release was 0.4.0-preview.17. Device evidence and remaining acceptance limits are recorded in the release report. Existing model distribution and hardware qualification markers are preserved.
