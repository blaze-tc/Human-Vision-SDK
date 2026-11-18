"""Summarize RuntimePrefabAcceptance JSONL and Android RTSP cadence in one elapsed-time window."""

import argparse
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
import json
from math import ceil
from pathlib import Path
import re
from statistics import median


LOG_TIME = re.compile(r"^\s*(\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\s+(\d+)\s+\d+\s+")
LOG_EPOCH = re.compile(r"^\s*(\d{9,}\.\d+)\s+(\d+)\s+\d+\s+")
CADENCE = re.compile(r"\binput_(decode|gpu|analysis)_cadence\s+(.*)")
PAIR_COUNTS = (
    "pair_identity_mismatches_window",
    "pair_input_id_verified_window",
    "pair_preview_size_mismatches_window",
)
RATE_FIELDS = {
    "decode": ("codec_output_fps", "acquired_image_fps"),
    "gpu": ("converted_fps", "decoded_to_submit_mean_ms", "submit_to_observed_fence_mean_ms"),
    "analysis": ("analysis_fps", "decoded_to_submit_mean_ms", "submit_to_observed_fence_mean_ms"),
}
COUNT_FIELDS = {
    "decode": ("codec_decode_samples", "codec_decode_unmatched_outputs", "codec_decode_skipped_outputs"),
    "gpu": ("completed", "slot_drops", "pending_replaced", "busy_events", "no_free_slot_events",
            "start_unobserved_events", "superseded_unobserved", "fence_pending_events", "preview_published"),
    "analysis": ("completed", "analysis_published", "analysis_no_slot", "preview_yield_to_analysis",
                 "preview_expired", "preview_skipped_no_slot", "preview_skipped_stride",
                 "preview_batched", "preview_replaced_by_batch", "preview_target_prepare_failures",
                 "preview_only_submitted", "preview_only_published",
                 "split_decoded", "split_decoded_replaced"),
}
MANAGED_COUNTS = {
    "unity_preview_publication": "preview_frames_window",
    "new_results_including_empty": "accepted_results_window",
    "valid_skeleton": "valid_skeleton_results_window",
    "matched_preview": "presented_results_window",
    "matched_valid_skeleton": "valid_skeleton_presented_window",
}


def load_jsonl(path):
    records, malformed = [], []
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        try:
            record = json.loads(line)
            if isinstance(record, dict) and isinstance(record.get("elapsed_s"), (int, float)):
                records.append(record)
            else:
                malformed.append(number)
        except ValueError:
            malformed.append(number)
    return records, malformed


def summarize_ages(values):
    values = sorted(value for value in values if value >= 0)
    if not values:
        return None
    return {"count": len(values), "p50": round(median(values), 3),
            "p95": round(values[ceil(len(values) * 0.95) - 1], 3), "max": round(values[-1], 3)}


def selected_samples(records, start, end, phase):
    return [record for record in records if record.get("kind") == "sample"
            and (not phase or record.get("phase") == phase) and record.get("window_s", 0) > 0
            and start <= record["elapsed_s"] - record["window_s"] and record["elapsed_s"] <= end
            and record.get("state") == "Running" and record.get("input_state") == "Streaming"]


def dimensions(samples, key):
    values = (record.get(key) or {} for record in samples)
    return Counter(f'{size["x"]}x{size["y"]}' for size in values if size.get("x", 0) > 0 and size.get("y", 0) > 0)


def mean_by_duration(rows, key, width):
    selected = [(row[key], row[width]) for row in rows if row.get(key, -1) >= 0]
    return round(sum(value * seconds for value, seconds in selected) / sum(seconds for _, seconds in selected), 3) if selected else None


def sum_if_present(rows, key):
    return sum(row[key] for row in rows if key in row) if any(key in row for row in rows) else None


