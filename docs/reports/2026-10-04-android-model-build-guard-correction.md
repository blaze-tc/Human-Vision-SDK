# Android selected model-pack build guard correction

The formal UPM Android build guard rejected the already approved local YOLO640
FP32 profile because it required two TopDown models and fixed precision-model
directories. The corrected guard admits exactly the supported one-body YOLO
route while retaining the two-model TopDown route and explicit ORT modes.
Runtime mode, native libraries, models, input implementation and inference are
unchanged. Formal UPM has no diagnostics bypass.

Deployment admission verifies the complete versioned runtime index, selected
profile/pack hash binding, cardinality, roles, known decoder/execution contracts,
local FP32 eligibility, file containment and declared hashes. Model tensor
dimensions and other detailed semantic contracts remain validated by native
initialization; this editor check does not duplicate the entire native parser.

## Evidence

Evidence root: `.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/android-build-contract-correction/`.

- `red-upm2/tests.xml`: original guard fails the approved closure, 0/1.
- `baseline-topdown/tests.xml`: original TopDown route passes, 1/1.
- `green-upm2/tests.xml`, `green-canonical2/tests.xml`: real Unity 30/30 each,
  zero skips, including 26 malformed-closure cases.
- `regression-upm/tests.xml`, `regression-canonical/tests.xml`: existing Android
  settings/build validation 19/19 each, zero skips.
- `managed-upm.log`, `managed-canonical.log`: Windows and Android conditional
  assembly compilation passes. Architecture and catalog Python checks exit0.
- `independent-review.md`: independent spec compliance PASS and code quality PASS;
  exact six source hashes match reviewed frozen sources.
- Root `root-stage.py`: verifies six reviewed sources, all421 immutable package
  files, exact four-file delta from v12 and all11 installed runtime files.

The current open user project imported immutable local v13. Its real installed
selected model admission passes. The live environment receipt is
`production-correction-user-integration-v13/actual-installed-model-admission.txt`
under the same ledger: Vulkan-first/manual, ARM64 and IL2CPP are enabled;
minimum API remains24. This is model admission proof, not full Android build
validation: the full NCNN build requires API26. A separate formal project will
use API26 and preserve the current user's scene and recognition settings.

The user's ordinary test run without external approved fixtures skipped all30
new tests (`tests-no-fixture-skipped.json`); it is not PASS evidence. The isolated
green runs above stage the real fixtures and skip none. Earlier long-path
fixture failures and nonshipping probe mistakes remain retained and superseded.

## Limits

This bounded prerequisite does not establish APK success or physical Camera,
Video or RTSP skeleton acceptance. Task11 and 30 fresh complete observation
frames/s remain open. No main merge, release or distribution qualification.
