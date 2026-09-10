# Human-Vision-SDK 0.4 Unity Renderer, Diagnostics, Packaging and Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep Unity integration generic while providing a Kinect-style batched skeleton renderer, actionable runtime diagnostics, package-isolation verification and a reproducible `v0.4.0-preview.1` GitHub release.

**Architecture:** Unity receives canonical bodies/capabilities/stats through stable P/Invoke only. Rendering builds one UGUI mesh from canonical joints/bones rather than instantiating a GameObject/LineRenderer per element. Packaging treats public Unity source, native runtime and model packs as separate artifacts even when bundled in one release archive.

**Tech Stack:** Unity 2021.3, C#, UGUI `MaskableGraphic`, P/Invoke, Python/PowerShell packaging, Git/GitHub.

**Spec:** `docs/SDK_040_REALTIME_MULTIPERSON_DESIGN.md`

## Global Constraints

- Existing scenes/game scripts compile without changing model-specific logic.
- Public Unity API remains semantic/model-agnostic.
- Renderer does not allocate one GameObject per joint/bone.
- HUD distinguishes camera/body/detector/hand/raw-output/render rates and actual providers.
- Codex never claims phone FPS/accuracy/thermal acceptance.

---

### Task 1: Add canonical managed types and preserve legacy facade

**Files:**
- Modify: `upm/com.blazetc.humanvision/Runtime/HumanVisionBody.cs`
- Create: `upm/com.blazetc.humanvision/Runtime/HumanVisionCanonicalJointType.cs`
- Create: `upm/com.blazetc.humanvision/Runtime/HumanVisionCanonicalBody.cs`
- Modify: interop bindings under `upm/com.blazetc.humanvision/Runtime/Interop/`
- Modify: `upm/com.blazetc.humanvision/Runtime/HumanVisionManager.cs`
- Modify: `upm/com.blazetc.humanvision/Runtime/Demo/Live/HumanVisionCameraManager.cs`

**Interfaces:**
- New canonical accessors: `GetCanonicalJointCount()`, `TryGetCanonicalBodyByRegionIndex`, `TryGetCanonicalJointByRegionIndex`.
- Existing `GetJointCount`, `TryGetJointByRegionIndex`, `GetJointPosition2D`, `GetJointPosition`, `GetUsersCount`, region and color-image methods remain callable.

- [ ] **Step 1: Add managed compile sample that contains both old 0.3-style calls and new canonical calls in the same assembly.**
- [ ] **Step 2: Run `tools/package/compile_managed.ps1`; expected fail until bindings/types are added.**
- [ ] **Step 3: Implement V2 marshaling using reusable arrays/buffers and API-version check once during initialization.**
- [ ] **Step 4: Implement legacy facade as canonical projection, not separate model-specific logic.**
- [ ] **Step 5: Run managed compile and public-surface checker.**
- [ ] **Step 6: Commit.**

### Task 2: Replace debug skeleton GameObjects with batched Kinect-style UGUI mesh

**Files:**
- Create: `upm/com.blazetc.humanvision/Runtime/Demo/Live/HumanVisionSkeletonGraphic.cs`
- Create: `upm/com.blazetc.humanvision/Runtime/Demo/Live/HumanVisionSkeletonStyle.cs`
- Modify: `upm/com.blazetc.humanvision/Runtime/Demo/Live/HumanVisionSkeletonOverlayer.cs` to become legacy wrapper/diagnostic or mark deprecated without breaking serialized scenes.
- Modify: generated demo/settings scenes or their builder scripts.

**Interfaces:**
- `HumanVisionSkeletonGraphic : MaskableGraphic` receives manager/preview and generates bone quads + ring/circle joints in `OnPopulateMesh`.
- Fixed topology uses canonical skeleton indices only.

