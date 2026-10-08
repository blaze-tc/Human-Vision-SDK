# Unity SDK Gameplay API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Deliver a documented one-component Unity SDK, semantic queries and a reusable UGUI settings scene based on the approved Setting reference.

**Architecture:** Add queries/configuration to Runtime and a thin HumanVisionSdk composition component to the existing Demo assembly, reusing Input and its retirement adapter. Settings UI uses public APIs and explicit serialized references. Preserve V1, source GUIDs and native/runtime payloads.

**Tech Stack:** Unity 2021.3.45f1, C#, UGUI, existing Input/Runtime C ABI, PowerShell verification.

**Spec:** ../specs/2026-10-08-unity-sdk-gameplay-api-design.md

## Global Constraints

- User approved execution on 2026-10-08, including the settings Demo. Execute inline without another approval round.
- Isolated branch codex/unity-sdk-api-settings from b523f13; do not modify the ongoing Android worktree.
- Canonical Unity source owns changes; regenerate the package copies together.
- 32 semantic joints; index is a possibly empty stable/region slot; use StableTrackId for identity.
- Raw results and sampled results remain separate. No fabricated production observations.
- Regions use left-top normalized image Rects, 1–8 people, one region per slot when enabled.
- Screen pixels are bottom-left; world positions map to a configurable XY plane, not measured depth.
- Input pixels are already upright/mirrored; do not transform them twice.
- Stop must detach, await retirement, close Input, then dispose Runtime.
- Chinese XML comments and tooltips; UI remains editable serialized UGUI.

## Review Focus

- Old/restarted sources must not publish stale occupancy or identities.
- Apply failure must not replace active/saved configuration.
- Destroying/disabling a component must not free an in-flight texture.
- A slot hole must not truncate index iteration.
- Source size/rotation/mirror and UI letterboxing must use the same coordinate space.

### Task 1: Semantic configuration and result queries

**Files:** Runtime/HumanVisionSdkConfiguration.cs, Runtime/HumanVisionSkeletonQueries.cs, Tests/EditMode/HumanVisionSdkQueryTests.cs.
**Interfaces:** Configuration owns MaxBodies, Regions, UseRegions, confidence and age limits; clone isolates arrays. Queries consume borrowed raw/sample bodies, count, monotonic result metadata and live configuration. Produce TryGetBody/Joint, occupancy, ID, caller-buffer copy, screen/plane geometry.

- [ ] Write tests: invalid/overlapping/nonfinite regions, clone isolation, sparse slots, long IDs, empty/unknown/stale results, invalid confidence/joint, independently expired hands, caller copy, transformed world coordinates, screen origins, degenerate angle.
- [ ] Run focused Unity tests and record RED from absent/new behavior.
- [ ] Implement configuration validation, query helper and metadata; avoid per-query allocations.
- [ ] Compile Runtime/Demo/Editor and Android conditional sources; run focused tests GREEN.
- [ ] Update status and commit verified Task 1 files.

### Task 2: One-component lifecycle and creation menu

**Files:** Demo/Sdk/HumanVisionSdk.cs, Demo/Sdk/HumanVisionSdkLifecycle.cs, Editor/HumanVisionSdkMenu.cs, Tests/EditMode/HumanVisionSdkLifecycleTests.cs.
**Interfaces:** HumanVisionSdk.Initialize(configuration) and StopSdk()/Shutdown() return IEnumerator; TryApplyConfiguration validates before queuing; expose active config copy, readiness, input state, Stats, query facade and main-thread events. Reuse Task 1 queries and current Input adapter.

- [ ] Add failing component/initialization/stop/configuration tests, including pre-start calls, cancelled initialization, repeated stop, sparse IDs, empty current result, exact-once event subscribers and retirement ordering.
- [ ] Implement coroutine operation cancellation and a persistent retirement owner for disable/destroy fallback.
- [ ] Add GameObject/Human Vision/Create SDK menu with Undo/current parent and no mandatory Canvas; compatible Renderer connection is explicit.
- [ ] Verify focused tests, packaged native initialization and all affected assemblies.
- [ ] Update status and commit verified Task 2 files.

### Task 3: Settings state and UGUI Demo

**Files:** Demo/Settings/HumanVisionSettings{Data,Store,View,Layout,Controller,RegionHandle,RegionBorder}.cs, Editor/HumanVisionSettingsDemoBuilder.cs, Tests/EditMode/HumanVisionSettingsDemoTests.cs.
**Interfaces:** SettingsData stores version, three Input modes, shared Task 1 config, renderer and log options. Store validates before writing and uses backups. View stores explicit control references; controller reads/apply/saves via Task 2 facade.

- [ ] Add RED tests for reference layout, 3-mode visibility, controls, invalid apply, clone/save/reload semantics, region drag mapping, finite bounds, CanvasRenderer, EventSystem and busy buttons.
- [ ] Adapt reference 72%/28% 1600x900 UGUI layout; source mode, people/quality, capture, mirror, region tools, drawing, advanced status and actions.
- [ ] Implement draft/active/saved distinctions, apply-save after Running, stop/reconnect and optional return callback/scene; error text redacts source credentials.
- [ ] Generate ordinary saved Prefab and HumanVisionSettingsDemo.unity through Unity Editor APIs, with stable new GUIDs.
- [ ] Run focused tests GREEN and observe actual preview/settings/region actions.
- [ ] Update status and commit verified Task 3 files.

### Task 4: Documentation, packaging and complete verification

**Files:** docs/user-guide/UNITY_SDK.md, API_REFERENCE.md, SETTINGS_DEMO.md, examples; Runtime/Demo README; maintenance CHANGE_MAP/UNITY_STABLE_API; DEVELOPMENT_STATUS; generated UPM sources/assets.

- [ ] Write runnable initialization/query/stop example, mounting/menu guide, API contracts and KinectManager mapping with Chinese comments.
- [ ] Generate UPM copies and local development packages preserving native payload hashes; include scene/Prefab or sample import entry.
- [ ] Run full affected managed tests, native ABI/public surface/architecture checks, fresh Windows build and Android managed conditional compilation.
- [ ] Import into actual Human-Vision-SDK-Test, create/run settings Demo, inspect Console and live raw skeleton/region values, retain evidence.
- [ ] Fresh-context whole-branch review; fix material findings with RED/GREEN, update status and commit.

## Execution record

Progress and rulings live in .superpowers/sdd/2026-10-08-unity-sdk-gameplay-api/progress.md and docs/reports/2026-10-08-unity-sdk-api-settings.md. No merge/push/public Release is required.
