"""Analyze observed SDK publications and Unity consumption from acceptance logcat.

HV_INDEPENDENT_PROGRESS emits sampled cumulative counters and tags each snapshot
with the latest canonical result identity. Null or absent fields are unknown.
The two stall markers take snapshots immediately around the main-thread sleep.
"""

import argparse
from bisect import bisect_left
from datetime import datetime
import json
from math import isfinite
from pathlib import Path
import sys

if __name__ == "__main__":
    sys.dont_write_bytecode = True

from analyze_rtsp_preview_cadence import LOG_EPOCH, analyze as analyze_cadence, load_jsonl, summarize_ages

MARKER = "HV_INDEPENDENT_PROGRESS "
KINDS = {"sample", "stall_begin", "stall_end"}
RATE_FIELDS = (
    "native_input_frames", "native_fresh_frames", "unity_callbacks",
    "unity_deliveries", "unity_renders",
)
COUNT_FIELDS = RATE_FIELDS + ("input_errors", "runtime_errors", "input_drops", "result_overruns")
EPOCH_FIELDS = ("source_id", "generation", "config_epoch", "region_revision")
CONTEXT_FIELDS = (
    "actual_source", "actual_image_service", "actual_backend", "execution_mode",
    "capability_level", "preview_enabled", "target_fps",
)
TEXT_CONTEXT_FIELDS = CONTEXT_FIELDS[:-2]
BACKENDS = {"backend.ort.cpu": "cpu", "backend.ncnn.vulkan": "gpu", "backend.rknn": "npu"}


def count(value):
    return value if type(value) is int and value >= 0 else None


def read_progress(lines):
    records, malformed = [], []
    for number, line in enumerate(lines, 1):
        if MARKER not in line:
            continue
        payload = line.split(MARKER, 1)[1]
        try:
            row = json.loads(payload)
            if (
                not isinstance(row, dict) or row.get("kind") not in KINDS
                or count(row.get("elapsed_us")) is None
                or any(row.get(field) is not None and count(row[field]) is None
                       for field in (*COUNT_FIELDS, *EPOCH_FIELDS, "frame_id", "result_age_us",
                                     "stall_requested_ms", "target_fps"))
                or any(row.get(field) is not None and not isinstance(row[field], str)
                       for field in TEXT_CONTEXT_FIELDS)
                or any(row.get(field) is not None and type(row[field]) is not bool
                       for field in ("streaming", "preview_enabled"))
            ):
                raise ValueError("invalid progress record")
        except (ValueError, TypeError):
            malformed.append(number)
            continue
        if records and row == records[-1][0]:
            continue
        match = LOG_EPOCH.match(line)
        records.append((row, float(match[1]) if match else None))
    return records, malformed


def epoch(row):
    return tuple(row.get(field) for field in EPOCH_FIELDS)


def interval(first, last):
    duration = last["elapsed_us"] - first["elapsed_us"]
    deltas = {
        field: None if first.get(field) is None or last.get(field) is None else last[field] - first[field]
        for field in COUNT_FIELDS
    }
    if duration <= 0:
        return duration, deltas, "non_increasing_elapsed_time"
    if not first.get("source_id") or not first.get("generation") or not last.get("source_id") or not last.get("generation"):
        return duration, deltas, "missing_source_epoch"
    if epoch(first) != epoch(last):
        return duration, deltas, "source_epoch_changed"
    if first.get("streaming") is False or last.get("streaming") is False:
        return duration, deltas, "source_not_streaming"
    if any(first.get(key) is not None and last.get(key) is not None and first[key] != last[key]
           for key in CONTEXT_FIELDS):
        return duration, deltas, "execution_context_changed"
    if any(value is not None and value < 0 for value in deltas.values()):
        return duration, deltas, "counter_decreased"
    fresh = deltas["native_fresh_frames"]
    first_frame, last_frame = first.get("frame_id"), last.get("frame_id")
    if first_frame is None or last_frame is None:
        deltas["native_fresh_frames"] = None
    else:
        frame_advance = last_frame - first_frame
        if frame_advance < 0:
            return duration, deltas, "result_frame_decreased"
        if fresh is not None and (fresh > frame_advance or (fresh == 0 and frame_advance > 0)):
            return duration, deltas, "fresh_counter_disagrees_with_result_identity"
    return duration, deltas, None