- [ ] **Step 1: Add editor-side pure geometry tests/helper tests for a known 100x100 rect: one bone creates the expected quad vertices/triangles; one joint creates a closed circular/ring fan; invalid endpoint hides the bone.**
- [ ] **Step 2: Implement screen/RectTransform mapping directly in UI local coordinates; avoid world camera depth conversions used by the old overlayer.**
- [ ] **Step 3: Implement bone quads, circular center/ring joints, per-track colors, confidence alpha and configurable screen-space thickness.**
- [ ] **Step 4: Reuse `VertexHelper`/pre-sized lists; do not instantiate per-joint or per-bone objects.**
- [ ] **Step 5: Update demo to use the new renderer while keeping the old component loadable for compatibility.**
- [ ] **Step 6: Run managed compile.**
- [ ] **Step 7: Commit.**

### Task 3: Expand Unity diagnostics HUD

**Files:**
- Search/modify current demo HUD script under `upm/com.blazetc.humanvision/Runtime/Demo/`
- Create: `upm/com.blazetc.humanvision/Runtime/HumanVisionStats.cs`
- Modify: `HumanVisionManager` and interop bindings.

**Interfaces:**
- HUD fields: Camera/Presentation FPS, Analysis Submit FPS, Detector FPS + pre/infer/post/total, Body FPS + pre/infer/post/total, Hand FPS + pre/infer/post/total, Raw Skeleton FPS, Output Hz, Unity FPS, dropped frames, body count, raw/output age, pipeline, requested backend, actual detector/body/hand providers, fallback reason, model profile/input sizes.

- [ ] **Step 1: Add managed marshaling/formatting tests or compile fixture using synthetic V2 stats.**
- [ ] **Step 2: Implement stats binding and rolling display update no faster than 4–10 Hz so labels do not allocate every rendered frame.**
- [ ] **Step 3: Add a single diagnostic string suitable for screenshots; do not spam per-frame `Debug.Log`.**
- [ ] **Step 4: Run managed compile.**
- [ ] **Step 5: Commit.**

### Task 4: Add multi-model package-isolation compatibility test

**Files:**
- Create: `tests/integration/package_isolation/` fixtures/harness
- Modify/create: `tools/package/verify_package_isolation.py`
- Modify: `tools/package/compile_managed.ps1`
- Modify: packaging scripts.

**Interfaces:**
- Same managed demo assembly must compile against canonical fixture outputs for RTMO, Body26, Legacy133 and Hand21 without conditional model symbols.

- [ ] **Step 1: Create serialized canonical fixture results representing each source model family; fixtures contain canonical outputs only at the Unity boundary.**
- [ ] **Step 2: Add source scan that rejects model/provider implementation names in public runtime C# and public C headers.**
- [ ] **Step 3: Add package diff test: replacing `ModelPacks/realtime-multiperson` contents may change only model-pack manifest/assets/hashes, not public C# source, public C headers, scenes, prefabs or their GUIDs.**
- [ ] **Step 4: Add compile test that builds the same managed sources once; no model-specific compilation define is allowed.**
- [ ] **Step 5: Run isolation verifier; expected PASS.**
- [ ] **Step 6: Commit.**

### Task 5: Update build/package scripts for 0.4 runtime artifacts

**Files:**
- Modify: `tools/package/build_live_native.ps1`
- Modify: `tools/package/package_live_sdk.py`
- Modify: `tools/package/package_upm.py`
- Modify: `tools/package/HumanVisionModelInstaller.cs`
- Modify: `upm/com.blazetc.humanvision/package.json`
- Modify: top-level `CMakeLists.txt`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Outputs identify stable Unity package, Windows runtime, Android runtime, model packs and SHA-256 manifest separately.

- [ ] **Step 1: Bump project/package version to `0.4.0-preview.1` only after source migration compiles.**
- [ ] **Step 2: Make build script print enabled providers and whether QNN was compiled; fallback-only Android builds remain valid artifacts but are labeled accurately.**
- [ ] **Step 3: Stage selected model packs and runtime binaries without changing stable public `.meta` GUIDs.**
- [ ] **Step 4: Generate `SHA256SUMS.txt` covering native runtimes, UPM tgz/unitypackage and model packs.**
- [ ] **Step 5: Run package generation plus isolation checker.**
- [ ] **Step 6: Commit.**

