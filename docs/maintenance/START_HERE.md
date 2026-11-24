# Start here

Application integration documentation: [Chinese user guide](../user-guide/README.md)
contains a code-first walkthrough for a new Unity project, Start initialization,
skeleton result examples, API semantics and SDK platform evidence. Start with
[configuration and API](../user-guide/SDK_CONFIGURATION.md). The public Release
is SDK 0.4.1 / Input 0.1.1. The matching local Android CPU/NPU resources have their own
[deployment guide](../user-guide/LOCAL_ANDROID_CPU_YOLO.md) and verification report.
Historical measurements do not establish acceptance of a later build.

0.4 keeps the V1 API and adds a semantic runtime API. Device performance remains
manual acceptance. The current implementation evidence is in
[DEVELOPMENT_STATUS](../DEVELOPMENT_STATUS.md).

For Android Vulkan startup or preview failures, start with
[Android startup and preview](DEBUGGING_GUIDE.md#android-startup-and-preview).
The [0.4.1 release report](../reports/2026-11-24-v041-release.md) records the device test
conditions and the scope of RK3588 validation.

1. Read this file.
2. Use [CHANGE_MAP](CHANGE_MAP.md) to identify the component.
3. Read only that component README and its focused tests.
4. Modify that bounded component.
5. Run its focused tests.
6. Run `python tools/maintenance/check_architecture_boundaries.py`.
7. Run full regression/package checks only when preparing a release.

For weights, begin at [ModelPack guide](MODEL_PACK_GUIDE.md); do not modify Unity,
Host or tracking. For a new algorithm, begin at [plugin guide](PLUGIN_DEVELOPMENT.md);
do not expose tensors to gameplay. For drawing, begin at the
[Unity renderer README](../../unity/HumanVisionDemo/Assets/HumanVision/Demo/README.md);
do not alter native inference. Each row in CHANGE_MAP links directly to the
component README and its test filter, so the destination is at most two hops away.

Caches and user archives are retained by user instruction. Do not clean them as
part of a maintenance change. [Release guide](RELEASE_GUIDE.md) owns publication.

## 2026-11-23 SDK 独立运行本地候选

[设计](../superpowers/specs/2026-11-23-sdk-independent-runtime-design.md)与[分阶段计划](../superpowers/plans/2026-11-23-sdk-independent-runtime-plan.md)已获用户批准，通用原生调度、完成驱动发布、增量控制API和RTSP自主输入已实现，保留Unity异步输入兼容模式。RK3588的RGA仍是可选、待设备验证的图像服务优化。[MediaPipeUnityPlugin核查](../reports/2026-11-23-mediapipe-async-runtime-analysis.md)说明借鉴依据；[Windows验收记录](../reports/2026-11-23-sdk-independent-runtime-acceptance.md)之后，用户恢复安装授权，[Android实时Texture与业务取点报告](../reports/2026-11-23-android-realtime-live-acceptance.md)记录OnePlus实测、失败候选及范围限制。本轮正式发布记录见[0.4.0报告](../reports/2026-11-24-v040-release.md)。
