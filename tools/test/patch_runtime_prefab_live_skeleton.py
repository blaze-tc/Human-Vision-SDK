"""Patch native binding and configured sampled-joint display in the private Demo.

This never replaces the private Demo with the public sample. Apply to the
existing RuntimePrefabLiveSkeleton.cs, preserving its encoding and line endings.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


OLD_GATE = """            // A new source/generation needs one corroborating analysis frame.
            // A borrowed latest texture can pause while an accepted pose remains valid.
            if (!bindingConfirmed) {
                bindingConfirmed = sdk.TryGetAnalysisInputFrame(-1, out var analysis) &&
                    analysis.SourceId == preview.SourceId && analysis.Generation == preview.Generation &&
                    analysis.AppliedRotationDegrees == preview.AppliedRotationDegrees &&
                    analysis.AppliedMirrorX == preview.AppliedMirrorX;
                if (!bindingConfirmed) return;
            }
"""
NEW_GATE = """            // Native analysis has no borrowed Unity texture. Its coherent identity
            // confirms this live binding; it need not be the latest preview frame.
            if (sdk.TryGetRuntimeProgress(out var native) && native.ExecutionMode == "native") {
                if (!native.Streaming || native.NativeFreshFrames == 0 || native.FrameId <= 0 ||
                    native.ConfigEpoch == 0 || native.SourceId != preview.SourceId ||
                    native.Generation != preview.Generation) {
                    Expire(now);
                    return;
                }
                if (nativeEpoch != 0 && nativeEpoch != native.ConfigEpoch) ClearGeometry();
                nativeEpoch = native.ConfigEpoch;
                bindingConfirmed = true;
            } else if (nativeEpoch != 0) {
                // A failed identity query must not prolong previously drawn geometry.
                Expire(now);
                return;
            } else if (!bindingConfirmed) {
                // Compatible sources retain their original analysis-frame check.
                bindingConfirmed = sdk.TryGetAnalysisInputFrame(-1, out var analysis) &&
                    analysis.SourceId == preview.SourceId && analysis.Generation == preview.Generation &&
                    analysis.AppliedRotationDegrees == preview.AppliedRotationDegrees &&
                    analysis.AppliedMirrorX == preview.AppliedMirrorX;
                if (!bindingConfirmed) return;
            }
"""
FIELD = "        private bool bindingConfirmed;"
PATCHED_FIELD = FIELD + "\n        private ulong nativeEpoch;"
RESET = "            bindingConfirmed = false;\n            ClearGeometry();"
PATCHED_RESET = "            bindingConfirmed = false;\n            nativeEpoch = 0;\n            ClearGeometry();"
FILTER_FIELDS = """        private HumanVisionSkeletonQueries displayQueries;
        private readonly HumanVisionBody[] sampledBodies = new HumanVisionBody[Capacity];
        private readonly double[,] expiresUs = new double[Capacity, JointCount];
        private int configuredCapacity;
        private float configuredConfidence, bodyAgeMilliseconds, handAgeMilliseconds;
"""
CONFIGURE = """        internal void Configure(HumanVisionSdkConfiguration configuration)
        {
            if (configuration == null) throw new System.ArgumentNullException(nameof(configuration));
            if (displayQueries != null && configuredCapacity == configuration.MaxBodies &&
                configuredConfidence == configuration.MinimumJointConfidence &&
                bodyAgeMilliseconds == configuration.MaximumResultAgeMilliseconds &&
                handAgeMilliseconds == configuration.MaximumHandAgeMilliseconds) return;
            // The SDK query owns one validated copy, only when these settings change.
            var replacement = new HumanVisionSkeletonQueries(configuration);
            displayQueries = replacement;
            configuredCapacity = configuration.MaxBodies;
            configuredConfidence = configuration.MinimumJointConfidence;
            bodyAgeMilliseconds = configuration.MaximumResultAgeMilliseconds;
            handAgeMilliseconds = configuration.MaximumHandAgeMilliseconds;
            ClearGeometry();
        }

