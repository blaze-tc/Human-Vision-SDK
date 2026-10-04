# Observation HUD metadata import correction

2026-10-04. Follow-up to HUD commit `3f48a8c`. The actual fresh v14 Android
build succeeded, but its full import log revealed YAML parser fallback for the
two new `.meta` files and mixed line endings in the new test script. BuildReport
warnings zero did not establish a clean import. Original v14 evidence and APK
remain preserved under `out/input/formal-android-oct04-v14-observer/`.

Four canonical/UPM metadata files now use consistent LF with a terminating
newline; both test copies only normalize line endings. All content lines,
GUIDs and normalized C# bytes are unchanged. No HUD logic, inference, API,
renderer, model or native library changes. Six disk files were corrected;
the canonical test was already normalized in Git, leaving five source paths
with a committed diff.

Evidence ledger:
`.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/demo-rate-metadata-correction/`.
Fresh implementer and independent reviewer checked the exact preimages,
normalized-byte equality, three canonical/UPM pairs and final six-file freeze.

Fresh actual Unity import/test project:
`out/input/production-correction/demo-rate-meta-green-project/`.
Both scripts import with their original GUIDs and no target YAML parser,
GUID extraction or mixed-line-ending diagnostic. Actual EditMode XML reports
**11/11 PASS, zero failed/skipped**, OS process exit 0. Other preexisting
package scripts still have mixed-line-ending messages; the whole log is not
claimed warning-free.

Qualified local v15 composition has 425 files and exactly four differences
from immutable v14: two metadata files, the test source's newline bytes, and
the regenerated SDK asset index. Input, models, runtime index and native
libraries are unchanged. This local composition is not a published Git package.

New isolated actual Android build:
`out/input/formal-android-oct04-v15-observer/player/HumanVisionFormal.apk`.
BuildReport Succeeded, zero errors/warnings; OS exit 0 captured. APK 82,971,249
bytes, SHA256 `36b6b189e70f608d767a7f891bcd1aa0a3209e0aae14d873efb399552997a0b9`.
Target metadata/test imports are clean in this Android log too. All 425
composition hashes and 14 runtime/native ZIP entries were checked; those 14
entries equal v14 byte-for-byte. Runtime index:
`89703ad345f2b7a954b6711f8e65ffbd02e5f80cbf1c653d6815a53395ee3cfc`.
Official three scenes and unchanged additive observation recorder are retained;
API26, ARM64 IL2CPP and Vulkan-only use `android-ncnn-vulkan`.

Actual installation on `e7c07019` succeeded. The pulled installed `base.apk`
matches the built APK hash and size. The first appearance capture was black.
After the wake attempt, saved power state still reports Asleep and the saved
keyguard state reports showing=true; a visible, awake app was not established.
This attempt does not qualify visible video, skeletons or HUD; no unlock bypass
was attempted. The app was force-stopped. `installed-identity.json`,
`device-attempt.json` and the original capture retain this boundary.

The actual user Unity project resolves SDK v15 plus Input v13 and runs real
Video1 in the same clean scene with four enabled regions/capacity four. Scene
bytes match the backup. The post-Play console captured one UnitySkills HTTP
server `Thread was being aborted` fallback error during domain reload; this
control-layer failure is retained, and a whole-console-zero claim is not made.
This import/functional evidence does not certify throughput or physical motion.

The prior Android measurements remain 15.675/15.9 complete manager results/s,
**30 FPS FAIL**; native coverage remains INCONCLUSIVE. Camera participation
and actual RTSP device runs remain pending in the approved Video -> Camera ->
RTSP sequence. No main merge, Release or final Task11 acceptance.
