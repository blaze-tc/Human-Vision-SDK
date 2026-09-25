# Android Vulkan ncnn Production Runtime Implementation Plan

> Milestone C Tasks C2–C6 below are historical after the user approved Revision
> 3 on 2026-09-25. The replacement plan is
> `docs/superpowers/plans/2026-09-25-android-ncnn-topdown-cadence-revision-3.md`.
> It awaits user review before implementation resumes. Milestones A/B/C1 and
> the A→B→C→D sequence remain intact.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver an API 26+ Android production runtime in which Unity camera textures stay on the GPU, cross into a three-slot AHardwareBuffer bridge, run ncnn Vulkan/FP16 inference, and publish the unchanged HumanVision canonical skeleton API. Users explicitly select `NCNN Vulkan`, `ORT XNNPACK`, or `ORT CPU` in Project Settings; NCNN failures are explicit and never fall back.

**Architecture:** Keep the existing V1 C plugin ABI and Unity public skeleton API byte-for-byte stable. Add a V2 GPU plugin extension, an Android Vulkan bridge owned by the runtime, and an ncnn Vulkan backend. Unity and ncnn use the same `VkPhysicalDevice` through separate `VkDevice` instances matched by device and driver UUID. A three-slot AHardwareBuffer ring transfers ownership with external sync-fd semaphores. Each slot caches its Unity Vulkan objects, AHB, ncnn allocator/import objects, and converted tensors. Region filtering becomes post-inference bbox/pelvis assignment. TopDown ships first; RTMO is added only after TopDown passes its device gate.

**Tech Stack:** Unity 2021.3/2022.3 C#, C++17, Android NDK API 26, Vulkan 1.1, AHardwareBuffer, Unity native rendering plugin API, Tencent ncnn 20260526, FP16 storage/arithmetic where declared by profile, CMake/CTest/GTest, Python model conversion/golden tools, PowerShell build/package scripts.

**Spec:** [Revision 2 design](../specs/2026-09-13-android-vulkan-ncnn-production-runtime-design.md)

## Global Constraints

- Execute Milestones A, B, C, and D strictly in that order. Do not start the next milestone until every automated gate is green, `docs/DEVELOPMENT_STATUS.md` contains the exact evidence, and any required device report has been accepted.
- Do not implement RTMO before Milestone D. Do not implement Hand, QNN, MediaPipe, Windows runtime work, renderer work, or ORT/NNAPI/XNNPACK/CPU performance optimization in this plan.
- Preserve `humanvision_plugin.h` V1 layout and behavior, `humanvision_v2.h` existing struct layouts and exports, canonical skeleton semantics, tracker behavior, profile/model-pack concepts, and the Unity public skeleton API.
- `NCNN Vulkan` is the Android default for new settings assets. It is strict: Vulkan, ARM64, API 26, GPU bridge, matching physical device, required external-memory/synchronization features, ncnn libraries, profile/model pack, and declared FP16 capabilities must all succeed or initialization fails with a specific error.
- `ORT XNNPACK` and `ORT CPU` remain available only through explicit Project Settings selection. The runtime must not mutate the selected mode and must not fall back between modes.
- The first packaging pass may include all three backend dependencies. Dependency stripping is outside this plan.
- A fresh skeleton result means one complete observation frame containing all current bodies. FPS is counted per observation frame, never per body.
- Required performance output is fresh observation FPS plus P50/P95 end-to-end result age, detector/pose timing, GPU bridge dropped frames, inference queue drops, and current copy path.
- Unity render thread submission is non-blocking. When all three slots are busy, increment the GPU bridge drop counter and discard the submitted frame.
- Neither the blit path nor the color-attachment path may read an entire camera frame back to CPU.
- Generated UPM content under `upm/com.blazetc.humanvision` is changed only by package generation scripts.

## Implementation File Map

### Milestone A — Explicit runtime selection and additive contracts

- Create `unity/HumanVisionDemo/Assets/HumanVision/Editor/HumanVisionAndroidRuntimeSettings.cs`: project-level serialized mode selection and Settings Provider.
- Create `unity/HumanVisionDemo/Assets/HumanVision/Editor/HumanVisionAndroidRuntimeModeRegistry.cs`: immutable descriptors for the three allowed modes.
- Create `unity/HumanVisionDemo/Assets/HumanVision/Editor/HumanVisionAndroidRuntimeBuildValidator.cs`: pure validation rules and build hook.
- Create `unity/HumanVisionDemo/Assets/HumanVision/Runtime/Android/HumanVisionAndroidRuntimeSelection.cs`: runtime reader for build metadata.
- Modify `unity/HumanVisionDemo/Assets/HumanVision/Editor/HumanVisionAndroidBuildSettings.cs`: delegate to the new validator and require API 26.
- Modify `unity/HumanVisionDemo/Assets/HumanVision/Runtime/HumanVisionRuntimeSession.cs`: resolve exactly one selected profile before native initialization.
- Create `native/include/humanvision/humanvision_android_gpu.h`: additive Android GPU submission/status ABI.
- Create `runtime/include/humanvision_plugin_v2.h`: additive GPU backend/pipeline plugin ABI.
- Modify `runtime/host/profile_manager.h` and `runtime/host/profile_manager.cpp`: strict Android requirements and schema-2 model asset parsing.
- Modify `runtime/host/backend_factory.h` and `runtime/host/backend_factory.cpp`: V2 query/registration without changing V1.
- Create `profiles/android-ncnn-vulkan.json`, `profiles/android-ort-xnnpack.json`, and `profiles/android-ort-cpu.json`.
- Create `tests/runtime/test_plugin_abi_v2.cpp`, modify `tests/runtime/test_model_profiles.cpp`, and add Unity EditMode tests.

### Milestone B — AHB bridge and ncnn Vulkan foundation

- Create `runtime/gpu/vulkan/vulkan_device_identity.h/.cpp`: UUID enumeration and exact physical-device matching.
- Create `runtime/gpu/android/ahb_capabilities.h/.cpp`: actual AHB format/usage probing and copy-path selection.
- Create `runtime/gpu/android/ahb_slot_ring.h/.cpp`: three-slot state machine and latest-frame/drop policy.
- Create `runtime/gpu/android/unity_vulkan_bridge.h/.cpp`: Unity-device imports, render events, blit/color copy commands, and signal export.
- Create `runtime/gpu/android/unity_vulkan_plugin.cpp`: Unity graphics lifecycle integration.
- Create `runtime/plugins/backend/ncnn/ncnn_vulkan_backend.h/.cpp`: explicit device selection, cached AHB imports, and explicit dtype/packing conversion.
- Create `third_party/ncnn/provenance.json`, `third_party/ncnn/LICENSE`, `third_party/ncnn/patches/0001-ahb-external-acquire.patch`, and `tools/setup/prepare_ncnn_android.ps1`.
- Create `third_party/unity-plugin-api/include/IUnityInterface.h`, `IUnityGraphics.h`, `IUnityGraphicsVulkan.h`, plus license/provenance files.
- Modify native CMake and Android package build scripts to compile API 26 ARM64 Vulkan/ncnn artifacts.
- Modify `HumanVisionLiveSource.cs` and `VideoPlayerFrameSource.cs` so NCNN mode submits the oriented GPU texture while ORT modes keep their current paths.
- Add portable unit tests and the in-demo Android bridge device-gate harness.

### Milestone C — RTMDet Nano + RTMPose TopDown production path

- Create model conversion/golden tools under `tools/models/ncnn/`.
- Create the schema-2 `precision-t-26-ncnn-fp16` model pack with ncnn `.param/.bin` assets and explicit input/output contracts.
- Extend `runtime/plugins/pipeline/simcc/` with the V2 GPU TopDown path while retaining its V1 CPU path.
- Modify `runtime/composition/session.*` to select CPU or GPU submission once at initialization.
- Modify region handling so assignment occurs after detector output by bbox overlap and pelvis containment.
- Extend tracker/snapshot/diagnostics tests and build the production `HumanVisionCameraDemo` APK for device acceptance.

### Milestone D — RTMO after TopDown acceptance

- Add RTMO ncnn conversion/golden tools and schema-2 model pack.
- Extend `runtime/plugins/pipeline/rtmo/` with its V2 GPU path.
- Add the `android-ncnn-vulkan-rtmo` profile as an explicit pipeline choice beneath the selected NCNN runtime mode.
- Run capacity 3/4/6/8 device acceptance and close the Android production documentation.

### Release closeout after Milestone D

- Update maintenance contracts, package manifests, architecture guards, UPM generation, release notes, and remote artifact verification.
- Do not publish a release until the user accepts the required target-device evidence.

## Cross-Milestone Interfaces

Add `humanvision_plugin_v2.h` with a V1 prefix and separate V2 query:

