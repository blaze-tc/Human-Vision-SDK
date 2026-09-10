# Human-Vision-SDK 0.4 Stable Contracts and Package Isolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze a model/backend-agnostic Unity and C ABI contract so future model/runtime replacements do not require gameplay, scene, prefab, or public managed API changes.

**Architecture:** Preserve current V1 ABI memory layouts, add V2 semantic configuration/results/capabilities via new symbols, and introduce a native model-pack manifest/registry below the ABI. Unity passes semantic choices only; the native runtime resolves actual models and providers.

**Tech Stack:** C++17, C ABI, CMake, Unity 2021.3 C#, P/Invoke, JSON manifest parsing, existing packaging scripts.

**Spec:** `docs/SDK_040_REALTIME_MULTIPERSON_DESIGN.md`

## Global Constraints

- Do not rename/remove existing V1 C ABI functions or reorder existing public struct fields.
- New public structs begin with `struct_size` and `api_version`.
- Public Unity assemblies may not reference model family names or inference provider implementation types.
- Model filenames are resolved from native/model-pack configuration, not hard-coded in `HumanVisionCameraManager`.
- Existing region, WebCam, RTSP and legacy 23-joint access remain available through compatibility paths.
- User hardware validation is out of Codex scope.

---

### Task 1: Lock ABI and managed public-surface snapshots