def total_deltas(intervals):
    result = dict.fromkeys(COUNT_FIELDS)
    for field in result:
        changes = [deltas[field] for _, deltas, _ in intervals]
        if changes and all(change is not None for change in changes):
            result[field] = sum(changes)
    return result


def fps(deltas, duration):
    return {field: round(deltas[field] * 1_000_000 / duration, 3)
            if duration > 0 and deltas[field] is not None else None for field in RATE_FIELDS}


def stall_summary(begin, end, requested_ms, expected_controls):
    duration, deltas, issue = interval(begin, end)
    configured = begin.get("stall_requested_ms")
    rates_available = all(deltas[key] is not None for key in ("native_input_frames", "native_fresh_frames", "unity_callbacks"))
    controls = {key: requested_control([begin, end], key, expected)
                for key, expected in expected_controls.items()}
    if any(control["status"] == "mismatch" for control in controls.values()):
        issue = issue or "requested_control_mismatch"
    if issue or configured != end.get("stall_requested_ms") or (requested_ms is not None and configured != requested_ms):
        status = "invalid"
    elif (type(configured) is not int or configured <= 0 or not rates_available
          or any(control["status"] == "unsupported" for control in controls.values())):
        status = "unsupported"
    elif duration < configured * 950:
        status = "stall_too_short"
    elif deltas["unity_callbacks"] != 0:
        status = "unity_callbacks_advanced"
    elif any(deltas[key] for key in ("unity_deliveries", "unity_renders")):
        status = "unity_consumption_advanced"
    elif any(deltas[key] is None for key in ("input_errors", "runtime_errors")):
        status = "unsupported"
    elif any(deltas[key] for key in ("input_errors", "runtime_errors")):
        status = "errors_during_stall"
    elif not deltas["native_input_frames"] or not deltas["native_fresh_frames"]:
        status = "no_native_progress"
    else:
        status = "observed"
    return {
        "status": status, "issue": issue, "requested_ms": configured,
        "begin_us": begin["elapsed_us"], "finish_us": end["elapsed_us"],
        "observed_ms": duration / 1000, "deltas": deltas if issue is None else dict.fromkeys(COUNT_FIELDS),
        "requested_controls": controls,
    }


def inspect_stalls(rows, requested_ms, expected_controls):
    reports, begin = [], None
    for row in rows:
        if row["kind"] == "stall_begin":
            if begin is not None:
                reports.append({"status": "invalid", "issue": "nested_stall"})
            begin = row
        elif row["kind"] == "stall_end":
            reports.append({"status": "invalid", "issue": "stall_end_without_begin"} if begin is None
                           else stall_summary(begin, row, requested_ms, expected_controls))
            begin = None
    if begin is not None:
        reports.append({"status": "incomplete", "issue": "stall_begin_without_end"})
    if requested_ms is not None and not reports:
        reports.append({"status": "unsupported", "issue": "no_stall_markers", "requested_ms": requested_ms})
    return reports


def slowest_fresh_window(rows):
    times = [row["elapsed_us"] for row in rows]
    windows = []
    for start, time_us in enumerate(times):
        end = bisect_left(times, time_us + 10_000_000, lo=start + 1)
        if end == len(times):
            break
        # Aggregate snapshots can bound a window with actual samples. Do not
        # interpolate unobserved publications to make it exactly 10 seconds.
        width = times[end] - time_us
        fresh = rows[end]["native_fresh_frames"] - rows[start]["native_fresh_frames"]
        windows.append({"start_us": time_us, "finish_us": times[end], "window_seconds": width / 1_000_000,
                        "fresh_frames": fresh, "fps": round(fresh * 1_000_000 / width, 3)})
    return min(windows, key=lambda window: window["fps"], default=None)