```cpp
#define HV_PLUGIN_API_V2 2u
#define HV_GPU_FRAME_API_V1 1u

typedef enum HV_GpuImageFormatV1 {
    HV_GPU_IMAGE_RGBA8_UNORM = 1,
    HV_GPU_IMAGE_BGRA8_UNORM = 2
} HV_GpuImageFormatV1;

typedef enum HV_GpuTensorTypeV1 {
    HV_GPU_TENSOR_FP32 = 1,
    HV_GPU_TENSOR_FP16 = 2
} HV_GpuTensorTypeV1;

typedef struct HV_GpuFrameRefV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* opaque_slot;
    int32_t width;
    int32_t height;
    int64_t frame_id;
    int64_t timestamp_us;
    uint64_t generation;
    uint32_t image_format;
    uint32_t flags;
} HV_GpuFrameRefV1;

typedef struct HV_GpuImageTransformV1 {
    uint32_t struct_size;
    uint32_t api_version;
    HV_Rect source_rect_px;
    int32_t output_width;
    int32_t output_height;
    uint32_t output_type;
    uint32_t output_elempack;
    uint32_t channel_order;
    float mean[4];
    float norm[4];
} HV_GpuImageTransformV1;

typedef struct HV_GpuDeviceContextV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* host_context;
    uint8_t device_uuid[16];
    uint8_t driver_uuid[16];
    uint32_t graphics_queue_family;
    uint32_t reserved;
} HV_GpuDeviceContextV1;

typedef struct HV_GpuBackendConfigV1 {
    uint32_t struct_size;
    uint32_t api_version;
    const char* model_manifest_utf8;
    const char* asset_root_utf8;
    const char* requested_provider_utf8;
} HV_GpuBackendConfigV1;

typedef struct HV_GpuBackendApiV1 {
    uint32_t struct_size;
    uint32_t api_version;
    HV_Result (HV_CALL *create)(const HV_GpuBackendConfigV1* config,
                               const HV_GpuDeviceContextV1* device,
                               void** out_instance,
                               HV_ErrorBufferV1* error);
    void (HV_CALL *destroy)(void* instance);
    HV_Result (HV_CALL *run_image)(void* instance,
                                  const HV_GpuFrameRefV1* frame,
                                  const HV_GpuImageTransformV1* transform,
                                  HV_TensorViewV1* outputs,
                                  uint32_t output_capacity,
                                  uint32_t* out_count,
                                  HV_ErrorBufferV1* error);
    HV_Result (HV_CALL *session_info)(void* instance,
                                     HV_BackendSessionInfoV1* out_info);
} HV_GpuBackendApiV1;

typedef struct HV_HostServicesV2 {
    HV_HostServicesV1 v1;
    HV_Result (HV_CALL *create_gpu_backend)(
        void* context,
        const HV_GpuBackendConfigV1* config,
        const HV_GpuDeviceContextV1* device,
        const HV_GpuBackendApiV1** out_api,
        void** out_instance,
        HV_ErrorBufferV1* error);
    void (HV_CALL *release_gpu_backend)(
        void* context,
        const HV_GpuBackendApiV1* api,
        void* instance);
} HV_HostServicesV2;

typedef struct HV_GpuPipelineApiV1 {
    uint32_t struct_size;
    uint32_t api_version;
    HV_Result (HV_CALL *create)(const HV_PipelineConfigV1* config,
                               const HV_HostServicesV2* host,
                               void** out_instance,
                               HV_ErrorBufferV1* error);
    void (HV_CALL *destroy)(void* instance);
    HV_Result (HV_CALL *process_gpu)(void* instance,
                                    const HV_GpuFrameRefV1* frame,
                                    HV_ObservationFrameV1* output,
                                    HV_ErrorBufferV1* error);
} HV_GpuPipelineApiV1;

typedef struct HV_PluginApiV2 {
    HV_PluginApiV1 v1;
    const HV_GpuBackendApiV1* gpu_backend;
    const HV_GpuPipelineApiV1* gpu_pipeline;
} HV_PluginApiV2;

typedef HV_Result (HV_CALL *HV_QueryPluginV2Fn)(
    uint32_t requested_api_version,
    HV_PluginApiV2* out_api);
```

The existing `HV_QueryPluginFn`, V1 tables, and all V1 struct offsets remain unchanged. A plugin may expose both queries. The host chooses one query once during composition.

Add `humanvision_android_gpu.h` without changing existing declarations:

```cpp
#define HV_ANDROID_GPU_UUID_SIZE 16u

typedef enum HV_AndroidGpuCopyPath {
    HV_ANDROID_GPU_COPY_UNAVAILABLE = 0,
    HV_ANDROID_GPU_COPY_BLIT = 1,
    HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT = 2
} HV_AndroidGpuCopyPath;

typedef struct HV_AndroidGpuSubmissionV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* unity_texture;
    int32_t width;
    int32_t height;
    int64_t frame_id;
    int64_t timestamp_us;
    uint32_t rotation_degrees;
    uint32_t mirrored;
} HV_AndroidGpuSubmissionV1;

typedef struct HV_AndroidGpuBridgeStatusV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t copy_path;
    uint32_t ahb_format;
    uint64_t ahb_usage;
    uint64_t ahb_format_features;
    uint64_t submitted_frames;
    uint64_t imported_frames;
    uint64_t dropped_no_slot;
    uint64_t dropped_generation;
    uint8_t unity_device_uuid[HV_ANDROID_GPU_UUID_SIZE];
    uint8_t ncnn_device_uuid[HV_ANDROID_GPU_UUID_SIZE];
    uint8_t unity_driver_uuid[HV_ANDROID_GPU_UUID_SIZE];
    uint8_t ncnn_driver_uuid[HV_ANDROID_GPU_UUID_SIZE];
} HV_AndroidGpuBridgeStatusV1;

HV_API HV_Result HV_RuntimePrepareAndroidGpuFrame(
    HV_RuntimeHandle runtime,
    const HV_AndroidGpuSubmissionV1* submission,
    void** out_render_event_data);

HV_API void* HV_GetAndroidGpuRenderEventAndDataFunction(void);

HV_API HV_Result HV_RuntimeGetAndroidGpuBridgeStatus(
    HV_RuntimeHandle runtime,
    HV_AndroidGpuBridgeStatusV1* out_status);
```

`HV_RuntimePrepareAndroidGpuFrame` reserves a free slot and creates only a small render-event command record. It never records or waits for inference on the calling thread. The render event performs the GPU copy and exports synchronization. The worker consumes the newest ready slot.

## Required Slot and Synchronization Protocol

Each of three persistent slots follows this exact state machine:

```text
Free
  -> UnityReserved
  -> UnityGpuCopySubmitted
  -> ReadyForNcnn
  -> NcnnGpuSubmitted
  -> DropDrain or Complete
  -> Free
```

- `Free -> UnityReserved`: main thread performs one atomic compare/exchange. If no slot is free, drop the new frame.
- `UnityReserved -> UnityGpuCopySubmitted`: Unity render event records the selected GPU copy into the imported AHB image and signals an exportable semaphore.
- `UnityGpuCopySubmitted -> ReadyForNcnn`: render event exports a sync fd, calls `AHardwareBuffer_acquire` for the consumer lease, publishes metadata with release ordering, and returns without waiting.
- `ReadyForNcnn -> NcnnGpuSubmitted`: inference worker atomically claims the newest generation, marks older ready slots `DropDrain`, temporarily imports the sync fd into a cached ncnn-device semaphore, records the external queue-family acquire and sampled/read-only transition, then records ncnn import and explicit conversion.
- `NcnnGpuSubmitted -> Complete`: a worker-owned completion fence/timeline value confirms ncnn no longer references the slot. The worker releases its AHB lease and resets temporary semaphore payload state.
- `DropDrain -> Free`: the worker submits the minimum GPU wait needed to consume the producer signal, waits only on the worker thread, releases the AHB lease, and recycles the slot.
- `Complete -> Free`: the worker destroys no cached image/import objects; it resets per-frame ids, timestamps, and sync-fd ownership, then publishes `Free`.
- Unity graphics shutdown first rejects new reservations, drains worker submissions, waits both devices idle off the Unity render callback, releases per-slot objects, releases AHB ownership, and unregisters the Unity graphics device callback.
- Camera session, width, height, AHB actual format/usage, rotation/mirror contract, or input-contract hash changes increment the bridge generation and rebuild all three slots after the old generation drains. Ordinary frames never create AHB, Vulkan images/views/framebuffers, allocators, import pipelines, or ncnn `VkImageMat` objects.

## Milestone A — Explicit Runtime Mode and Contract Freeze

### Task A1: Add the Project Settings runtime-mode registry

**Files:**
- Create: `unity/HumanVisionDemo/Assets/HumanVision/Editor/HumanVisionAndroidRuntimeModeRegistry.cs`
- Create: `unity/HumanVisionDemo/Assets/HumanVision/Editor/HumanVisionAndroidRuntimeSettings.cs`
- Test: `unity/HumanVisionDemo/Assets/HumanVision/Tests/EditMode/HumanVisionAndroidRuntimeSettingsTests.cs`