"""
SAMPLE_START = "        internal void Sample("
CLEAR_START = "        internal void Clear()"
V1_SAMPLE_SHA256 = "f5353178a0bb488eb580c6d183db16e9ccac89574cf02f0aba7c5dbfcd3c75b3"
FILTERED_SAMPLE = """        internal void Sample(HumanVisionSdk sdk, in HumanVisionTextureFrame preview)
        {
            if (sdk == null || sdk.InputState != InputSourceState.Streaming) {
                Clear();
                return;
            }
            var nextBinding = (preview.SourceId, preview.Generation, preview.Width, preview.Height,
                preview.AppliedRotationDegrees, preview.AppliedMirrorX, preview.RowOrigin);
            if (nextBinding != binding) {
                Clear();
                binding = nextBinding;
            }

            double now = Time.realtimeSinceStartupAsDouble;
            if (!sdk.HasFreshResult) {
                ClearGeometry();
                return;
            }
            // Recover the SDK's observation clock; Unity time is only the display hold clock.
            double queryNowUs = sdk.SourceTimestampUs + sdk.ResultAgeMilliseconds * 1000d;
            if (double.IsNaN(queryNowUs) || double.IsInfinity(queryNowUs) || queryNowUs < 0 || queryNowUs >= long.MaxValue) {
                ClearGeometry();
                return;
            }
            long nowUs = (long)System.Math.Round(queryNowUs);
""" + NEW_GATE.replace("Expire(now)", "Expire(now, nowUs)") + """            if (sdk.GetUsersCount() == 0) {
                ClearGeometry();
                return;
            }
            // Legacy callers that do not Configure get one settings snapshot, not a clone per frame.
            if (displayQueries == null) Configure(sdk.Configuration.Recognition);
            int capacity = System.Math.Min(Capacity, System.Math.Min(configuredCapacity, sdk.GetMaxBodies()));
            System.Array.Clear(sampledBodies, 0, sampledBodies.Length);
            int sampledCount = 0;
            for (int slot = 0; slot < capacity; slot++) {
                if (sdk.TryGetSampledBodyByIndex(slot, out var body)) {
                    sampledBodies[slot] = body;
                    ++sampledCount;
                }
            }
            // Filtering uses the real sampled points and the existing SDK rules;
            // it does not replace smooth coordinates or create an observation sequence.
            displayQueries.Clear();
            if (!displayQueries.Observe(sampledBodies, capacity, sdk.ResultSequence, sdk.SourceFrameId, sdk.SourceTimestampUs, true)) {
                Expire(now, nowUs);
                return;
            }
            bool foundGeometry = false;
            for (int slot = 0; slot < capacity; slot++) {
                var body = sampledBodies[slot];
                if (body == null) continue;
                int usableJoints = 0;
                for (int joint = 0; joint < JointCount; joint++) {
                    bool accepted = displayQueries.TryGetJoint(slot, (HumanVisionCanonicalJointId)joint, nowUs, out var point);
                    Vector2 uv = point.Position.Normalized;
                    newValid[joint] = accepted && uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1;
                    newPoints[joint] = uv;
                    if (!newValid[joint]) continue;
                    bool hand = joint >= 8 && joint <= 10 || joint >= 15 && joint <= 17;
                    double jointLimit = hand ? handAgeMilliseconds : bodyAgeMilliseconds;
                    expiresUs[slot, joint] = System.Math.Min(body.ObservationTimestampUs + bodyAgeMilliseconds * 1000d,
                        point.ObservationTimestampUs + jointLimit * 1000d);
                    ++usableJoints;
                }
                if (usableJoints == 0) continue;
                if (!foundGeometry) {
                    ClearGeometry();
                    foundGeometry = true;
                }
                visible[slot] = true;
                ++VisibleBodies;
                VisibleJoints += usableJoints;
                for (int joint = 0; joint < JointCount; joint++) {
                    points[slot, joint] = newPoints[joint];
                    valid[slot, joint] = newValid[joint];
                }
                for (int joint = 0; joint < JointCount; joint++) {
                    int parent = Parents[joint];
                    if (parent >= 0 && valid[slot, joint] && valid[slot, parent] &&
                        points[slot, joint] != points[slot, parent]) ++VisibleBones;
                }
            }
            if (foundGeometry) lastVisibleAt = now;
            else if (sampledCount > 0) ClearGeometry(); // Explicit filter rejection cannot extend old points.
            else Expire(now, nowUs);
        }


"""
OLD_EXPIRE = """        private void Expire(double now)
        {
            if (now - lastVisibleAt >= PoseHoldSeconds) ClearGeometry();
        }
