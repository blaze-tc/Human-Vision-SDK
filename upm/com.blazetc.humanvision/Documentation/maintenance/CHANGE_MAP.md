# Change map

Run native filters with `pwsh -File tools/test/run_native_tests.ps1 -Filter FILTER`.
After focused checks, run the architecture guard.

| Need | Modify first / component README | Usually do not modify | Focused filter/check |
|---|---|---|---|
| Unity configuration asset / lifecycle / copied skeleton API | Demo/Sdk/HumanVisionSdkPreset.cs, HumanVisionSdk.cs; Demo/Settings/HumanVisionSettingsData.cs; [guide](../user-guide/SDK_CONFIGURATION.md) | native ABI, model weights, source identity | HumanVisionPresetTests; HumanVisionSdkQueryTests; HumanVisionSdkLifecycleTests; HumanVisionSettingsDemoTests |
| RTSP decoder output cadence / Unity render opportunity | [input](../../native/input/README.md), mediacodec_source.cpp, unity_input_vulkan.cpp, RtspFrameSource.cs | model weights, inference instance count, frame identity | input.OutputPump; RtspDeferredOpenTests; actual device decode/GPU/publication counts and pixel gates |
| Swap RTMO weights or compatible model | [pack](../../modelpacks/rtmo-t-416/README.md), then profile | Unity, Host, tracker | RtmoPlugin, ModelPack |
| Add pose algorithm | [pipeline](../../runtime/plugins/pipeline/rtmo/README.md) | Unity, common tracker | new plugin fixture + RuntimeSession |
| Add RKNN or another accelerator | [backend](../../runtime/plugins/backend/ort/README.md) | Unity, pose decoder | BackendPlugin, BackendFactory |
| Qualify private NCNN command batching | [NCNN backend](../../runtime/plugins/backend/ncnn/README.md), tools/benchmark/ncnn_dispatch_experiment.py | weights, public ABI, input lifecycle | NcnnDispatchBudget; test_ncnn_dispatch_experiment.py; test_ncnn_cmake_binding.py; real fresh-result capture |
| Change identity association | [services](../../runtime/services/README.md), body_services.cpp | pipeline decoder | CommonServices |
| Change smoothing or prediction | [services](../../runtime/services/README.md), Sample/Observe | pipeline decoder | CommonServices |
| Change derived structural joints | [services](../../runtime/services/README.md), Derive | gameplay, observed model indices | CommonServices |
| Change native model index mapping | [pipeline](../../runtime/plugins/pipeline/simcc/README.md) | gameplay, common services | SimccPlugin |
| Change region masking/assignment | [services](../../runtime/services/README.md) | weights | CommonServices, RuntimeSession |
| Change skeleton appearance | [renderer](../../unity/HumanVisionDemo/Assets/HumanVision/Demo/README.md) | native recognition | managed compile, geometry tests |
| Add preset | [profiles](../../profiles/README.md) | source code | architecture guard + RuntimeSession |
| Change worker lifecycle | [Host](../../runtime/host/README.md) | model decoder | RuntimeHost |
| Release runtime | [release guide](RELEASE_GUIDE.md) | gameplay | full release gates |

| Unity gameplay API / UGUI settings | Runtime/HumanVisionSkeletonQueries.cs; Demo/Sdk; Demo/Settings | [guide](../user-guide/UNITY_SDK.md), [settings](../user-guide/SETTINGS_DEMO.md) | HumanVisionSdkQueryTests / HumanVisionSdkLifecycleTests / HumanVisionSdkRuntimeTests / HumanVisionSettingsDemoTests |

| Private NPU pixel preprocessing / hybrid candidates | [tensor pipeline](../../runtime/plugins/pipeline/yolo/README.md), [RKNN tools](../../tools/models/rknn/README.md) | canonical decoder, public ABI, shipping model bytes | YoloRgbPreprocess.*, YoloTensorPipeline.*, test_rknn*.py; actual ARM64 whole-byte oracle |
| SettingsDemo resolution / CPU-NPU feed | [renderer](../../unity/HumanVisionDemo/Assets/HumanVision/Demo/README.md), VideoPlayerFrameSource, HumanVisionOverlay | model decoding, input-source lifetime | InputAdapterTests; accepted-pixel vs preview geometry; source lease/fence checks |

| Foreground/complete person selection and anti-switch hysteresis | [services](../../runtime/services/README.md), BodyServices; composition candidate capacity | model weights, raw joints, C ABI | CommonServices.Crowd*, RegionGpu*, full native tests |
| Busy / total inference-instance telemetry | RuntimeHost, GpuRuntimeHost, RuntimeSession; [device diagnostics](../../tools/setup/unity-device-diagnostics/README.md) | accelerator utilization, public ABI | RuntimeHost*, InferenceInstanceTests; device pipeline.snapshot |

| Delayed preview / pose flicker / frame identity | Demo/Input/PresentationTextureHistory.cs, PresentationPoseHistory.cs, VideoPlayerFrameSource.cs | raw SDK result timestamps / fresh counters | PresentationPoseHistoryTests; device snapshot pixel probe; inspect source/generation transitions |
| Instance routing or publication FPS | runtime/composition/session.cpp; input AndroidRtspGpuSource.Tick; diagnostics pipeline.snapshot | GPU utilization must not replace fresh-person counts | RuntimeGpuComposition / RuntimeHost; A/B device measurements, thermal evidence |