- [ ] Write an EditMode test asserting the default ID is `android-ncnn-vulkan`, all three IDs resolve to the exact profile IDs, and unknown IDs throw an actionable `InvalidOperationException`.

```csharp
[Test]
public void RegistryExposesOnlyApprovedModes()
{
    CollectionAssert.AreEqual(
        new[] { "android-ncnn-vulkan", "android-ort-xnnpack", "android-ort-cpu" },
        HumanVisionAndroidRuntimeModeRegistry.All.Select(x => x.Id).ToArray());
    Assert.AreEqual(
        "android-ncnn-vulkan",
        HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId);
    Assert.Throws<InvalidOperationException>(
        () => HumanVisionAndroidRuntimeModeRegistry.Resolve("automatic"));
}
```

- [ ] Run `pwsh -File tools/test/run_unity040_tests.ps1 -Unity D:/Developer/2021.3.45f1/Editor/Unity.exe` and confirm the new test fails because the registry/settings classes do not exist.
- [ ] Implement immutable descriptors with `Id`, `DisplayName`, `ProfileId`, `RequiresVulkan`, `RequiresGpuBridge`, `RequiresNcnn`, and `RequiredCapabilities`; do not add an automatic mode.
- [ ] Implement a `ScriptableSingleton` stored at `ProjectSettings/HumanVisionAndroidRuntimeSettings.asset` and a `Project/Human Vision/Android Runtime` Settings Provider. Persist changes using `Save(true)`.
- [ ] Re-run the EditMode suite and confirm it passes.
- [ ] Commit with `git commit -m "feat(unity): add explicit Android runtime settings"`.

### Task A2: Add deterministic build validation and baked selection metadata

**Files:**
- Create: `unity/HumanVisionDemo/Assets/HumanVision/Editor/HumanVisionAndroidRuntimeBuildValidator.cs`
- Create: `unity/HumanVisionDemo/Assets/HumanVision/Runtime/Android/HumanVisionAndroidRuntimeSelection.cs`
- Modify: `unity/HumanVisionDemo/Assets/HumanVision/Editor/HumanVisionAndroidBuildSettings.cs`
- Test: `unity/HumanVisionDemo/Assets/HumanVision/Tests/EditMode/HumanVisionAndroidRuntimeBuildValidatorTests.cs`

- [ ] Write pure validator tests for NCNN success and failures covering API below 26, non-ARM64, Mono, missing Vulkan, Vulkan not first, automatic graphics APIs enabled, missing `libhumanvision.so`, missing ncnn library, missing bridge symbol manifest, missing profile, and missing model-pack assets. Assert ORT modes do not require Vulkan and do not inherit NCNN failures.
- [ ] Run the EditMode suite and confirm the tests fail before implementation.
- [ ] Implement `AndroidBuildEnvironment`, `AndroidBuildValidationIssue`, and a pure `Validate(descriptor, environment)` method. The pre-build hook converts errors into `BuildFailedException`; warnings remain explicit console warnings.
- [ ] Require IL2CPP, ARM64, and minimum API 26 for the packaged Android runtime. For NCNN require manual graphics APIs with Vulkan first, the bridge/ncnn libraries, `android-ncnn-vulkan` profile, all schema-2 ncnn assets, and the expected SHA-256 index.
- [ ] Add an `IPostGenerateGradleAndroidProject` writer that emits exact application metadata keys `com.blazetc.humanvision.runtime_mode` and `com.blazetc.humanvision.profile_id`. Add the runtime reader and pass its profile ID into `HumanVisionRuntimeSession`.
- [ ] Ensure validation reports missing NCNN model assets during Milestones A/B instead of substituting an ORT profile.
- [ ] Re-run EditMode tests and the package architecture guard:

```powershell
pwsh -File tools/test/run_unity040_tests.ps1 -Unity D:/Developer/2021.3.45f1/Editor/Unity.exe
.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py
```

- [ ] Commit with `git commit -m "feat(android): validate and bake runtime mode"`.

### Task A3: Add strict profiles and schema-2 model-pack contracts

**Files:**
- Create: `profiles/android-ncnn-vulkan.json`
- Create: `profiles/android-ort-xnnpack.json`
- Create: `profiles/android-ort-cpu.json`
- Modify: `runtime/host/profile_manager.h`
- Modify: `runtime/host/profile_manager.cpp`
- Modify: `runtime/host/model_pack_manager.h`
- Modify: `runtime/host/model_pack_manager.cpp`
- Modify: `tools/maintenance/check_architecture_boundaries.py`
- Modify: `docs/maintenance/PROFILE_GUIDE.md`
- Modify: `docs/maintenance/MODEL_PACK_GUIDE.md`
- Test: `tests/runtime/test_model_profiles.cpp`

- [ ] Add failing tests that parse schema 2 model assets containing `param_path`, `bin_path`, SHA-256 values, image format, color order, normalization, tensor dtype, elempack, input size, input/output blob names, and required Vulkan/FP16 capabilities.
- [ ] Add failing tests that each approved Android profile has `allow_fallback: false`, one backend ID, one pipeline ID, and exact required capabilities. Assert a missing requirement produces the requirement name in the error.
- [ ] Run `pwsh -File tools/test/run_native_tests.ps1 -Filter ProfileManager` and confirm the new assertions fail.
- [ ] Add schema-2 parsing while retaining schema-1 parsing unchanged. Represent multi-file model assets as a vector of named files rather than overloading the schema-1 `asset_path`.
- [ ] Add capability names `vulkan`, `fp16-storage`, `fp16-arithmetic`, `android-hardware-buffer`, and `external-sync-fd` as additive bits. Keep all existing bit values fixed.
- [ ] Commit the three profiles. The NCNN profile declares the future `precision-t-26-ncnn-fp16` pack and is intentionally rejected by build validation until Milestone C installs that pack; the two ORT profiles reference their existing compatible packs and providers.
- [ ] Update both maintenance guides with complete schema-2 JSON examples and the strict no-fallback rule.
- [ ] Run the native profile tests and architecture guard. The architecture guard must understand a declared staged model-pack requirement without treating the absent pack as a releasable configuration; it must fail release/package mode until the pack exists.
- [ ] Commit with `git commit -m "feat(runtime): define strict Android profiles and model schema 2"`.

### Task A4: Freeze the additive GPU plugin and Android submission ABIs

**Files:**
- Create: `runtime/include/humanvision_plugin_v2.h`
- Create: `native/include/humanvision/humanvision_android_gpu.h`
- Modify: `runtime/host/backend_factory.h`
- Modify: `runtime/host/backend_factory.cpp`
- Modify: `runtime/CMakeLists.txt`
- Test: `tests/runtime/test_plugin_abi_v2.cpp`
- Test: `tests/runtime/test_android_gpu_abi.cpp`

- [ ] Add compile-time and runtime tests for every V1 struct size, offset, enum value, query signature, and export before including either new header.
- [ ] Add tests for the exact V2 prefix layout and Android GPU struct sizes on 64-bit builds. Assert a V1-only plugin still loads and a V2 plugin can expose either a GPU backend, a GPU pipeline, or both.
- [ ] Run `pwsh -File tools/test/run_native_tests.ps1 -Filter "PluginAbi|AndroidGpuAbi"` and confirm the V2 tests fail.
- [ ] Add the interfaces in “Cross-Milestone Interfaces.” Put Vulkan constant fallbacks such as UUID array length behind HumanVision-owned constants so public headers do not require Vulkan headers.
- [ ] Extend `BackendFactory` with separate `RegisterV2` and `CreateGpuBackend` paths. Never reinterpret a V1 function table as V2.
- [ ] Export the Android GPU functions on all platforms; return an actionable unsupported-platform result outside Android/Vulkan so Unity bindings remain link-stable.
- [ ] Run the focused native tests, full native tests, and architecture guard:

```powershell
pwsh -File tools/test/run_native_tests.ps1 -Filter "PluginAbi|AndroidGpuAbi"
pwsh -File tools/test/run_native_tests.ps1
.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py
```

- [ ] Commit with `git commit -m "feat(abi): add versioned GPU plugin contracts"`.

### Task A5: Close Milestone A

**Files:**
- Modify: `docs/DEVELOPMENT_STATUS.md`
- Create: `docs/maintenance/DECISIONS/0002-android-runtime-mode-and-gpu-abi.md`

- [ ] Run the full native suite, Unity EditMode suite, architecture guard, maintenance catalog verification, and package dry run.
- [ ] Record commands, pass counts, tool versions, commit hash, and the expected NCNN build-validation failure for the absent Milestone C model pack.
- [ ] Document the explicit-mode decision, no-fallback behavior, V1 preservation evidence, and V2 ownership rules.
- [ ] Verify `git diff --check` and confirm no generated UPM files or user-owned untracked files are staged.
- [ ] Commit with `git commit -m "docs: close Android Vulkan milestone A"`.
- [ ] Do not start Milestone B unless all A gates are green.

