"""Analyze additive production-Demo recorder JSONL, never reconstruct missed bodies.

Public v13 counters cannot establish native V2 fresh-frame or GPU error/readback
coverage. A valid manager observation measurement is therefore not physical SDK
acceptance. Source/PTS clocks are kept separate from sensor capture time.
"""
import argparse
import collections
import json
import math
import pathlib
import re

QUANTIZATION_US = 2
MAX_INTERVAL_ERROR_US = 5000
INT32_MAX = 2**31 - 1
INT64_MAX = 2**63 - 1
UINT64_MAX = 2**64 - 1
FLOAT32_MAX = 3.4028234663852886e38


def percentile(values, fraction):
    if not values:
        return None
    values = sorted(values)
    index = (len(values) - 1) * fraction
    lower = int(index)
    return values[lower] + (values[min(lower + 1, len(values) - 1)] - values[lower]) * (index - lower)


def analyze(manifest, records):
    try:
        return _analyze(manifest, records)
    except (KeyError, TypeError, ValueError, OverflowError) as error:
        # Malformed artifacts fail closed; no partial arithmetic is accepted.
        result = _analyze({}, [])
        result["invalid_reasons"] = ["malformed artifact field types: " + str(error)]
        return result


def _analyze(manifest, records):
    reasons = []

    def reject(reason):
        if reason not in reasons:
            reasons.append(reason)

    def finite(value):
        return isinstance(value, (float, int)) and not isinstance(value, bool) and math.isfinite(value)

    def integer(row, field, minimum=0, maximum=INT64_MAX):
        value = row.get(field)
        if type(value) is not int or not minimum <= value <= maximum:
            reject("missing/invalid typed integer " + field)
            return False
        return True

    def integers(row, fields):
        # Evaluate every field for diagnostics; never replace invalid values with
        # zero in joins, timestamps, percentile inputs or retained arithmetic.
        checks = [integer(row, field, *bounds) for field, bounds in fields.items()]
        return all(checks)

    def booleans(row, fields):
        checks = [type(row.get(field)) is bool for field in fields]
        if not all(checks):
            reject("missing/invalid boolean provenance")
        return all(checks)

    def floats(row, fields):
        checks = [finite(row.get(field)) and abs(row[field]) <= FLOAT32_MAX for field in fields]
        if not all(checks):
            reject("nonfinite/missing/out-of-range float data")
        return all(checks)

    long = (0, INT64_MAX)
    int32 = (0, INT32_MAX)
    ulong = (0, UINT64_MAX)

    for key in ("apk_sha256", "native_sha256", "model_index_sha256", "package_index_sha256",
                "recorder_sha256", "source_evidence_sha256"):
        if not isinstance(manifest.get(key), str) or not re.fullmatch(r"[0-9a-fA-F]{64}", manifest[key]):
            reject("missing frozen identity " + key)
    if not re.fullmatch(r"[0-9a-fA-F]{7,40}", str(manifest.get("commit", ""))):
        reject("missing commit identity")
    grouped = collections.defaultdict(list)
    for row in records:
        if not isinstance(row, dict):
            reject("invalid JSONL record")
        else:
            if row.get("kind") not in ("header", "footer", "source", "observation", "sample"):
                reject("unknown JSONL record kind")
            else:
                grouped[row["kind"]].append(row)
    if len(grouped["header"]) != 1 or len(grouped["footer"]) != 1:
        reject("require exactly one header/footer")
    header = grouped["header"][0] if grouped["header"] else {}
    footer = grouped["footer"][0] if grouped["footer"] else {}
    if not records or records[0] != header or records[-1] != footer:
        reject("header/footer record order")
    header_valid = integers(header, {"schema": int32, "max_bodies": (1, INT32_MAX),
        "start_unity_us": long, "duration_us": long, "warmup_us": long, "window_us": long,
        "begin_sequence": long, "begin_source_frame": long})
    header_valid = booleans(header, ("gpu", "runtime")) and header_valid
    if header.get("schema") != 1 or not isinstance(header.get("run_id"), str) or not header.get("run_id"):
        reject("missing schema/run identity")
        header_valid = False
    if header.get("profile") != manifest.get("profile") or not header.get("profile"):
        reject("profile identity mismatch")
        header_valid = False
    if header.get("gpu") is not True or header.get("runtime") is not True:
        reject("public-only CPU frame/clock mapping is unavailable; qualified GPU runtime required")
        header_valid = False
    for field, expected in (("duration_us", 60000000), ("warmup_us", 10000000), ("window_us", 40000000)):
        if header.get(field) != expected:
            reject("fixed 60s run/10s warmup/40s window required")
            header_valid = False
    footer_valid = integers(footer, {"elapsed_us": long, "end_sequence": long, "end_source_frame": long,
        "hand_only_events": long, "observation_count": int32, "source_count": int32, "sample_count": int32})
    footer_valid = booleans(footer, ("overflow", "interrupted", "copy_invalid")) and footer_valid
    if not footer_valid or footer["elapsed_us"] < 60000000:
        reject("insufficient complete 60s recording")
    for field in ("overflow", "interrupted", "copy_invalid"):
        if footer.get(field) is not False:
            reject("recorder " + field)
    start = header["start_unity_us"] if header_valid else None
    body_capacity = header["max_bodies"] if header_valid else None
    observations, sources, samples = grouped["observation"], grouped["source"], grouped["sample"]
    for field, rows in (("observation_count", observations), ("source_count", sources), ("sample_count", samples)):
        if footer.get(field) != len(rows):
            reject("footer retained count mismatch " + field)

    source_index = {}
    previous_source = header["begin_source_frame"] if header_valid else None
    identity = provenance = None
    previous_publication = previous_source_elapsed = None
    previous_source_times = {}
    ambiguous_source = False
    for row in sources:
        source_ok = integers(row, {"source_id": (1, UINT64_MAX), "generation": (1, UINT64_MAX),
            "frame_id": long, "elapsed_us": long, "published_us": long, "source_us": long,
            "pts_us": (-2**63, INT64_MAX), "source_clock_id": ulong, "resource_token": ulong,
            "source_clock": (1, 2), "timestamp_kind": (0, 1), "width": (1, INT32_MAX),
            "height": (1, INT32_MAX), "rotation": (-2**31, INT32_MAX), "row_origin": (0, 1), "color_space": (0, 2)})
        source_ok = booleans(row, ("mirror",)) and source_ok
        if "baseline" in row:
            source_ok = booleans(row, ("baseline",)) and source_ok
        frame_valid = integer(row, "frame_id")
        frame_id = row["frame_id"] if frame_valid else None
        if frame_valid and frame_id in source_index:
            reject("duplicate source frame")
            ambiguous_source = True
            source_ok = False
        if row.get("baseline") is True and frame_valid:
            if source_index or frame_id != previous_source:
                reject("invalid baseline source")
                source_ok = False
        elif frame_valid and previous_source is not None and frame_id != previous_source + 1:
            reject("source publication sequence gap/reset")
            if frame_id <= previous_source:
                source_ok = False
                ambiguous_source = True
        if frame_valid:
            previous_source = frame_id
        # Only typed, internally valid clock metadata may establish provenance.
        if source_ok and row["source_clock"] == 1:
            if row["source_clock_id"] != 0 or row["source_us"] > row["published_us"]:
                reject("invalid InputMonotonic source clock")
                source_ok = False
        elif source_ok and (row["source_clock_id"] == 0 or row["timestamp_kind"] != 1):
            reject("invalid unmapped local-decode source clock")
            source_ok = False
        if source_ok:
            current_identity = (row["source_id"], row["generation"])
            current_provenance = (row["source_clock"], row["timestamp_kind"], row["source_clock_id"])
            if identity is not None and identity != current_identity:
                reject("source generation/identity changed during measurement")
                ambiguous_source = True
                source_ok = False
            if provenance is not None and provenance != current_provenance:
                reject("source clock domain/kind/origin changed during measurement")
                ambiguous_source = True
                source_ok = False
            if identity is None:
                identity, provenance = current_identity, current_provenance
            if previous_publication is not None and row["published_us"] < previous_publication:
                reject("source publication clock regression")
                source_ok = False
            if previous_source_elapsed is not None and row["elapsed_us"] < previous_source_elapsed:
                reject("source metadata observation time regression")
                source_ok = False
            origin = (row["source_clock"], row["source_clock_id"])
            if origin in previous_source_times and row["source_us"] < previous_source_times[origin]:
                reject("source observation clock regression within declared origin")
                source_ok = False
            if footer_valid and row["elapsed_us"] > footer["elapsed_us"]:
                reject("source metadata observation outside run")
                source_ok = False
            if source_ok:
                previous_publication = row["published_us"]
                previous_source_elapsed = row["elapsed_us"]
                previous_source_times[origin] = row["source_us"]
        if frame_valid:
            source_index[frame_id] = (row, source_ok)
    if previous_source != footer.get("end_source_frame"):
        reject("missing source suffix coverage")

    previous_sequence = header["begin_sequence"] if header_valid else None
    previous_frame = -1
    previous_elapsed = -1
    previous_input_now = mapping_offset_twice = None
    window, retained = [], []
    publication_ages, observation_ages, manager_ages, clock_errors = [], [], [], []
    for row in observations:
        # A complete raw manager delivery is independently observable even when
        # its source join or adjacent clock pair cannot qualify an age. Count
        # only typed event identities and correctly placed retained raw frames.
        delivery_ok = integers(row, {"elapsed_us": long, "sequence": (1, INT64_MAX),
            "source_frame_id": long, "unity_before_us": long, "processed_sequence": long})
        delivery_ok = delivery_ok and header_valid and footer_valid
        if delivery_ok and abs(row["unity_before_us"]-start-row["elapsed_us"]) > QUANTIZATION_US:
            reject("raw delivery event time/elapsed mapping mismatch")
            delivery_ok = False
        observation_ok = integers(row, {"elapsed_us": long, "sequence": (1, INT64_MAX),
            "source_frame_id": long, "source_timestamp_us": long, "unity_before_us": long,
            "unity_after_us": long, "input_now_us": long, "body_count": int32,
            "submitted": long, "processed_sequence": long, "runtime_drops": long})
        for field in ("region_revision",):
            if field in row:
                observation_ok = integer(row, field) and observation_ok
        if "region_assignments_available" in row:
            observation_ok = booleans(row, ("region_assignments_available",)) and observation_ok
        for field in ("inference_fps", "detection_ms", "pose_ms", "tracking_ms", "total_ms"):
            if field in row:
                observation_ok = floats(row, (field,)) and observation_ok
        sequence_valid = integer(row, "sequence", 1)
        sequence = row["sequence"] if sequence_valid else None
        if sequence_valid and previous_sequence is not None and sequence != previous_sequence + 1:
            reject("native result sequence gap/reset/duplicate; unknown bodies cannot be reconstructed")
        if sequence_valid:
            previous_sequence = sequence
        frame_valid = integer(row, "source_frame_id")
        frame_id = row["source_frame_id"] if frame_valid else None
        if frame_valid and frame_id <= previous_frame:
            reject("duplicate/regressing result source frame")
            observation_ok = False
            delivery_ok = False
        if frame_valid:
            previous_frame = frame_id
        elapsed_valid = integer(row, "elapsed_us")
        elapsed = row["elapsed_us"] if elapsed_valid else None
        if elapsed_valid and (elapsed < previous_elapsed or footer_valid and elapsed > footer["elapsed_us"]):
            reject("result observation time regression/outside run")
            observation_ok = False
            delivery_ok = False
        if elapsed_valid:
            previous_elapsed = elapsed
        count_valid = integer(row, "body_count", 0, INT32_MAX)
        count = row["body_count"] if count_valid else None
        body_complete = count_valid
        bodies = row.get("bodies")
        if not isinstance(bodies, list) or len(bodies) != count or body_capacity is None or count is None or count > body_capacity:
            reject("incomplete raw body snapshot")
            bodies = []
            body_complete = False
        for body in bodies:
            if not isinstance(body, dict):
                reject("invalid raw body")
                body_complete = False
                continue
            body_complete = integers(body, {"track_id": int32, "stable_track_id": long,
                "region_index": (-1, INT32_MAX), "observation_us": long}) and body_complete
            body_complete = floats(body, ("confidence", "x", "y", "width", "height")) and body_complete
            for field, length in (("canonical_joints", 32), ("joints", 17), ("hand_joints", 6)):
                joints = body.get(field)
                if not isinstance(joints, list) or len(joints) != length:
                    reject("incomplete full raw joints")
                    body_complete = False
                    continue
                for joint in joints:
                    if not isinstance(joint, dict):
                        reject("invalid joint")
                        body_complete = False
                        continue
                    body_complete = floats(joint, ("px", "py", "nx", "ny", "confidence", "prediction_ms")) and body_complete
                    body_complete = integer(joint, "observation_us") and body_complete
                    body_complete = booleans(joint, ("valid", "derived")) and body_complete
                    if joint.get("prediction_ms") != 0:
                        reject("predicted/sampled joints cannot be fresh observations")
                        body_complete = False
        clock_ok = observation_ok and header_valid and footer_valid
        if clock_ok:
            before, after = row["unity_before_us"], row["unity_after_us"]
            input_now, timestamp = row["input_now_us"], row["source_timestamp_us"]
            error = after - before + QUANTIZATION_US
            clock_errors.append(error)
            if before > after or error > MAX_INTERVAL_ERROR_US or abs(before-start-elapsed) > QUANTIZATION_US:
                reject("clock mapping interval/order exceeds 5ms bound")
                clock_ok = False
            if timestamp > after:
                reject("future manager source timestamp")
                clock_ok = False
            if previous_input_now is not None and input_now < previous_input_now:
                reject("InputMonotonic observation clock regression")
                clock_ok = False
            # Unity/Input monotonic epochs survive one fixed process/run. Keep
            # subtraction integer-exact and exclude mapping jumps over5ms; an
            # invalid row may not reset the mapping anchor for later rows.
            current_offset_twice = 2*(before-input_now)+(after-before)
            if mapping_offset_twice is not None and abs(current_offset_twice-mapping_offset_twice) > 2*MAX_INTERVAL_ERROR_US:
                reject("Unity/Input clock epoch mapping changed over5ms")
                clock_ok = False
            if clock_ok:
                previous_input_now = input_now
                if mapping_offset_twice is None:
                    mapping_offset_twice = current_offset_twice
        joined_source = source_index.get(frame_id)
        source, source_ok = joined_source if joined_source is not None else (None, False)
        if source is None:
            reject("missing exact source metadata join")
        elif clock_ok and source_ok and input_now < source["published_us"]:
            reject("future source publication")
            source_ok = False
        elif clock_ok and source_ok:
            # Frozen InputTimestampMapping.Map rejects an age before the Unity
            # epoch. A representable long does not alone qualify this mapping.
            publication_age = input_now-source["published_us"]
            source_age = input_now-source["source_us"] if source["source_clock"] == 1 else None
            if publication_age > after+QUANTIZATION_US or source_age is not None and source_age > after+QUANTIZATION_US:
                reject("source observation/publication age precedes Unity clock epoch")
                source_ok = False
        if row.get("processed_sequence") != sequence:
            reject("public processed-sequence semantics mismatch")
            observation_ok = False
            delivery_ok = False
        if elapsed_valid and 10000000 <= elapsed < 50000000:
            window.append(row)
            if delivery_ok and body_complete:
                retained.append(row)
            age_eligible = observation_ok and body_complete and clock_ok and source_ok
            if age_eligible:
                publication_ages.append((input_now-source["published_us"])/1000.)
                if source["source_clock"] == 1:
                    observation_ages.append((input_now-source["source_us"])/1000.)
                # Subtract integer timestamps before conversion to float; large
                # but representable long epochs must not lose sub-ms age precision.
                manager_ages.append(((before-timestamp)+(after-before)/2.)/1000.)
    if previous_sequence != footer.get("end_sequence"):
        reject("missing native result suffix coverage")

    previous_counters = None
    for row in samples:
        fields = ("elapsed_us", "sequence", "submitted", "processed_sequence", "runtime_drops",
                  "bridge_drops", "bridge_copies", "cpu_readbacks")
        sample_ok = integers(row, {field: long for field in fields})
        sample_ok = integer(row, "source_state", 0, 5) and sample_ok
        sample_ok = booleans(row, ("error",)) and sample_ok
        if "readback_errors" in row:
            sample_ok = integer(row, "readback_errors") and sample_ok
            if sample_ok and row["readback_errors"] > 0:
                reject("positive CPU readback errors independent of summary flag")
        if not sample_ok:
            continue
        current = tuple(row[field] for field in fields)
        if previous_counters and any(a < b for a, b in zip(current, previous_counters)):
            reject("counter reset/regression")
        previous_counters = current
        if row.get("error") is not False or row.get("source_state") != 2:
            reject("production source/manager/adapter error or non-streaming state")
        if row.get("cpu_readbacks") != 0:
            reject("CPU full-frame readbacks")
    if not samples or samples[0].get("elapsed_us") != 0 or not integer(samples[-1], "elapsed_us") or samples[-1]["elapsed_us"] < 60000000:
        reject("missing start/end production counter samples")
    if samples and (samples[0].get("sequence") != header.get("begin_sequence") or
                    samples[-1].get("sequence") != footer.get("end_sequence")):
        reject("boundary counter/result sequence mismatch")

    valid = not reasons
    rate = len(window)/40. if valid else None
    source_rows = [r for r in sources if r.get("baseline") is not True and
                   10000000 <= r.get("elapsed_us", -1) < 50000000]
    # A numeric source limit can only disqualify 30FPS, never prove native coverage.
    source_limit = manifest.get("source_rate_hz")
    if source_limit is not None and (not finite(source_limit) or source_limit <= 0):
        reject("invalid frozen source rate")
        valid = False
        rate = None
    limited = finite(source_limit) and source_limit < 30
    histogram = dict(sorted(collections.Counter(str(r.get("body_count")) for r in window).items()))

    def ages(values):
        return {"p50": percentile(values, .5) if valid else None,
                "p95": percentile(values, .95) if valid else None, "count": len(values)}

    def retained_ages(values):
        return {"p50": percentile(values, .5) if not ambiguous_source else None,
                "p95": percentile(values, .95) if not ambiguous_source else None,
                "count": 0 if ambiguous_source else len(values)}

    def delta(field):
        return samples[-1][field]-samples[0][field] if valid else None

    unique_sequences, unique_frames = set(), set()
    for row in retained:
        if row["sequence"] not in unique_sequences and row["source_frame_id"] not in unique_frames:
            unique_sequences.add(row["sequence"])
            unique_frames.add(row["source_frame_id"])
    native_invalid = any("native result" in reason or "counter reset" in reason for reason in reasons)
    return dict(manager_observation_validity="VALID" if valid else "INVALID", invalid_reasons=reasons,
                native_coverage_validity="INVALID" if native_invalid else "INCONCLUSIVE", native_fresh_body_frames=None,
                window_observations=len(window), manager_complete_observation_fps=rate,
                retained_manager_delivery_fps=len(unique_sequences)/40. if header_valid and footer_valid and footer["elapsed_us"] >= 60000000 else None,
                observed_source_publication_fps=len(source_rows)/40. if valid else None,
                body_count_histogram=histogram, zero_body_frames=histogram.get("0", 0),
                target_30_fps="INVALID" if not valid else "FAIL" if rate < 30 or limited else "INCONCLUSIVE",
                source_cannot_establish_30_fps=limited, publication_age_ms=ages(publication_ages),
                source_observation_age_ms=ages(observation_ages), manager_source_age_ms=ages(manager_ages),
                retained_publication_age_ms=retained_ages(publication_ages),
                retained_source_observation_age_ms=retained_ages(observation_ages),
                retained_manager_source_age_ms=retained_ages(manager_ages),
                max_clock_interval_error_us=max(clock_errors, default=None),
                public_runtime_drops=delta("runtime_drops"), public_bridge_drops=delta("bridge_drops"),
                public_cpu_readbacks=delta("cpu_readbacks"), decoder_drops=None,
                gpu_copy_errors=None, gpu_import_errors=None, native_cpu_readbacks=None,
                hand_only_notifications=footer.get("hand_only_events"),
                limitations=["Only manager-delivered raw complete observations are counted; body count is never summed.",
                             "Retained manager delivery FPS is descriptive when validity fails; it does not recover unknown native bodies.",
                             "Public ProcessedFrames is BodySequence, not native V2 FreshBodyFrames.",
                             "Native fresh/GPU/decoder counters are unavailable through frozen public v13 APIs.",
                             "Local observation/publication age is not sensor capture-to-display age.",
                             "Unmapped SourceLocalMonotonic decode clocks and stream PTS are never subtracted from Unity/Input clocks.",
                             "Profiler/thermal/mirror/motion/physical semantics require separate actual evidence."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--records", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    result = analyze(json.loads(args.manifest.read_text(encoding="utf-8-sig")),
                     [json.loads(line) for line in args.records.read_text(encoding="utf-8-sig").splitlines() if line.strip()])
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    return 0 if result["manager_observation_validity"] == "VALID" else 2


if __name__ == "__main__":
    raise SystemExit(main())
