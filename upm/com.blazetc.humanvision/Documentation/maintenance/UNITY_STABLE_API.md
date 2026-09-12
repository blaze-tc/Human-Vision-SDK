# Stable Unity integration

Authoritative source: unity/HumanVisionDemo/Assets/HumanVision. Generated Git UPM
copy: upm/com.blazetc.humanvision. Never edit only the generated copy.

V1 public signatures are frozen in tests/contracts/managed_v1_public_surface.json;
C layouts and symbols have native contract tests. HumanVisionManager initialization,
submission, Bodies, Stats, ResultUpdated and runtime regions remain available.
HumanVisionCameraManager retains GetColorImageTex, GetUsersCount, region lookup,
GetUserIdByIndex and the original joint enum/getters. Existing script GUIDs remain.
HumanVisionJoint.Count remains17; legacy body/hand arrays remain17+6.

New initialization uses HumanVisionConfig.RuntimeRoot and Profile after
HumanVisionRuntimeData.Prepare. Old DetectorModelPath/PoseModelPath fields remain
deprecated compatibility inputs; new gameplay must not use them. New code may read
CanonicalJoints (32 semantic IDs), StableTrackId, RegionIndex and observation times.
SampledBodies are separate from raw Bodies. Joint prediction carries its horizon
and original timestamp. Invalid/old hand points must not be treated as current.

All returned arrays are reused: consume them on the Unity main thread, copy if
retaining a historical snapshot. ResultUpdated denotes new native observations;
SampledBodies update per rendering frame. Positions are an RGB image plane, not
metric depth. Model filenames, native schema indices and provider classes must not
become gameplay dependencies. RuntimeDiagnostics is descriptive text only.

Removing/renaming a public type/member, changing enum values, Count, array ordering,
native layout, GUID or timestamp meaning is breaking. Add semantic members instead.
Run check_public_surface.py after regenerating UPM and compile_managed.ps1, followed
by managed regression/ABI tests where applicable.