## Milestone B — Production AHB Bridge and ncnn Vulkan Foundation

### Task B1: Pin and prepare deterministic Android native dependencies

**Files:**
- Create: `third_party/ncnn/provenance.json`
- Create: `third_party/ncnn/LICENSE`
- Create: `third_party/ncnn/patches/0001-ahb-external-acquire.patch`
- Create: `third_party/unity-plugin-api/provenance.json`
- Create: `third_party/unity-plugin-api/LICENSE.md`
- Create: `third_party/unity-plugin-api/include/IUnityInterface.h`
- Create: `third_party/unity-plugin-api/include/IUnityGraphics.h`
- Create: `third_party/unity-plugin-api/include/IUnityGraphicsVulkan.h`
- Create: `tools/setup/prepare_ncnn_android.ps1`
- Modify: `runtime/CMakeLists.txt`
- Modify: `tools/package/build_live_native.ps1`
- Test: `tests/architecture/test_third_party_provenance.py`

- [ ] Add a failing provenance test that requires source URL, version, source commit, archive SHA-256, license path, applied patch hashes, Android ABI, API level, and build flags.
- [ ] Pin ncnn release `20260526`, commit `e54f7b1f88434e1d844ea0551b880a1cfb079ce1`, archive `ncnn-20260526-full-source.zip`, size `23279174`, and SHA-256 `754659d6fe65545cf2ef4483ffb84526fea631f8764c44b150f1601d0fb4004b`.
- [ ] Pin the Unity PluginAPI headers from `D:/Developer/2021.3.45f1/Editor/Data/PluginAPI` by storing each source hash and the Unity license. Verify those headers also compile against Unity 2022.3 before closing B.
- [ ] Make `prepare_ncnn_android.ps1` download or accept a local archive, verify size/hash before extraction, apply the one audited external-acquire patch, and build into ignored `out/ncnn-20260526/android-arm64-api26`.
- [ ] Configure ncnn with Vulkan enabled, OpenMP disabled unless already required by measured model execution, exceptions/RTTI matching the host, and ARM64 API 26. Do not enable any CPU optimization project.
- [ ] Update the HumanVision Android build to link `android`, `vulkan`, `log`, and the pinned ncnn static libraries. Preserve the existing ORT artifacts in the APK.
- [ ] Run:

```powershell
.venv-reference/Scripts/python.exe -m unittest discover -s tests/architecture -p "test_third_party_provenance.py" -v
pwsh -File tools/setup/prepare_ncnn_android.ps1 -Abi arm64-v8a -ApiLevel 26
pwsh -File tools/package/build_live_native.ps1 -Platform Android -AndroidApiLevel 26
```

- [ ] Inspect `libhumanvision.so` with the NDK ELF tools and assert ARM64, API 26 compatibility, Vulkan symbols, and no missing ncnn dependency.
- [ ] Commit with `git commit -m "build(android): pin ncnn Vulkan toolchain"`.

### Task B2: Implement physical-device identity and actual AHB capability selection

**Files:**
- Create: `runtime/gpu/vulkan/vulkan_device_identity.h`
- Create: `runtime/gpu/vulkan/vulkan_device_identity.cpp`
- Create: `runtime/gpu/android/ahb_capabilities.h`
- Create: `runtime/gpu/android/ahb_capabilities.cpp`
- Test: `tests/runtime/test_vulkan_device_identity.cpp`
- Test: `tests/runtime/test_ahb_capabilities.cpp`

- [ ] Write a portable identity test that rejects matching vendor/device IDs when `deviceUUID` differs, rejects a matching device UUID when `driverUUID` differs, and accepts only both exact UUIDs. Assert no code path accepts ncnn’s default GPU index.
- [ ] Write table-driven copy-path tests for:
  - blit selected when source transfer-src, source blit-src, AHB transfer-dst, AHB blit-dst, import, and image creation all pass;
  - color attachment selected when blit is unavailable but source sampling, AHB color-attachment features, import, and framebuffer creation pass;
  - unavailable when neither path is fully supported;
  - unavailable when the actual AHB properties disagree with the requested properties;
  - a diagnostic listing every failed requirement.
- [ ] Run `pwsh -File tools/test/run_native_tests.ps1 -Filter "VulkanDeviceIdentity|AhbCapabilities"` and confirm the tests fail.
- [ ] Enumerate Unity’s physical device with `VkPhysicalDeviceIDProperties`. Enumerate all ncnn devices, read each ncnn `physicalDevice()`, query the same properties, and call `Net::set_vulkan_device(index)` only for the exact UUID pair match. Return a dedicated diagnostic when there is no match or more than one match.
- [ ] After each `AHardwareBuffer_allocate`, call `AHardwareBuffer_describe` and retain its actual width, height, layers, format, usage, and stride. Never infer the actual contract from the allocation request.
- [ ] On Unity’s device call `vkGetAndroidHardwareBufferPropertiesANDROID` with `VkAndroidHardwareBufferFormatPropertiesANDROID` chained through `pNext`. Record `format`, `externalFormat`, and `formatFeatures`.
- [ ] For every candidate image-usage combination, call `vkGetPhysicalDeviceImageFormatProperties2` with `VkPhysicalDeviceExternalImageFormatInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID`. Reject a path if external-memory compatibility or requested image usage is absent.
- [ ] Probe allocation contracts before creating the ring. First allocate one AHB with `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE`, describe its actual properties, and evaluate/import it only for the blit candidate. If blit fails, release that probe and allocate one with `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT`, describe it again, and evaluate/import it only for the color-attachment candidate. After selecting a viable contract, allocate all three persistent slots with that exact AHB format/usage contract. Never reuse requested properties as measured properties.
- [ ] Select the blit path only when all of these are true:

```cpp
const bool blit_supported =
    source.transfer_src &&
    source.blit_src_feature &&
    ahb.transfer_dst_feature &&
    ahb.blit_dst_feature &&
    ahb.external_importable &&
    ahb.image_create_transfer_dst_sampled &&
    (!requires_scale_or_conversion || formats.support_blit_conversion);
```

- [ ] If blit is unavailable, select color attachment only when the Unity source is sampleable, the actual AHB format supports color attachment, external import succeeds, and an image with `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT` plus cached view/framebuffer can be created.
- [ ] Keep the ncnn-side imported image at `VK_IMAGE_USAGE_SAMPLED_BIT` and shader-read-only access regardless of the Unity producer path. Never request transfer-dst or color-attachment usage on the ncnn logical device.
- [ ] Return `HV_ANDROID_GPU_COPY_UNAVAILABLE` with actual format, usage, feature bits, device UUIDs, and rejection reasons when neither path is viable. Do not invoke AsyncGPUReadback or any CPU frame path.
- [ ] Run the focused tests and full native suite.
- [ ] Commit with `git commit -m "feat(vulkan): probe AHB capabilities and match devices"`.

### Task B3: Implement the reusable three-slot state machine

**Files:**
- Create: `runtime/gpu/android/ahb_slot_ring.h`
- Create: `runtime/gpu/android/ahb_slot_ring.cpp`
- Test: `tests/runtime/test_ahb_slot_ring.cpp`
- Modify: `tests/native/CMakeLists.txt`

- [ ] Write deterministic tests for every legal transition in “Required Slot and Synchronization Protocol” and assert every illegal transition is rejected.
- [ ] Add contention tests in which all slots are busy, the newest ready generation wins, older ready generations enter `DropDrain`, and counters distinguish no-slot drops from generation drops.
- [ ] Add rebuild tests asserting identical contracts reuse all per-slot handles and that width, height, actual AHB format/usage, rotation, mirror, camera session, or input-contract hash changes require a drain and generation increment.
- [ ] Run `pwsh -File tools/test/run_native_tests.ps1 -Filter AhbSlotRing` and confirm the tests fail.
- [ ] Implement `AhbSlotState` as an atomic enum and publish per-frame metadata with acquire/release ordering. Store sync-fd ownership as an explicit RAII object with exactly one close.
- [ ] Define a `SlotResources` aggregate containing all persistent Unity image/memory/view/framebuffer/command objects, AHB leases, ncnn allocator, `VkImageMat`, `ImportAndroidHardwareBufferPipeline`, temporary-import semaphore, and completion primitive.
- [ ] Implement newest-ready selection using monotonically increasing generation/frame IDs. Do not queue more work than the three slots represent.
- [ ] Implement shutdown/rebuild drain outside the Unity render callback and prove with tests that every acquired AHB and fd is released exactly once.
- [ ] Run focused and full native tests under the thread sanitizer where available on the host abstraction.
- [ ] Commit with `git commit -m "feat(android): add reusable AHB slot ring"`.

### Task B4: Implement Unity Vulkan producer paths and non-blocking render events