### Task 6: Update documentation for stable Unity usage and user manual testing

**Files:**
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/SDK_API.md`
- Modify: `docs/SDK_LIVE_CAMERA_GUIDE.md`
- Modify: `docs/MODEL_MANIFEST.md`
- Create: `docs/SDK_040_MANUAL_ACCEPTANCE.md`
- Modify: `docs/DEVELOPMENT_STATUS.md`
- Modify: repository README if it contains 0.3 architecture claims.

**Interfaces:**
- Documentation shows one Unity integration example independent of pipeline/backend/model.

- [ ] **Step 1: Document four-layer dependency rule and stable public API.**
- [ ] **Step 2: Document Auto/RealtimeMultiPerson/PrecisionTopDown and backend preferences without requiring users to provide model filenames.**
- [ ] **Step 3: Document manual test matrix for camera-only, 1/2/4/6/8 people, crossing, leave-return, stationary, fast motion, hands, rotation and long-run thermal observation.**
- [ ] **Step 4: State explicitly that 8-person 30 FPS and phone latency are manual acceptance targets, not build claims.**
- [ ] **Step 5: Commit.**

### Task 7: Run final automated verification and archive evidence

**Files:**
- Create/update verification logs under repository's existing `out/` convention; do not commit large transient build directories.
- Update: `docs/DEVELOPMENT_STATUS.md` with commands/results.

- [ ] **Step 1: Run native Windows tests.**

```powershell
cmake -S . -B build/windows-test -DBUILD_TESTING=ON -DHV_USE_DIRECTML=ON
cmake --build build/windows-test --config Release
ctest --test-dir build/windows-test -C Release --output-on-failure
```

Expected: all contract, backend, pipeline, model decoder, tracker, hand, mapper, temporal and existing regression tests PASS.

- [ ] **Step 2: Build release runtimes.**

```powershell
pwsh -File tools/package/build_live_native.ps1
```

Expected: Windows x64 and Android ARM64 compile. If QNN authorized dependencies are installed, QNN-enabled Android runtime also compiles; otherwise log `QNN BUILD NOT PRESENT` without treating it as phone/runtime acceptance.

- [ ] **Step 3: Compile managed package.**

```powershell
pwsh -File tools/package/compile_managed.ps1
```

Expected: Runtime/Demo/Editor plus Android conditionals compile; only explicitly documented pre-existing warnings may remain.

- [ ] **Step 4: Package and inspect.**

```powershell
python tools/package/package_live_sdk.py
python tools/package/package_upm.py
python tools/package/check_public_surface.py
python tools/package/verify_package_isolation.py
```

Expected: PASS; no duplicate GUIDs; model/runtime hashes match manifests; stable public package contains no model-specific integration requirement.

- [ ] **Step 5: Update development status with exact command outputs and the line `USER MANUAL ACCEPTANCE PENDING: Android camera, FPS, latency, accuracy, thermal, 1/2/4/6/8 people, RTSP`.**
- [ ] **Step 6: Commit verification records/docs.**

### Task 8: Publish GitHub `v0.4.0-preview.1`

**Files:**
- Release source/artifacts generated by packaging scripts.
- Tag/release metadata.

- [ ] **Step 1: Confirm working tree contains only intended release changes and all automated checks from Task 7 passed.**
- [ ] **Step 2: Push implementation commits to the authorized `blaze-tc/Human-Vision-SDK` repository.**
- [ ] **Step 3: Create annotated tag `v0.4.0-preview.1` at the verified commit and push it.**
- [ ] **Step 4: Create GitHub Release `0.4.0-preview.1` and attach UPM/unitypackage/runtime/model-pack archives plus `SHA256SUMS.txt`.**
- [ ] **Step 5: Release notes must list `Automatically verified` with build/test/package evidence and `User manual acceptance pending` with phone/camera/multi-person/thermal items.**
- [ ] **Step 6: Do not close the manual acceptance section until the user returns test results.**