def person_rates(rows, duration):
    maps = [row.get("complete_body_frames") for row in rows]
    if not maps or not all(isinstance(item, dict) for item in maps):
        return None
    result = {}
    for track in set().union(*maps):
        values = [count(item.get(track)) for item in maps]
        if all(value is not None for value in values) and values == sorted(values):
            result[track] = round((values[-1] - values[0]) * 1_000_000 / duration, 3)
        else:
            result[track] = None
    return result


def requested_control(rows, field, expected):
    values = [row.get(field) for row in rows]
    if field == "actual_backend":
        values = [BACKENDS.get(value, value) for value in values]
    observed = sorted(set(value for value in values if value is not None))
    if expected is None:
        status = "not_requested"
    elif not observed or any(value is None for value in values):
        status = "unsupported"
    elif observed != [expected]:
        status = "mismatch"
    else:
        status = "observed"
    return {"status": status, "requested": expected, "observed": observed}


def analyze_progress(lines, *, requested_stall_ms=None, expected_backend=None, expected_execution=None,
                     expected_preview=None, expected_target_fps=None, window_epoch=None):
    records, malformed = read_progress(lines)
    all_rows = [row for row, _ in records]
    rows = all_rows if window_epoch is None else [
        row for row, timestamp in records if timestamp is not None and window_epoch[0] <= timestamp <= window_epoch[1]
    ]
    pairs = list(zip(rows, rows[1:]))
    intervals = [interval(first, last) for first, last in pairs]
    issues = sorted({issue for _, _, issue in intervals if issue is not None})
    if malformed:
        issues.append("malformed_progress")
    expected_controls = {"actual_backend": expected_backend, "execution_mode": expected_execution,
                         "preview_enabled": expected_preview, "target_fps": expected_target_fps}
    stalls = inspect_stalls(all_rows, requested_stall_ms, expected_controls)
    if malformed:
        for stall in stalls:
            if stall["status"] == "observed":
                stall["status"] = "invalid"
                stall["issue"] = "malformed_progress"

    stall_bounds = [(s["begin_us"], s["finish_us"]) for s in stalls if "begin_us" in s]
    steady = [
        current for (first, last), current in zip(pairs, intervals)
        if not any(first["elapsed_us"] < end and start < last["elapsed_us"] for start, end in stall_bounds)
    ]
    duration = sum(width for width, _, _ in intervals)
    deltas = dict.fromkeys(COUNT_FIELDS) if issues else total_deltas(intervals)
    steady_deltas = dict.fromkeys(COUNT_FIELDS) if issues else total_deltas(steady)
    if issues:
        status = "invalid"
    elif not records:
        status = "unsupported"
    elif not intervals:
        status = "insufficient"
    elif deltas["native_fresh_frames"] is None:
        status = "unsupported"
    else:
        status = "observed"

    # A retained result can grow arbitrarily old without a new publication.
    # Include only sampled ages attached to a newly observed canonical result.
    fresh_ages_ms = [last["result_age_us"] / 1000 for (_, last), (_, changes, issue) in zip(pairs, intervals)
                     if issue is None and changes["native_fresh_frames"] and last.get("result_age_us") is not None]
    return {
        "status": status,
        "sample_count": len(rows),
        "progress_record_count": len(all_rows),
        "coverage_seconds": duration / 1_000_000 if duration > 0 and not issues else 0,
        "issues": issues,
        "malformed_marker_lines": malformed,
        "epochs": [dict(zip(EPOCH_FIELDS, item)) for item in dict.fromkeys(map(epoch, rows))],
        "execution_context": {key: list(dict.fromkeys(row[key] for row in rows if row.get(key) is not None))
                              for key in CONTEXT_FIELDS},
        "requested_controls": {
            "backend": requested_control(rows, "actual_backend", expected_backend),
            "execution_mode": requested_control(rows, "execution_mode", expected_execution),
            "preview_enabled": requested_control(rows, "preview_enabled", expected_preview),
            "target_fps": requested_control(rows, "target_fps", expected_target_fps),
        },
        "deltas": deltas,
        "rates_hz": fps(deltas, duration),
        "steady_rates_hz": fps(steady_deltas, sum(width for width, _, _ in steady)),
        "fresh_result_age_ms": summarize_ages(fresh_ages_ms) if not issues else None,
        "per_track_complete_rates_hz": person_rates(rows, duration) if duration > 0 and not issues else None,
        "slowest_fresh_window": slowest_fresh_window(rows) if deltas["native_fresh_frames"] is not None else None,
        "stalls": stalls,
    }