**Files:**
- Create: `runtime/gpu/android/unity_vulkan_bridge.h`
- Create: `runtime/gpu/android/unity_vulkan_bridge.cpp`
- Create: `runtime/gpu/android/unity_vulkan_plugin.cpp`
- Modify: `native/src/core/humanvision_c.cpp`
- Modify: `runtime/composition/session.h`
- Modify: `runtime/composition/session.cpp`
- Test: `tests/runtime/test_unity_vulkan_bridge_contract.cpp`

- [ ] Add host-side contract tests with fake Vulkan dispatch tables asserting the bridge uses the copy path selected by `AhbCapabilities`, does not call a wait from the render-event callback, and does not reserve an unavailable slot.
- [ ] Add tests that blit records the required source/destination transitions and `vkCmdBlitImage`, while color attachment records a cached render pass that samples the Unity texture and writes the AHB framebuffer.
- [ ] Add tests that both paths release Unity image access correctly and signal an exportable semaphore; assert no readback/staging-buffer API is present in the bridge dispatch table.
- [ ] Run focused tests and confirm failure.
- [ ] Register `IUnityGraphicsVulkan` callbacks and capture Unity’s instance, physical device, logical device, graphics queue family, and queue. Treat device initialize/shutdown as bridge generation boundaries.
- [ ] Import each AHB into the Unity `VkDevice` using the actual properties from Task B2. Cache both candidate path object sets only when supported; select exactly one active path for the generation.
- [ ] In `HV_RuntimePrepareAndroidGpuFrame`, atomically reserve a slot and fill a fixed-size event record. Return no-slot as a dropped submission, not a fatal runtime error.
- [ ] In the render event, obtain Unity texture access, record the active copy path, signal the slot’s exportable semaphore, export a sync fd, acquire the consumer AHB lease, publish `ReadyForNcnn`, release Unity texture access, and return. Never wait for inference.
- [ ] In color-attachment mode, cache descriptor set layout, sampler, full-screen pipeline, render pass, image view, and framebuffer per generation. The fragment shader performs any required RGBA/BGRA conversion without CPU access.
- [ ] In blit mode, require Task B2’s actual transfer/blit gates before recording `vkCmdBlitImage`; never assume `VK_IMAGE_USAGE_TRANSFER_DST_BIT`.
- [ ] Expose the additive native functions and bridge status. Non-Android or non-Vulkan calls return a precise unsupported-platform error.
- [ ] Run focused tests, the full native suite, and an Android native build.
- [ ] Commit with `git commit -m "feat(android): add Unity Vulkan AHB producer bridge"`.

### Task B5: Implement cached ncnn AHB import and explicit FP16/packing conversion

**Files:**
- Create: `runtime/plugins/backend/ncnn/ncnn_vulkan_backend.h`
- Create: `runtime/plugins/backend/ncnn/ncnn_vulkan_backend.cpp`
- Create: `runtime/plugins/backend/ncnn/component.json`
- Modify: `runtime/host/backend_factory.cpp`
- Modify: `runtime/CMakeLists.txt`
- Test: `tests/runtime/test_ncnn_input_contract.cpp`
- Test: `tests/runtime/test_ncnn_backend_registration.cpp`

- [ ] Add input-contract tests that reject unspecified color order, mean/norm, dtype, elempack, dimensions, or blob names. Add exact cases for RGBA8 AHB to RGB FP32 pack1, RGB FP16 pack1, and RGB FP16 pack4.
- [ ] Add registration tests that require `backend.ncnn.vulkan`, required Vulkan/AHB/external-sync capabilities, strict physical-device identity, and no fallback backend.
- [ ] Run focused tests and confirm failure.
- [ ] Create the ncnn `VulkanDevice` by the exact matched index from Task B2. Never call the default-device overload.
- [ ] Per slot, create and retain `VkAndroidHardwareBufferImageAllocator`, sampled/read-only `VkImageMat::from_android_hardware_buffer`, `ImportAndroidHardwareBufferPipeline`, temporary-import semaphore, `VkCompute`, and converted tensor buffers. Apply the audited patch for external queue-family acquire before shader read.
- [ ] Temporarily import the producer sync fd into the cached ncnn semaphore, record a GPU wait, transfer queue ownership from `VK_QUEUE_FAMILY_EXTERNAL`, transition to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, and record `record_import_android_hardware_buffer`.
- [ ] Explicitly invoke ncnn color conversion, normalization, resize/crop, and `VulkanDevice::convert_packing` with the contract’s `cast_type_to` and elempack. Do not rely on `Extractor` to infer or convert dtype/packing.

```cpp
const int cast_type_to =
    contract.output_type == HV_GPU_TENSOR_FP16 ? 2 : 1;
vkdev->convert_packing(imported_rgb,
                       prepared_input,
                       contract.output_elempack,
                       compute,
                       options,
                       cast_type_to);
```

- [ ] Validate requested FP16 storage and arithmetic against the matched physical device and ncnn device capabilities. Fail initialization with the missing capability name.
- [ ] Run the focused tests, full native tests, and Android build.
- [ ] Commit with `git commit -m "feat(ncnn): add cached Vulkan AHB input backend"`.

### Task B6: Route Unity camera textures by explicit runtime mode

**Files:**
- Modify: `unity/HumanVisionDemo/Assets/HumanVision/Runtime/RuntimeBindings.cs`
- Modify: `unity/HumanVisionDemo/Assets/HumanVision/Runtime/HumanVisionRuntimeSession.cs`
- Modify: `unity/HumanVisionDemo/Assets/HumanVision/Demo/Live/HumanVisionLiveSource.cs`
- Modify: `unity/HumanVisionDemo/Assets/HumanVision/Demo/VideoPlayerFrameSource.cs`
- Create: `unity/HumanVisionDemo/Assets/HumanVision/Runtime/Android/HumanVisionAndroidGpuFrameBridge.cs`
- Test: `unity/HumanVisionDemo/Assets/HumanVision/Tests/EditMode/HumanVisionAndroidGpuRoutingTests.cs`

- [ ] Add failing tests asserting NCNN mode calls only GPU prepare/render-event bindings and ORT modes call only the existing CPU submission path. Assert NCNN errors remain errors and never invoke ORT.
- [ ] Add a source-code guard test that fails when `AsyncGPUReadback`, `GetPixels`, `ReadPixels`, or a full-frame managed byte array appears in `HumanVisionAndroidGpuFrameBridge.cs`.
- [ ] Run Unity EditMode tests and confirm failure.
- [ ] Bind the additive Android GPU functions and marshal fixed-size submission/status structs without per-frame allocations.
- [ ] Reuse `HumanVisionLiveSource`’s oriented GPU RenderTexture. Pass its texture pointer, actual width/height, rotation, mirror, frame ID, and capture timestamp to the bridge, then issue `GL.IssuePluginEventAndData`.
- [ ] Keep the current AsyncGPUReadback path reachable only for the two explicitly selected ORT modes. Remove every automatic provider switch from managed initialization.
- [ ] Display selected mode, active copy path, AHB actual properties, bridge drops, and device-match result in the existing diagnostics panel.
- [ ] Run Unity EditMode tests and build the API 26 ARM64 development APK.
- [ ] Commit with `git commit -m "feat(unity): route NCNN mode through GPU frame bridge"`.

### Task B7: Pass the real-device AHB format/usage gate

**Files:**
- Create: `tools/test/build_android_gpu_bridge_gate.ps1`
- Create: `tools/test/collect_android_gpu_bridge_gate.ps1`
- Create: `docs/validation/ANDROID_NCNN_VULKAN_AHB_GATE.md`
- Create: `unity/HumanVisionDemo/Assets/HumanVision/Tests/EditMode/HumanVisionAndroidGpuGateBuild.cs`
- Modify: `docs/DEVELOPMENT_STATUS.md`

- [ ] Build the gate from the existing `HumanVisionCameraDemo` scene and the production bridge/backend objects. The development-only gate component submits live camera textures through the same three-slot AHB ring and ncnn import/explicit conversion used by Milestone C; it emits no skeleton and does not create a separate benchmark application or alternate bridge.
- [ ] Make the gate build use `NCNN Vulkan`, Vulkan first, ARM64, IL2CPP, API 26, and a generated test-only input-contract fixture under `out/android-gpu-gate-runtime`. Never relax the production build validator or commit a fake production model pack.
- [ ] On the Snapdragon 888 target device, capture:
  - `AHardwareBuffer_describe` actual format and usage;
  - `VkAndroidHardwareBufferFormatPropertiesANDROID` format, external format, and format features;
  - external image-format query results for both usage candidates;
  - selected copy path and every rejected-path reason;
  - Unity/ncnn device and driver UUID equality;
  - 10 minutes of slot-state counts, bridge drops, and lifecycle errors;
  - proof that ncnn import is sampled/read-only;
  - proof that no AsyncGPUReadback/full-frame CPU readback executes.