def managed_summary(samples):
    duration = sum(record["window_s"] for record in samples)
    changes = {name: sum_if_present(samples, count) for name, count in MANAGED_COUNTS.items()}
    valid = changes["valid_skeleton"]
    paired = changes["matched_valid_skeleton"]
    return {
        "samples": len(samples),
        "coverage_s": round(duration, 3),
        "events": changes,
        "fps": {name: round(count / duration, 3) if duration and count is not None else None
                for name, count in changes.items()},
        "sdk_rate_samples_fps": {key: mean_by_duration(samples, key, "window_s")
                                 for key in ("unity_fps", "input_fps", "analysis_input_fps", "fresh_fps")},
        "matched_valid_to_valid_ratio": round(paired / valid, 4) if valid and paired is not None else None,
        "sizes": {key: dimensions(samples, key) for key in
                  ("stream_probed", "decoded", "published", "analysis_input", "analysis_submitted",
                   "model", "paired_color", "paired_preview")},
        "pair_counts": {key: sum_if_present(samples, key) for key in PAIR_COUNTS},
        "max_presented_gap_ms": max((s["max_presented_gap_ms"] for s in samples if s.get("max_presented_gap_ms", -1) >= 0), default=None),
        "pair_hold_age_ms": summarize_ages(s.get("displayed_pair_age_ms", -1) for s in samples),
        "decode_to_result_with_valid_skeleton_ms": summarize_ages(
            value for s in samples for value in s.get("decode_to_result_samples_ms", [])),
        "decode_to_matched_valid_skeleton_ms": summarize_ages(
            value for s in samples for value in s.get("decode_to_presented_samples_ms", [])),
        "latest_result_age_ms": summarize_ages(s.get("frame_age_ms", -1) for s in samples),
        "latest_infer_ms": summarize_ages(s.get("infer_ms", -1) for s in samples),
        "visible_pair_samples": sum(s.get("displayed_pair_visible", False) for s in samples),
        "expired_pair_samples": sum(s.get("displayed_pair_expired", False) for s in samples),
    }