def analyze_run(logcat, diagnostics=None, warmup=4, seconds=None, pid=None, *, capture_start_epoch=None, **controls):
    measurement = None
    cadence = None
    if diagnostics is not None and diagnostics.exists():
        records, malformed = load_jsonl(diagnostics)
        ready = next((record for record in records
                      if record.get("kind") == "mark" and record.get("outcome") == "ready"), None)
        if ready and ready.get("utc") and seconds is not None:
            start = datetime.fromisoformat(ready["utc"]).timestamp() + warmup
            measurement = (start, start + seconds)
            try:
                cadence = analyze_cadence(diagnostics, warmup, seconds, None, ready.get("phase"), logcat, pid)
            except ValueError as error:
                cadence = {"status": "insufficient", "issue": str(error),
                           "malformed_jsonl_lines": malformed}
    with logcat.open(encoding="utf-8-sig", errors="replace") as stream:
        lines = stream
        if pid is not None or capture_start_epoch is not None:
            lines = (line for line in stream if (match := LOG_EPOCH.match(line))
                     and (pid is None or int(match[2]) == pid)
                     and (capture_start_epoch is None or float(match[1]) >= capture_start_epoch))
        result = analyze_progress(lines, window_epoch=measurement, **controls)
    result["legacy_cadence"] = cadence
    result["measurement_epoch_seconds"] = measurement
    if seconds is not None:
        result["requested_measurement_seconds"] = seconds
        if measurement is None:
            result["measurement_status"] = "unsupported"
        elif result["status"] != "observed":
            result["measurement_status"] = result["status"]
        elif result["coverage_seconds"] < seconds:
            # Display the measured coverage without extrapolating near either endpoint.
            result["measurement_status"] = "sample_bounded"
        else:
            result["measurement_status"] = "observed"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logcat", type=Path)
    parser.add_argument("--diagnostics", type=Path)
    parser.add_argument("--warmup", type=float, default=4)
    parser.add_argument("--seconds", type=float)
    parser.add_argument("--pid", type=int)
    parser.add_argument("--capture-start-epoch", type=float)
    parser.add_argument("--backend", choices=("cpu", "gpu", "npu"))
    parser.add_argument("--execution", choices=("compatible", "native"))
    parser.add_argument("--stall-ms", type=int, choices=(500, 2000))
    parser.add_argument("--preview-off", action="store_true")
    parser.add_argument("--target-fps", type=int, choices=(15, 30, 60))
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if (not isfinite(args.warmup) or args.warmup < 0
        or (args.seconds is not None and (not isfinite(args.seconds) or args.seconds <= 0))
        or (args.capture_start_epoch is not None and not isfinite(args.capture_start_epoch))):
        parser.error("warmup must be nonnegative and seconds must be positive")
    report = analyze_run(
        args.logcat, args.diagnostics, args.warmup, args.seconds, args.pid, capture_start_epoch=args.capture_start_epoch,
        requested_stall_ms=args.stall_ms, expected_backend=args.backend, expected_execution=args.execution,
        expected_preview=False if args.preview_off else None, expected_target_fps=args.target_fps,
    )
    result = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + "\n"
    if args.out:
        args.out.write_text(result, encoding="utf-8")
    else:
        print(result, end="")
    return 2 if report["status"] == "invalid" else 0


if __name__ == "__main__":
    raise SystemExit(main())