- [ ] Test portrait, landscape-left, landscape-right, pause/resume, background/foreground, and camera restart. Rotation or session changes may rebuild the ring; steady-state frames must reuse all slot resources.
- [ ] Fail the gate if `TRANSFER_DST` is absent and the code still attempts blit, if color attachment is selected without actual format support, if neither path is viable but initialization continues, if device UUIDs differ, if the render thread waits, or if any full-frame CPU readback occurs.
- [ ] Run:

```powershell
pwsh -File tools/test/build_android_gpu_bridge_gate.ps1 -ProjectPath unity/HumanVisionDemo
pwsh -File tools/test/collect_android_gpu_bridge_gate.ps1 -DurationMinutes 10 -OutputPath out/device-gates/milestone-b
```

- [ ] The user performs and returns the physical-device evidence. Record the accepted report, APK SHA-256, device model/OS/GPU/driver, active path, and commit hash in `ANDROID_NCNN_VULKAN_AHB_GATE.md`.
- [ ] Re-run all Milestone B host tests and Android build, update `docs/DEVELOPMENT_STATUS.md`, and commit with `git commit -m "test(android): close Vulkan AHB bridge milestone B"`.
- [ ] Do not start detector/pose conversion or Milestone C until this device gate passes.

## Milestone C — RTMDet Nano + RTMPose TopDown

### Task C1: Add deterministic ncnn model conversion and golden-test tooling

**Files:**
- Create: `tools/models/ncnn/model_contract.py`
- Create: `tools/models/ncnn/export_rtmdet_nano.py`
- Create: `tools/models/ncnn/export_rtmpose.py`
- Create: `tools/models/ncnn/convert_to_ncnn.ps1`
- Create: `tools/models/ncnn/audit_ncnn_graph.py`
- Create: `tools/models/ncnn/compare_detector_outputs.py`
- Create: `tools/models/ncnn/compare_pose_outputs.py`
- Create: `tests/reference/test_ncnn_model_contract.py`
- Modify: `docs/MODEL_MANIFEST.md`

- [ ] Write failing Python tests for deterministic source hash checks, fixed preprocessing, fixed blob names, FP16 declarations, graph operator allowlists, and manifest generation.
- [ ] Define the detector contract as RGB 320x320, explicit mean/norm, FP16 pack4 internal input where supported, and named raw box/score outputs before host-side decode/NMS. Define the RTMPose-t Body26 contract from `pose-detection_simcc_ncnn-fp16_static-256x192`: RGB 256x192 bbox crop, fixed affine transform, explicit normalization, FP16 pack4 input, and named SimCC x/y outputs.
- [ ] Make every tool refuse an input checkpoint or intermediate ONNX whose SHA-256 differs from the manifest. Store conversion commands, ncnn tool version, source opset, output hashes, and golden fixture hashes.
- [ ] Add an graph audit that fails on unsupported/custom layers, unresolved dynamic shapes, implicit casts, or unnamed output blobs.
- [ ] Define golden gates:
  - detector: identical candidate count after NMS, matched box IoU at least 0.95, score absolute error at most 0.01, and no missed reference person above the profile threshold;
  - pose: same valid-joint mask, normalized joint distance P95 at most 0.01 of bbox diagonal, maximum at most 0.03, and confidence absolute error P95 at most 0.02;
  - all fixtures: deterministic repeat output within the same tolerances.
- [ ] Run `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p "test_ncnn_model_contract.py" -v` and confirm failure, implement the tooling, then rerun to green.
- [ ] Commit with `git commit -m "test(models): add deterministic ncnn conversion gates"`.

### Task C2: Time-box RTMDet Nano conversion and enforce the detector exit condition

**Files:**
- Create: `docs/validation/RTMDET_NCNN_CONVERSION_GATE.md`
- Create: `modelpacks/precision-t-26-ncnn-fp16/detector/model.json`
- Generate: `modelpacks/precision-t-26-ncnn-fp16/detector/model.param`
- Generate: `modelpacks/precision-t-26-ncnn-fp16/detector/model.bin`
- Test: `tests/reference/test_rtmdet_ncnn_golden.py`

- [ ] Pin and verify `rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth`, then start a 24-engineering-hour or three-working-day time box, whichever occurs first. Record each conversion attempt, failing operator, graph rewrite, and golden result. Permit at most two focused correction iterations after the baseline attempt.
- [ ] Run the pinned ONNX reference and ncnn Vulkan model on the exact shared golden image set. Run the graph audit and detector tolerances from Task C1.
- [ ] Stop RTMDet work immediately when any of these exit conditions is reached:
  - the 24-engineering-hour or three-working-day time box expires;
  - support needs more than two narrowly scoped custom layers or any persistent ncnn-core change;
  - conversion requires a fork larger than the single audited ncnn AHB patch;
  - the same golden mismatch survives two targeted graph rewrites;
  - Snapdragon 888 detector P95 exceeds the TopDown frame budget after warm-up.
- [ ] If RTMDet passes, retain ID `detector.rtmdet.nano.ncnn.fp16` and its converted assets.
- [ ] If any exit condition fires, replace only the detector with the official ncnn NanoDet-Plus-m 320 Android/Vulkan deployment path, pin its source/checkpoint hashes, give it ID `detector.nanodet-plus-m-320.ncnn.fp16`, and run the same output and human-recall gates. Do not modify RTMPose, tracker, canonical skeleton, Unity API, or profile concepts.
- [ ] **User-approved C2 contingency (2026-09-25):** RTMDet Nano and the prescribed NanoDet-Plus-m 320 path both exceeded the 33.33 ms complete-detector P95 period on Snapdragon 888; see `docs/validation/RTMDET_NCNN_CONVERSION_GATE.md`. Under Revision 2 §9.1, evaluate at most **two further mature ncnn Android person detectors** within C2. Spend at most 24 engineering hours or three working days in total **from the start of candidate research**, including source/license triage; record the start time in the gate report. Candidate research must establish a reproducible source, weight provenance and redistribution terms before selecting a production ModelPack asset. For each candidate, pin source/model/tool hashes, require a static mobile input and wholly Vulkan-supported FP16 graph without CPU fallback, run the same fixed real-image person-count and ONNX-or-PyTorch-to-ncnn golden gates, then measure three warmed 100-frame paired complete-detector P95 runs on the authorized Snapdragon 888 (pre-uploaded real FP16 `VkMat` through graph, required output downloads, decode and NMS). Reject any run above 33.33 ms; report remaining time for pose and bridge. Do not choose from graph-only timing, one image, an unclear weight license, or an untested model. The substitute may change only detector model, decoder and detector ModelPack assets; model-specific conversion/golden tooling may be extended with regression tests. Preserve the AHB bridge, backend interface, pose, tracker, skeleton, Region, snapshots and Unity API. If neither candidate qualifies, stop C2 for a new user design decision; do not start C3 or weaken the FPS/age gates.
- [ ] Record the selected detector ID, evidence, model hashes, conversion commands, and reason for retaining or replacing RTMDet in `RTMDET_NCNN_CONVERSION_GATE.md`.
- [ ] Run:

```powershell
.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p "test_rtmdet_ncnn_golden.py" -v
.venv-reference/Scripts/python.exe tools/models/ncnn/audit_ncnn_graph.py --manifest modelpacks/precision-t-26-ncnn-fp16/detector/model.json
```

- [ ] Commit with `git commit -m "feat(models): add accepted ncnn person detector"`.

### Task C3: Convert RTMPose and create the schema-2 FP16 model pack

**Files:**
- Create: `modelpacks/precision-t-26-ncnn-fp16/modelpack.json`
- Create: `modelpacks/precision-t-26-ncnn-fp16/body/model.json`
- Generate: `modelpacks/precision-t-26-ncnn-fp16/body/model.param`
- Generate: `modelpacks/precision-t-26-ncnn-fp16/body/model.bin`
- Modify: `profiles/android-ncnn-vulkan.json`
- Test: `tests/reference/test_rtmpose_ncnn_golden.py`
- Test: `tests/runtime/test_ncnn_modelpack.cpp`

- [ ] Write a failing native model-pack test that loads both selected detector files and RTMPose files, verifies every SHA-256, resolves all blob names, and rejects absent Vulkan/FP16 declarations.
- [ ] Convert RTMPose using the pinned toolchain and run the pose golden tests from Task C1 over full-body, cropped, partial, rotated, and mirrored fixtures.
- [ ] Create schema-2 manifests containing exact file hashes, license/provenance, input dimensions, image/color contract, affine crop contract, mean/norm, dtype, elempack, blob names, output interpretation, skeleton schema, and required capabilities.
- [ ] Update `android-ncnn-vulkan.json` to reference the completed pack and selected detector ID. Retain `allow_fallback: false`.
- [ ] Run golden tests, native model-pack tests, architecture checks, and production build validation. NCNN mode must now pass model/profile validation.
- [ ] Commit with `git commit -m "feat(models): package ncnn RTMPose TopDown models"`.

### Task C4: Implement the GPU TopDown pipeline without changing the CPU plugin