def native_event_time(line, anchor, local_offset):
    match = LOG_EPOCH.match(line)
    if match:
        return datetime.fromtimestamp(float(match[1]), timezone.utc), int(match[2])
    match = LOG_TIME.match(line)
    if not match or local_offset is None:
        return None, None
    local = anchor + local_offset
    zone = timezone(local_offset)
    dates = (datetime.strptime(f"{year}-{match[1]}", "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=zone)
             for year in (local.year - 1, local.year, local.year + 1))
    return min(dates, key=lambda date: abs((date - anchor).total_seconds())), int(match[2])


def native_summary(records, start, end, logcat, pid):
    session = next((r for r in records if r.get("kind") == "session"), None)
    if not session or not session.get("utc"):
        return {}
    anchor = datetime.fromisoformat(session["utc"])
    match = re.search(r"\blogcat_local_utc_offset_min=(-?\d+)", session.get("detail", ""))
    local_offset = timedelta(minutes=int(match[1])) if match else None
    if logcat:
        events = [(line, None) for line in logcat.read_text(encoding="utf-8-sig", errors="replace").splitlines()]
    else:
        events = [(r.get("line", ""), r["elapsed_s"]) for r in records if r.get("kind") == "native"]
    groups = defaultdict(list)
    for line, received in events:
        match = CADENCE.search(line)
        if not match:
            continue
        timestamp, event_pid = native_event_time(line, anchor, local_offset)
        if pid is not None and event_pid != pid:
            continue
        stage, fields = match.groups()
        row = {key: float(value) for key, value in re.findall(r"\b([a-z][a-z0-9_]*)=(-?\d+(?:\.\d+)?)(?=\s|$)", fields)}
        interval = row.get("interval_ms", 0) / 1000
        elapsed = (timestamp - anchor).total_seconds() + session["elapsed_s"] if timestamp else received
        if elapsed is not None and interval > 0 and start <= elapsed - interval and elapsed <= end:
            row["interval_s"] = interval
            groups[stage].append(row)
    return {
        stage: {
            "interval_count": len(rows),
            "coverage_s": round(sum(row["interval_s"] for row in rows), 3),
            "generations": sorted({int(row["generation"]) for row in rows if "generation" in row}),
            "rates_and_mean_intervals": {key: mean_by_duration(rows, key, "interval_s") for key in RATE_FIELDS[stage]},
            "counts": {key: int(value) if value is not None else None
                       for key in COUNT_FIELDS[stage] if (value := sum_if_present(rows, key)) is not None},
        } for stage, rows in groups.items()
    }


def analyze(path, warmup, seconds, start, phase, logcat, pid):
    records, malformed = load_jsonl(path)
    if start is None:
        ready = next((row for row in records if row.get("kind") == "mark" and row.get("outcome") == "ready"
                      and (not phase or row.get("phase") == phase)), None)
        if ready is None:
            raise ValueError("No matching ready marker. Use --start for an explicit elapsed-time window.")
        start = ready["elapsed_s"] + warmup
    end = start + seconds
    samples = selected_samples(records, start, end, phase)
    if not samples:
        raise ValueError(f"No complete running/streaming samples inside [{start:.3f}, {end:.3f}].")
    people = [s for s in samples if s.get("bodies", 0) > 0]
    empty = [s for s in samples if s.get("bodies", 0) == 0 and s.get("valid_skeleton_results_window") == 0
             and s.get("accepted_results_window", 0) > 0]
    return {
        "input_jsonl": str(path),
        "malformed_jsonl_lines": malformed,
        "requested_elapsed_window_s": [start, end],
        "phase_filter": phase,
        "device": next((r.get("detail") for r in records if r.get("kind") == "session"), None),
        "versions": sorted({s.get("sdk_version", "") for s in samples}),
        "actual_backends": dict(Counter(s.get("actual_backend", "") for s in samples)),
        "profiles": dict(Counter(s.get("profile", "") for s in samples)),
        "stream_selection": dict(Counter(f'{s.get("stream", "")} | {s.get("stream_selection_reason", "")}' for s in samples)),
        "source_generations": sorted({(s.get("source_id", ""), s.get("source_generation", "")) for s in samples}),
        "battery_c": summarize_ages((s.get("hardware") or {}).get("batteryTemperatureC", -1) for s in samples),
        "thermal_states": dict(Counter((s.get("hardware") or {}).get("thermalStatus", "unavailable") for s in samples)),
        "fixture_markers": [r for r in records if r.get("kind") in ("mark", "lifecycle", "stream_geometry")],
        "native": native_summary(records, start, end, logcat, pid),
        "all_managed": managed_summary(samples),
        "managed_with_people_at_sample": managed_summary(people),
        "managed_zero_bodies_and_no_valid_skeleton_in_sample": managed_summary(empty),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jsonl", type=Path)
    parser.add_argument("--seconds", type=float, required=True, help="Length of measurement window.")
    parser.add_argument("--warmup", type=float, default=4, help="Seconds after ready marker.")
    parser.add_argument("--start", type=float, help="Explicit start in the session elapsed time domain.")
    parser.add_argument("--phase")
    parser.add_argument("--logcat", type=Path, help="Use external -v epoch or -v threadtime logcat instead of embedded native records.")
    parser.add_argument("--pid", type=int, help="Restrict native records to this process ID.")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.seconds <= 0 or args.warmup < 0:
        parser.error("seconds must be positive and warmup nonnegative")
    try:
        result = analyze(args.jsonl, args.warmup, args.seconds, args.start, args.phase, args.logcat, args.pid)
    except ValueError as error:
        parser.error(str(error))
    data = json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False) + "\n"
    if args.out is not None:
        args.out.write_text(data, encoding="utf-8")
    else:
        print(data)


if __name__ == "__main__":
    main()