**Files:**
- Modify: `native/include/humanvision/humanvision_c.h`
- Modify: `native/include/humanvision/humanvision_types.h`
- Create: `tests/native/test_api_contract.cpp`
- Create: `tools/package/check_public_surface.py`
- Modify: `tests/native/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `HV_Config`, `HV_Body`, `HV_Joint`, region and stats declarations.
- Produces: immutable V1-size assertions plus a machine-readable public symbol/type snapshot used by all later tasks.

- [ ] **Step 1: Add a failing ABI layout test.** Assert `sizeof`, `offsetof`, enum values and existing symbol presence for every V1 public struct/function currently used by C#.

```cpp
static_assert(HV_BACKEND_ONNX_CPU == 0);
CHECK(offsetof(HV_Config, struct_size) == 0);
CHECK(sizeof(HV_Joint) == kRecordedV1JointSize);
CHECK(HV_GetVersionString() != nullptr);
```

Record the current exact V1 sizes as constants in the test before changing any header.

- [ ] **Step 2: Run the contract test and capture the current baseline.**

```powershell
cmake -S . -B build/contracts -DBUILD_TESTING=ON
cmake --build build/contracts --config Release
ctest --test-dir build/contracts -C Release -R api_contract --output-on-failure
```

Expected: PASS before V2 additions; this becomes the regression guard.

- [ ] **Step 3: Add `check_public_surface.py`.** Parse `native/include/humanvision/*.h` and `upm/com.blazetc.humanvision/Runtime/*.cs`; reject public lines containing implementation tokens `RTMO`, `RTMPose`, `COCO`, `Halpe`, `QNN`, `NNAPI`, `DirectML`, `RKNN`, `ncnn`, `ONNX` except inside explicitly internal/private interop implementation files.

- [ ] **Step 4: Run the surface checker.**

```powershell
python tools/package/check_public_surface.py
```

Expected initially: report current leaks such as backend/model configuration, giving later tasks a concrete red state.

- [ ] **Step 5: Commit.**

```bash
git add native/include/humanvision tests/native tools/package/check_public_surface.py
git commit -m "test: lock public ABI and Unity surface"
```

### Task 2: Add versioned semantic configuration and capabilities

**Files:**
- Modify: `native/include/humanvision/humanvision_types.h`
- Modify: `native/include/humanvision/humanvision_c.h`
- Modify: `native/src/core/humanvision_c.cpp`
- Modify: `native/src/core/version.cpp`
- Modify: `tests/native/test_c_api.cpp`
- Modify: `tests/native/test_api_contract.cpp`

**Interfaces:**
- Produces:
  - `uint32_t HV_GetApiVersion(void)` returning `0x00040000` for 0.4 ABI.
  - `int HV_GetCapabilities(HV_Handle, HV_CapabilitiesV1*)`.
  - `int HV_ConfigureV2(HV_Handle, const HV_ConfigV2*)`.
  - semantic enums `HV_PoseMode { AUTO, REALTIME_MULTIPERSON, PRECISION_TOPDOWN, LEGACY_WHOLEBODY }` and `HV_BackendPreference { AUTO, QNN_HTP, NNAPI, ONNX_CPU, DIRECTML, RKNN }`.

- [ ] **Step 1: Write failing tests for API version/config validation/capabilities.** Include wrong `struct_size`, unsupported version, MaxBodies 0/9, and fallback-capability cases.
- [ ] **Step 2: Run `ctest -R "c_api|api_contract"` and verify failures reference missing V2 symbols.**
- [ ] **Step 3: Add V2 structs without modifying V1 struct fields.** Required `HV_ConfigV2` semantic fields: `struct_size`, `api_version`, `max_bodies`, `pose_mode`, `backend_preference`, `target_body_fps`, `target_output_hz`, `enable_tracking`, `enable_hands`, `hand_target_fps`, detection/pose confidence values, and model-pack root UTF-8 path.
- [ ] **Step 4: Implement C entry points as forwarding calls into `HumanVisionEngine`; keep V1 create/config path working through a V1-to-V2 compatibility conversion.**
- [ ] **Step 5: Run tests; expected PASS.**
- [ ] **Step 6: Commit.**

```bash
git add native/include native/src/core tests/native
git commit -m "feat: add versioned semantic C ABI"
```

### Task 3: Freeze canonical public skeleton types

**Files:**
- Modify: `native/include/humanvision/humanvision_types.h`
- Create: `native/src/core/canonical_skeleton.h`
- Create: `tests/native/test_canonical_skeleton_contract.cpp`
- Modify: `tests/native/CMakeLists.txt`

**Interfaces:**
- Produces internal `CanonicalJointId` and public `HV_CanonicalJointId` with fixed numeric values.
- Canonical set contains pelvis/spine/neck/head, bilateral clavicle/shoulder/elbow/wrist/hand/hand-tip/thumb, and bilateral hip/knee/ankle/foot.
- Legacy 23-joint output remains mapped through compatibility access; it is not deleted.

- [ ] **Step 1: Add failing enum/topology contract test with explicit numeric values.**
- [ ] **Step 2: Implement the canonical enum once in the public header and mirror it internally through typed conversion, not duplicated magic integers.**
- [ ] **Step 3: Add `HV_CanonicalJointV1` containing normalized/source coordinates, confidence, validity and observation timestamp/age metadata.**
- [ ] **Step 4: Add new C getters `HV_GetCanonicalJointCount` and `HV_CopyCanonicalBodies`; leave existing body-copy API intact.**
- [ ] **Step 5: Run contract/C API tests.**
- [ ] **Step 6: Commit.**

### Task 4: Introduce model-pack manifest and remove Unity model filenames

**Files:**
- Create: `native/src/modelpack/model_pack_manifest.h`
- Create: `native/src/modelpack/model_pack_manifest.cpp`
- Create: `native/src/modelpack/model_pack_registry.h`
- Create: `native/src/modelpack/model_pack_registry.cpp`
- Create: `modelpacks/realtime-multiperson/manifest.json`
- Create: `modelpacks/precision-topdown/manifest.json`
- Create: `modelpacks/hands/manifest.json`
- Create: `tests/native/test_model_pack_manifest.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/native/CMakeLists.txt`

**Interfaces:**
- `ModelDescriptor { role, format, decoder_id, input_contract, output_contract, asset_path, sha256, preferred_backends, fallback_backends }`.
- `ModelPackRegistry::Resolve(pipeline_id, model_role)` returns a validated descriptor.

- [ ] **Step 1: Write manifest parser tests using temporary valid/invalid JSON fixtures.** Reject path traversal, missing SHA-256, duplicate roles, unsupported schema version and assets outside pack root.
- [ ] **Step 2: Implement manifest parser with repository-available JSON facility; if none exists, add a small dedicated dependency through CMake rather than a Unity-facing dependency.**
- [ ] **Step 3: Add three 0.4 manifests.** Manifests identify roles and decoder IDs; model binary acquisition scripts fill the exact official asset names and hashes before packaging.
- [ ] **Step 4: Implement registry resolution and SHA-256 validation before session creation.**
- [ ] **Step 5: Run `ctest -R model_pack`.** Expected PASS.
- [ ] **Step 6: Commit.**

### Task 5: Convert Unity configuration to semantic-only inputs

**Files:**
- Modify: `upm/com.blazetc.humanvision/Runtime/HumanVisionConfig.cs`
- Modify: `upm/com.blazetc.humanvision/Runtime/Interop/NativeBindings.cs` or the current interop file containing `HVConfigNative`
- Modify: `upm/com.blazetc.humanvision/Runtime/Demo/Live/HumanVisionCameraManager.cs`
- Create: `upm/com.blazetc.humanvision/Runtime/HumanVisionCapabilities.cs`
- Create: `upm/com.blazetc.humanvision/Runtime/HumanVisionPoseMode.cs`
- Create: `upm/com.blazetc.humanvision/Runtime/HumanVisionBackend.cs`
- Modify: `tools/package/compile_managed.ps1`

**Interfaces:**
- Public config becomes semantic: `MaxBodies`, `PoseMode`, `Backend`, `TargetBodyFps`, `TargetSkeletonOutputHz`, `EnableHands`, `HandTargetFps`, `EnableTracking`.
- Model paths are not public configuration.

- [ ] **Step 1: Add managed compile fixture that instantiates the new semantic config and invokes all existing legacy manager methods.**
- [ ] **Step 2: Run `tools/package/compile_managed.ps1`; expected failure until V2 bindings exist.**
- [ ] **Step 3: Add V2 P/Invoke structs and compatibility fallback for older native runtime with a clear API-version error message.**
- [ ] **Step 4: Remove hard-coded `rtmdet_tiny_person_640.onnx` and `rtmpose_s_133.onnx` extraction/loading from `HumanVisionCameraManager`; replace with model-pack root preparation only.**
- [ ] **Step 5: Run managed compile and `python tools/package/check_public_surface.py`.** Expected both PASS.
- [ ] **Step 6: Commit.**

### Task 6: Separate stable Unity integration from replaceable runtime/model artifacts

**Files:**
- Modify: `upm/com.blazetc.humanvision/package.json`
- Modify: `tools/package/package_live_sdk.py`
- Modify: `tools/package/package_upm.py`
- Modify: `tools/package/HumanVisionModelInstaller.cs`
- Create: `tools/package/verify_package_isolation.py`
- Modify: `docs/UPM_INSTALLATION.md`

**Interfaces:**
- Stable UPM public source keeps one package identity `com.blazetc.humanvision`.
- Native runtime binaries/model packs are copied by packaging/install tooling but remain separately identifiable by manifest/version/hash.

- [ ] **Step 1: Write package-isolation verifier first.** It must inspect generated UPM/tgz/unitypackage and fail if public C# files embed `.onnx`, `.rknn`, `.param`, RTMO/RTMPose model asset names, or model-specific decoder constants.
- [ ] **Step 2: Run verifier against current package; expected failure because model assets/names are coupled today.**
- [ ] **Step 3: Update package scripts to stage `Runtime/<platform>` and `ModelPacks/<pack-id>` separately while preserving Unity `.meta` GUID stability for public package files.**
- [ ] **Step 4: Ensure installing/replacing model packs never regenerates public C# `.meta` files, scenes or prefabs.**
- [ ] **Step 5: Generate package and run isolation verifier + managed compile + ABI tests.**
- [ ] **Step 6: Commit.**

```bash
git add upm tools/package docs/UPM_INSTALLATION.md
git commit -m "refactor: isolate Unity package from runtime model packs"
```