**Files:**
- Create: `runtime/plugins/pipeline/simcc/topdown_gpu_pipeline.h`
- Create: `runtime/plugins/pipeline/simcc/topdown_gpu_pipeline.cpp`
- Modify: `runtime/plugins/pipeline/simcc/simcc_pipeline.cpp`
- Modify: `runtime/plugins/pipeline/simcc/component.json`
- Modify: `runtime/composition/session.h`
- Modify: `runtime/composition/session.cpp`
- Test: `tests/runtime/test_topdown_gpu_pipeline.cpp`
- Test: `tests/runtime/test_runtime_gpu_composition.cpp`

- [ ] Add failing tests using a deterministic fake GPU backend that assert one detector run per source frame, one pose crop per selected person, one complete observation publication after all current bodies finish, and no partial per-person publication.
- [ ] Assert runtime composition chooses the V2 GPU pipeline only for `backend.ncnn.vulkan`; both ORT profiles continue through the unchanged V1 CPU pipeline.
- [ ] Assert NCNN initialization errors propagate unchanged and do not trigger V1/ORT composition.
- [ ] Run focused tests and confirm failure.
- [ ] Extend the existing `pipeline.topdown` plugin with a V2 table while keeping its V1 query/table byte stable. The V2 implementation sends the full GPU frame plus explicit transforms to the backend.
- [ ] Run detector once, apply detector NMS/body capacity, then create exact bbox affine crops for RTMPose. Reuse descriptor sets, command buffers, output tensors, decode arrays, and observation storage after warm-up.
- [ ] Complete all selected bodies for a source frame before publishing one `HV_ObservationFrameV1`. Set source frame ID and capture timestamp from the GPU frame ref.
- [ ] Keep latest-frame semantics: if a newer frame is ready before processing starts, drain/drop the older slot; once inference starts, finish its complete observation or mark it invalid without publishing partial data.
- [ ] Run focused tests, full native tests, Android native build, and golden tests.
- [ ] Commit with `git commit -m "feat(pipeline): add ncnn Vulkan TopDown execution"`.

### Task C5: Move Region filtering to post-inference assignment

**Files:**
- Create: `runtime/composition/region_assignment.h`
- Create: `runtime/composition/region_assignment.cpp`
- Modify: `runtime/composition/session.cpp`
- Modify: `native/src/tracking/center_iou_tracker.cpp`
- Modify: `native/src/core/result_snapshot_store.cpp`
- Test: `tests/runtime/test_region_assignment.cpp`
- Test: `tests/runtime/test_runtime_region_gpu_path.cpp`

- [ ] Add failing tests for one region/one person, overlapping regions, pelvis inside only one region, bbox overlap fallback when pelvis is unavailable, capacity-limited regions, and a person outside all regions.
- [ ] Define deterministic assignment order:
  1. eligible regions whose normalized rectangle contains the pelvis;
  2. highest bbox intersection-over-person-area;
  3. lowest region index for equal scores;
  4. unassigned when the best overlap is below the profile threshold.
- [ ] Assert region index is display/lookup placement and remains separate from tracker `track_id`.
- [ ] Add a guard test proving `MaskRegions` and CPU pixel mutation are never called by the NCNN GPU path. Keep legacy behavior available to explicit ORT profiles until a separate approved compatibility change.
- [ ] Implement assignment after detector decode and before final capacity/tracker publication. Preserve the original image for detector and pose inference.
- [ ] Publish one stable snapshot containing all assigned current bodies and explicit empty region slots. Do not count bodies as separate observation frames.
- [ ] Run focused tests, full native tests, and architecture checks.
- [ ] Commit with `git commit -m "feat(runtime): assign regions after GPU inference"`.

### Task C6: Add complete-observation latency metrics and TopDown device acceptance

**Files:**
- Modify: `native/include/humanvision/humanvision_v2.h`
- Modify: `native/src/core/stats_collector.h`
- Modify: `native/src/core/stats_collector.cpp`
- Modify: `unity/HumanVisionDemo/Assets/HumanVision/Demo/Live/HumanVisionCameraManager.cs`
- Create: `tools/test/collect_android_topdown_gate.ps1`
- Create: `docs/validation/ANDROID_NCNN_TOPDOWN_GATE.md`
- Modify: `docs/DEVELOPMENT_STATUS.md`
- Modify: `tests/native/test_stats_collector.cpp`
- Test: `unity/HumanVisionDemo/Assets/HumanVision/Tests/EditMode/HumanVisionDiagnosticsTests.cs`

- [ ] Add failing tests that count three observation frames containing 1, 4, and 8 bodies as exactly three fresh observations. Assert repeated/stale copies do not increment fresh observation FPS.
- [ ] Add a bounded, allocation-free rolling metrics window for capture-to-publication age P50/P95, detector P50/P95, aggregate pose P50/P95, bridge drops, inference queue drops, and fresh complete-observation FPS.
- [ ] Add metrics only through additive stats/versioning; do not change the existing `HV_RuntimeStatsV1` layout. Expose a new `HV_RuntimeStatsV2` query or diagnostics record with a size/version prefix.
- [ ] Update the HUD to label `Fresh observations/s`, `Age P50/P95`, `Detector P50/P95`, `Pose P50/P95`, `GPU drops`, and `Copy path`. Remove any body-count-multiplied FPS display.
- [ ] Run native and Unity tests to green.
- [ ] Build the production `HumanVisionCameraDemo` APK with Project Settings mode `NCNN Vulkan`; no bridge-only gate define is present.
- [ ] On Snapdragon 888, test capacities 1 and 2 using a 5-second warm-up followed by a 60-second measured window at a 30 FPS camera submission target, then run one 15-minute thermal test using live camera input. Acceptance requires:
  - at least 29.0 fresh complete observation frames/s in every 10-second rolling window and at least 29.5 average over the 60-second measured window;
  - no one-second result freezes;
  - end-to-end age P50 at or below 75 ms, P95 at or below 100 ms, and no sample above 250 ms outside startup, pause/resume, or rotation transitions;
  - stable track IDs through ordinary motion/brief overlap;
  - skeleton coordinates aligned with portrait and both landscape orientations;
  - zero silent fallback and zero full-frame CPU readback;
  - all bodies for an observation share one source frame ID/timestamp.
- [ ] Test Region assignment with multiple editable regions and verify only post-inference assignment affects output slots.
- [ ] The user returns device evidence. Record APK/model/profile hashes, device/driver, copy path, per-capacity metrics, thermal state, failures, and accepted result in `ANDROID_NCNN_TOPDOWN_GATE.md`.
- [ ] Re-run all C tests, update `docs/DEVELOPMENT_STATUS.md`, and commit with `git commit -m "test(android): close ncnn TopDown milestone C"`.
- [ ] Do not start RTMO or Milestone D until the TopDown gate is accepted.

## Milestone D — RTMO Multi-Person GPU Pipeline

### Task D1: Convert RTMO only after TopDown acceptance

**Files:**
- Create: `tools/models/ncnn/export_rtmo.py`
- Create: `tools/models/ncnn/compare_rtmo_outputs.py`
- Create: `modelpacks/rtmo-t-416-ncnn-fp16/modelpack.json`
- Generate: `modelpacks/rtmo-t-416-ncnn-fp16/body/model.param`
- Generate: `modelpacks/rtmo-t-416-ncnn-fp16/body/model.bin`
- Create: `tests/reference/test_rtmo_ncnn_golden.py`
- Create: `docs/validation/RTMO_NCNN_CONVERSION_GATE.md`

- [ ] Verify the committed Milestone C status and accepted device report before touching RTMO files.
- [ ] Write failing contract/golden tests for RTMO’s fixed image preprocessing, output blob shapes, person selection, keypoint decode, and canonical joint mapping.
- [ ] Convert with the same pinned ncnn toolchain and explicit FP16/packing rules. Audit the graph and run reference-versus-ncnn golden comparisons.
- [ ] Require matched-person recall 100% for reference persons above threshold, bbox IoU at least 0.95, and the Task C1 pose tolerances. Leave Milestone D open if the model cannot pass; do not substitute MediaPipe or QNN.
- [ ] Create a complete schema-2 model pack with hashes, provenance, blob contracts, output decode, and capability declarations.
- [ ] Record conversion commands, hashes, golden metrics, and accepted graph in `RTMO_NCNN_CONVERSION_GATE.md`.
- [ ] Commit with `git commit -m "feat(models): add accepted ncnn RTMO model pack"`.

### Task D2: Implement the V2 RTMO pipeline and explicit pipeline selection

**Files:**
- Create: `runtime/plugins/pipeline/rtmo/rtmo_gpu_pipeline.h`
- Create: `runtime/plugins/pipeline/rtmo/rtmo_gpu_pipeline.cpp`
- Modify: `runtime/plugins/pipeline/rtmo/rtmo_pipeline.cpp`
- Modify: `runtime/plugins/pipeline/rtmo/component.json`
- Create: `profiles/android-ncnn-vulkan-rtmo.json`
- Modify: `unity/HumanVisionDemo/Assets/HumanVision/Editor/HumanVisionAndroidRuntimeSettings.cs`
- Test: `tests/runtime/test_rtmo_gpu_pipeline.cpp`
- Test: `unity/HumanVisionDemo/Assets/HumanVision/Tests/EditMode/HumanVisionAndroidPipelineSettingsTests.cs`