"""
FILTERED_EXPIRE = """        private void Expire(double now, long nowUs)
        {
            if (now - lastVisibleAt >= PoseHoldSeconds) {
                ClearGeometry();
                return;
            }
            bool changed = false;
            for (int slot = 0; slot < Capacity; slot++) {
                if (!visible[slot]) continue;
                for (int joint = 0; joint < JointCount; joint++) {
                    if (valid[slot, joint] && nowUs > expiresUs[slot, joint]) {
                        valid[slot, joint] = false;
                        changed = true;
                    }
                }
            }
            if (!changed) return;
            VisibleBodies = VisibleJoints = VisibleBones = 0;
            for (int slot = 0; slot < Capacity; slot++) {
                if (!visible[slot]) continue;
                int count = 0;
                for (int joint = 0; joint < JointCount; joint++) {
                    if (!valid[slot, joint]) continue;
                    ++count;
                    int parent = Parents[joint];
                    if (parent >= 0 && valid[slot, parent] && points[slot, joint] != points[slot, parent]) ++VisibleBones;
                }
                VisibleJoints += count;
                visible[slot] = count > 0;
                if (visible[slot]) ++VisibleBodies;
            }
        }
"""
FILTERED_RESET = "            bindingConfirmed = false;\n            nativeEpoch = 0;\n            displayQueries?.Clear();\n            System.Array.Clear(sampledBodies, 0, sampledBodies.Length);\n            ClearGeometry();"


def patch_live_skeleton(text):
    newline = "\r\n" if "\r\n" in text else "\n"
    value = text.replace("\r\n", "\n")
    if "private HumanVisionSkeletonQueries displayQueries;" in value:
        if any(part not in value for part in (FILTER_FIELDS, CONFIGURE, FILTERED_SAMPLE, FILTERED_EXPIRE, FILTERED_RESET)):
            raise ValueError("Refusing to modify a partially changed configured display filter")
        return text
    if "private ulong nativeEpoch;" in value:
        if NEW_GATE not in value or PATCHED_FIELD not in value or PATCHED_RESET not in value:
            raise ValueError("Refusing to modify a partially changed native skeleton binding")
    else:
        for original, replacement in ((FIELD, PATCHED_FIELD), (OLD_GATE, NEW_GATE), (RESET, PATCHED_RESET)):
            if value.count(original) != 1:
                raise ValueError("Expected exactly one original skeleton binding section")
            value = value.replace(original, replacement, 1)
    start = value.index(SAMPLE_START)
    end = value.index(CLEAR_START, start)
    if hashlib.sha256(value[start:end].encode()).hexdigest() != V1_SAMPLE_SHA256:
        raise ValueError("Private Sample method changed; review it before applying the display filter")
    value = value[:start] + CONFIGURE + FILTERED_SAMPLE + value[end:]
    field_marker = "        private double lastVisibleAt = double.NegativeInfinity;\n"
    for original, replacement in ((field_marker, field_marker + FILTER_FIELDS), (OLD_EXPIRE, FILTERED_EXPIRE), (PATCHED_RESET, FILTERED_RESET)):
        if value.count(original) != 1:
            raise ValueError("Expected exactly one original configured-filter section")
        value = value.replace(original, replacement, 1)
    return value.replace("\n", newline)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--backup", type=Path, help="Backup file path; required when applying a change")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    target = args.project.resolve() / "Assets/HumanVision/Samples/RuntimePrefab/RuntimePrefabLiveSkeleton.cs"
    original = target.read_bytes()
    text = original.decode("utf-8-sig")
    payload = patch_live_skeleton(text).encode("utf-8")
    if original.startswith(b"\xef\xbb\xbf"):
        payload = b"\xef\xbb\xbf" + payload
    changed = original != payload
    if args.apply and changed:
        if args.backup is None:
            parser.error("--backup is required before applying a change")
        if args.backup.exists() and args.backup.read_bytes() != original:
            raise ValueError("Refusing to overwrite a different original-file backup")
        args.backup.parent.mkdir(exist_ok=True, parents=True)
        if not args.backup.exists():
            shutil.copy2(target, args.backup)
        target.write_bytes(payload)
    print(json.dumps({"path": str(target), "applied": args.apply, "changed": changed,
        "before_sha256": hashlib.sha256(original).hexdigest(),
        "sha256": hashlib.sha256(payload).hexdigest()}, indent=2))


if __name__ == "__main__":
    main()
