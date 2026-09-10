# 0.4 v2 implementation rulings

Authority: SDK_040_PLUGIN_ARCHITECTURE_ADDENDUM.md, followed by the v2 master and
maintenance contract. LEGACY detailed tasks apply only when compatible.

1. Preserve actual V1 ABI. HV_BACKEND_AUTO=0 and HV_BACKEND_ONNX_CPU=1 in the
   shipping header; a LEGACY test snippet reversing this is illustrative, not a migration.
2. Existing legacy signatures may remain as documented deprecated compatibility
   entry points. New Unity/default configuration uses semantic options and a profile/
   model-pack root; old concrete filename fields are no longer mandatory.
3. The plugin boundary is C-compatible, versioned and size-checked. C++ vectors,
   strings, ownership or exceptions never cross a DLL boundary. Plugin allocations
   are destroyed by the same plugin, and borrowed result lifetimes are explicit.
4. Algorithm-specific source indices, normalization and decoder logic belong to
   pipeline plugins. Common skeleton services operate on semantic observations and
   derive torso centers; they never decode model-native tensor layouts.
5. Host composition selects registered IDs/capabilities from data. It must not
   branch on concrete algorithm or accelerator names. Profiles can name those IDs.
6. Backend diagnostics report requested provider, actual initialized session provider
   and fallback reason. Selecting AUTO alone does not prove hardware execution.
7. 60 Hz sampled output is distinct from raw model inference. Prediction remains
   bounded and carries the original observation timestamp and prediction duration.
8. New plan explicitly requires automated tests/build/package validation. Earlier
   no-tests guidance no longer applies to non-hardware verification. Physical camera,
   phone, RTSP, multi-person and thermal acceptance remains with the user.
9. Model assets require upstream source/license/contracts and exact SHA-256. Never
   silently substitute an incompatible model or publish missing-model placeholders.
10. Cleanup preserves user documents/media and active dependency caches. Only exact
    audited paths with regenerable contents can be removed; record reclaimed bytes.
11. Current source checkout is on codex/runtime-040. Prior preview.5 release remains
    a draft; completion/publication now targets v0.4.0-preview.1 after all gates.