- [ ] Add failing tests asserting one RTMO inference produces one complete observation containing all decoded current bodies, capacity is applied after decode, and region/tracker/snapshot services are the same services used by TopDown.
- [ ] Add a Project Settings pipeline choice visible only under `NCNN Vulkan`: `TopDown` maps to `android-ncnn-vulkan`; `RTMO` maps to `android-ncnn-vulkan-rtmo`. Keep runtime mode and pipeline choice separate.
- [ ] Assert choosing RTMO under either ORT mode is rejected at build time rather than silently remapped.
- [ ] Run focused native and Unity tests and confirm failure.
- [ ] Add a V2 GPU table to the existing RTMO plugin while preserving its V1 CPU table. Reuse the Milestone B GPU bridge/backend and Milestone C region/tracker/snapshot/stat services.
- [ ] Decode all persons into preallocated storage, apply `MaxBodies`, assign regions, update tracker IDs, and publish one observation frame. Never increment FPS per body.
- [ ] Run focused tests, full native and Unity suites, architecture checks, model golden tests, and Android build.
- [ ] Commit with `git commit -m "feat(pipeline): add ncnn Vulkan RTMO execution"`.

### Task D3: Pass 3/4/6/8-person device acceptance

**Files:**
- Create: `tools/test/collect_android_rtmo_gate.ps1`
- Create: `docs/validation/ANDROID_NCNN_RTMO_GATE.md`
- Modify: `docs/DEVELOPMENT_STATUS.md`

- [ ] Build one production APK with `NCNN Vulkan` and `RTMO` explicitly selected. Record APK, profile, model pack, and native library SHA-256 values.
- [ ] On Snapdragon 888, for each of 3, 4, 6, and 8 visible people, run a 5-second warm-up followed by a 60-second measured window at a 30 FPS camera submission target; also run a 15-minute thermal test using the same capture resolution/orientation contract. Include entrances, exits, crossings, short occlusions, and region boundaries.
- [ ] For every capacity, collect fresh complete-observation FPS, P50/P95 age, GPU bridge drops, inference queue drops, detector/body timing as applicable, thermal state, memory high-water mark, and track-ID switches.
- [ ] Require at least 29.0 fresh complete observation frames/s in every 10-second rolling window, at least 29.5 average over each 60-second measured window, age P50 at or below 75 ms, age P95 at or below 100 ms, no ordinary sample above 250 ms, and combined bridge drops at or below 1% of GPU capture requests; count one full observation per frame. Reject runs that reach 30 only by summing per-person results.
- [ ] Verify every published observation is internally complete, all bodies share one source frame/timestamp, Region index remains deterministic, and no stale skeleton is labeled current.
- [ ] Repeat portrait/landscape changes, pause/resume, camera restart, and app background/foreground. Confirm ring rebuilds only on contract/session changes and no AHB/fd/Vulkan resource leaks occur.
- [ ] Verify NCNN still fails fast when a required capability/library/model is deliberately removed, and both ORT modes run only after explicit rebuild with their selected modes.
- [ ] The user returns device evidence. Record the accepted results and any explicitly accepted hardware limitation in `ANDROID_NCNN_RTMO_GATE.md`.
- [ ] Re-run full automated tests, architecture checks, and package dry run; update `docs/DEVELOPMENT_STATUS.md`.
- [ ] Commit with `git commit -m "test(android): close ncnn Vulkan RTMO milestone D"`.

## Release Closeout — Only After Milestone D

### Task E1: Complete maintenance and package validation

**Files:**
- Modify: `docs/maintenance/START_HERE.md`
- Modify: `docs/maintenance/COMPONENT_INDEX.md`
- Modify: `docs/maintenance/PLUGIN_DEVELOPMENT.md`
- Modify: `docs/maintenance/MODEL_PACK_GUIDE.md`
- Modify: `docs/maintenance/PROFILE_GUIDE.md`
- Create: `docs/maintenance/ANDROID_GPU_RUNTIME.md`
- Modify: `tools/package/package_live_sdk.py`
- Modify: `tools/package/package_upm.py`
- Modify: `tools/maintenance/check_architecture_boundaries.py`
- Modify: `unity/HumanVisionDemo/Assets/HumanVision/Runtime/component.json`
- Modify: `unity/HumanVisionDemo/Assets/HumanVision/Demo/component.json`
- Generate: `upm/com.blazetc.humanvision/package.json`

- [ ] Document device identity, AHB lifecycle, sync-fd ownership, copy-path probing, failure codes, slot rebuild triggers, explicit packing, model conversion, profile selection, metrics semantics, and field diagnostics.
- [ ] Update the component catalog for the GPU bridge, ncnn backend, V2 TopDown, V2 RTMO, and additive ABI headers.
- [ ] Update package scripts to include all new source/settings/profile/model/native files. Generate `libhumanvision.so.meta` with Android preload enabled and correct ARM64 settings; retain all three backend dependencies.
- [ ] Extend architecture checks to reject V1 ABI drift, undeclared model assets, missing hashes/licenses/provenance, NCNN profiles with fallback, GPU code containing CPU-readback APIs, or generated/source package drift.
- [ ] Set package version `0.4.0-preview.4`, regenerate the UPM package, and create the local package archive.
- [ ] Run:

```powershell
pwsh -File tools/test/run_native_tests.ps1
pwsh -File tools/test/run_unity040_tests.ps1 -Unity D:/Developer/2021.3.45f1/Editor/Unity.exe
.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -v
.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py
.venv-reference/Scripts/python.exe tools/package/package_live_sdk.py
.venv-reference/Scripts/python.exe tools/package/package_upm.py
.venv-reference/Scripts/python.exe tools/package/verify_package_isolation.py
pwsh -File tools/test/run_upm040_import.ps1
```

- [ ] Extract the generated package into a clean directory and compare every packaged file hash with its source/index. Import the Git URL and local archive into clean Unity projects and run EditMode plus Android build validation for each explicit mode.
- [ ] Update `docs/DEVELOPMENT_STATUS.md` with exact pass counts, hashes, tool versions, device gates, and remaining physical-device limits.
- [ ] Commit with `git commit -m "release: prepare Human Vision SDK 0.4.0 preview 4"`.

### Task E2: Publish only after explicit device acceptance

**Files:**
- Create: release tag and GitHub Release for `v0.4.0-preview.4`
- Attach: generated UPM/local-import archive and checksum file

- [ ] Confirm the user accepted Milestones B, C, and D device evidence and explicitly authorized release publication.
- [ ] Confirm the branch is clean except for the known user-owned untracked source/reference files, and verify the release commit is on the intended mainline.
- [ ] Push the verified commit and annotated tag, create the GitHub Release, upload the archive/checksum, then download each remote asset and compare SHA-256 with the local artifact.
- [ ] Verify a fresh Unity Package Manager Git import using the published tag and rerun Android build validation.
- [ ] Record release URL, tag commit, remote artifact hashes, and final verification in `docs/DEVELOPMENT_STATUS.md`.

## Plan Self-Review Checklist

- [ ] Every Revision 2 requirement maps to at least one task and one gate.
- [ ] Milestones execute only A → B → C → D; release closeout follows D and adds no runtime feature.
- [ ] Milestone B measures actual AHB properties/features on the target device and chooses blit or color attachment from supported facts.
- [ ] ncnn AHB import remains sampled/read-only; both Unity producer paths remain entirely on GPU.
- [ ] Physical device matching uses device UUID plus driver UUID and never ncnn’s default GPU index.
- [ ] The three-slot state machine defines synchronization, ownership, drops, drain, lifecycle, and rebuild behavior.
- [ ] All per-slot AHB/Vulkan/ncnn import and conversion resources are cached across frames.
- [ ] Dtype and packing conversion are explicit and covered by contract tests.
- [ ] RTMDet has a fixed time box, finite rewrite count, and a detector-only mature ncnn fallback.
- [ ] Fresh FPS counts complete observation frames, with P50/P95 age and bridge-drop metrics.
- [ ] Project Settings selection is explicit and NCNN has no automatic fallback.
- [ ] No step performs Hand, QNN, MediaPipe, Windows work, renderer work, ORT performance optimization, or APK stripping.
- [ ] V1 ABI/public skeleton API stability is tested before V2 code is accepted.
- [ ] Search the plan for unresolved markers and ambiguous substitute language:

```powershell
rg -n "TB[D]|TO[D]O|FIXM[E]|later decid[e]|similar t[o]|and so o[n]" docs/superpowers/plans/2026-09-13-android-vulkan-ncnn-production-runtime.md
```

- [ ] Run `git diff --check` and review only this plan before committing it.
