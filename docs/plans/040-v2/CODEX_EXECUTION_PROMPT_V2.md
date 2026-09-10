# Paste this to Codex

First read:

1. `SDK_040_PLUGIN_ARCHITECTURE_ADDENDUM.md`
2. `2026-09-10-humanvision-040-master-v2.md`
3. `2026-09-10-humanvision-040-maintenance-documentation-contract.md`

Then use the `LEGACY_*.md` 0.4 plans only for detailed RTMO/RTMPose/QNN/tracking/renderer requirements that do not conflict with the v2 architecture.

The v2 architecture is mandatory: Stable Unity SDK -> Stable Host/C ABI -> Runtime Host -> versioned C Plugin ABI -> Pipeline/Backend Plugins + common services -> ModelPacks -> Profiles.

Do not place model filenames, model-native joint indices, tensor layouts or provider implementation classes in Unity public code.

Documentation is a required deliverable. Before release, build `docs/maintenance/START_HERE.md`, CHANGE_MAP, COMPONENT_INDEX, plugin/model-pack/profile/Unity/debugging/release guides, component-local READMEs, machine-readable component metadata, a generated component catalog and automatic architecture/documentation guards.

The explicit goal is that a future Codex session can start from START_HERE + CHANGE_MAP, read one component README/test set, and modify one bounded area without rereading the whole repository.

Complete implementation, automated verification, packaging, Git commits/push and GitHub release `v0.4.0-preview.1`. Do not perform or claim physical-device acceptance; the user will test that manually.
