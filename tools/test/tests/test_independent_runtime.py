"""Regression tests for cumulative runtime observations and fixture ownership."""

import hashlib
import importlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


def load(name, test):
    try:
        return importlib.import_module(name)
    except ModuleNotFoundError as error:
        if error.name != name:
            raise
        test.fail(f"Missing acceptance tool {name}")


def sample(elapsed_us, *, frame_id=10, native_input_frames=12, native_fresh_frames=8, **fields):
    record = {
        "kind": "sample", "elapsed_us": elapsed_us, "source_id": 42, "generation": 1,
        "frame_id": frame_id, "native_input_frames": native_input_frames,
        "native_fresh_frames": native_fresh_frames, "unity_callbacks": 0,
        "unity_deliveries": 0, "unity_renders": 0, "actual_backend": "cpu",
        "execution_mode": "native", "input_errors": 0, "runtime_errors": 0,
    }
    record.update(fields)
    return record


class AnalyzerTests(unittest.TestCase):
    def report(self, rows, **options):
        tool = load("analyze_independent_runtime", self)
        lines = [
            row if isinstance(row, str) else "I Unity: HV_INDEPENDENT_PROGRESS " + json.dumps(row)
            for row in rows
        ]
        return tool.analyze_progress(lines, **options)

    def test_repeated_sample_does_not_count_as_new_result(self):
        report = self.report([
            sample(0), sample(1_000_000, unity_callbacks=60),
            sample(2_000_000, frame_id=14, native_input_frames=18, native_fresh_frames=11,
                   unity_callbacks=120, unity_deliveries=2, unity_renders=1),
        ])
        self.assertEqual(report["status"], "observed")
        self.assertEqual(report["rates_hz"]["native_fresh_frames"], 1.5)
        self.assertEqual(report["rates_hz"]["unity_callbacks"], 60)
        self.assertEqual(report["rates_hz"]["unity_deliveries"], 1)
        self.assertEqual(report["rates_hz"]["unity_renders"], 0.5)

    def test_repeated_log_line_with_equal_timestamp_is_ignored(self):
        repeat = sample(1_000_000, frame_id=11, native_fresh_frames=9)
        report = self.report([sample(0), repeat, repeat])
        self.assertEqual(report["rates_hz"]["native_fresh_frames"], 1)
        self.assertEqual(report["status"], "observed")

    def test_unavailable_measurement_stays_unknown(self):
        report = self.report([sample(0, native_input_frames=None),
                              sample(1_000_000, native_input_frames=None)])
        self.assertIsNone(report["rates_hz"]["native_input_frames"])
        self.assertEqual(report["rates_hz"]["native_fresh_frames"], 0)

    def test_fresh_counter_cannot_advance_against_stale_identity(self):
        report = self.report([sample(0), sample(1_000_000, native_fresh_frames=9)])
        self.assertEqual(report["status"], "invalid")
        self.assertIsNone(report["rates_hz"]["native_fresh_frames"])

    def test_epoch_changes_do_not_merge_counters(self):
        report = self.report([
            sample(0), sample(1_000_000, frame_id=11, native_fresh_frames=9),
            sample(2_000_000, frame_id=1, native_fresh_frames=1, generation=2),
            sample(3_000_000, frame_id=5, native_fresh_frames=5, generation=2),
        ])
        self.assertEqual(report["status"], "invalid")
        self.assertIsNone(report["rates_hz"]["native_fresh_frames"])
        self.assertEqual(len(report["epochs"]), 2)

    def test_counter_reset_within_epoch_is_invalid(self):
        report = self.report([sample(0), sample(1_000_000, native_input_frames=1)])
        self.assertEqual(report["status"], "invalid")
        self.assertIsNone(report["rates_hz"]["native_input_frames"])

    def test_overrun_is_visible_even_while_native_progress_continues(self):
        report = self.report([
            sample(0, result_overruns=2),
            sample(1_000_000, frame_id=15, native_input_frames=18, native_fresh_frames=11,
                   result_overruns=4),
        ])
        self.assertEqual(report["deltas"]["native_fresh_frames"], 3)
        self.assertEqual(report["deltas"]["result_overruns"], 2)

    def test_stall_requires_separate_native_progress_and_stopped_callbacks(self):
        report = self.report([
            sample(0),
            sample(1_000_000, kind="stall_begin", stall_requested_ms=500, unity_callbacks=60),
            sample(1_510_000, kind="stall_end", stall_requested_ms=500, unity_callbacks=60,
                   frame_id=14, native_input_frames=15, native_fresh_frames=10),
            sample(2_510_000, frame_id=15, native_input_frames=21, native_fresh_frames=11,
                   unity_callbacks=120),
        ], requested_stall_ms=500)
        stall, = report["stalls"]
        self.assertEqual(stall["status"], "observed")
        self.assertEqual(stall["deltas"]["native_fresh_frames"], 2)
        self.assertEqual(report["steady_rates_hz"]["native_fresh_frames"], 0.5)

    def test_complete_stall_without_native_progress_is_reported(self):
        report = self.report([
            sample(1_000_000, kind="stall_begin", stall_requested_ms=2000),
            sample(3_000_000, kind="stall_end", stall_requested_ms=2000),
        ], requested_stall_ms=2000)
        self.assertEqual(report["stalls"][0]["status"], "no_native_progress")

    def test_stall_cannot_be_certified_without_callback_evidence(self):
        report = self.report([
            sample(1_000_000, kind="stall_begin", stall_requested_ms=2000, unity_callbacks=None),
            sample(3_000_000, kind="stall_end", stall_requested_ms=2000, unity_callbacks=None,
                   frame_id=14, native_input_frames=18, native_fresh_frames=11),
        ], requested_stall_ms=2000)
        self.assertEqual(report["stalls"][0]["status"], "unsupported")

    def test_stall_cannot_join_across_epoch_change(self):
        report = self.report([
            sample(1_000_000, kind="stall_begin", stall_requested_ms=2000),
            sample(3_000_000, kind="stall_end", stall_requested_ms=2000, generation=2,
                   frame_id=14, native_input_frames=18, native_fresh_frames=11),
        ], requested_stall_ms=2000)
        self.assertEqual(report["stalls"][0]["status"], "invalid")

    def test_requested_stall_without_markers_is_unsupported(self):
        report = self.report([sample(0), sample(1_000_000)], requested_stall_ms=500)
        self.assertEqual(report["stalls"][0]["status"], "unsupported")

    def test_absent_probe_does_not_misrepresent_unrelated_native_log(self):
        report = self.report(["D HVInput: input_decode_cadence generation=2 codec_output_fps=30"])
        self.assertEqual(report["status"], "unsupported")
        self.assertIsNone(report["rates_hz"]["native_fresh_frames"])

    def test_malformed_marker_invalidates_the_window(self):
        report = self.report([sample(0), "I Unity: HV_INDEPENDENT_PROGRESS {oops}",
                              sample(1_000_000)])
        self.assertEqual(report["status"], "invalid")
        self.assertIsNone(report["rates_hz"]["native_fresh_frames"])

    def test_frozen_result_age_is_excluded_from_fresh_result_sample_distribution(self):
        report = self.report([sample(0, result_age_us=5_000),
                              sample(1_000_000, result_age_us=500_000),
                              sample(2_000_000, frame_id=12, native_fresh_frames=9,
                                     result_age_us=19_000)])
        self.assertEqual(report["fresh_result_age_ms"]["max"], 19)
        self.assertEqual(report["fresh_result_age_ms"]["count"], 1)

    def test_slowest_window_reports_real_counter_changes(self):
        report = self.report([
            sample(0), sample(5_000_000, frame_id=30, native_fresh_frames=18),
            sample(10_000_000, frame_id=50, native_fresh_frames=28),
            sample(15_000_000, frame_id=55, native_fresh_frames=32),
        ])
        self.assertEqual(report["slowest_fresh_window"]["finish_us"], 15_000_000)
        self.assertEqual(report["slowest_fresh_window"]["fps"], 1.4)

    def test_request_falls_back_to_incompatible_backend_or_execution(self):
        report = self.report([sample(0), sample(1_000_000)],
                             expected_backend="gpu", expected_execution="compatible")
        self.assertEqual(report["requested_controls"]["backend"]["status"], "mismatch")
        self.assertEqual(report["requested_controls"]["execution_mode"]["status"], "mismatch")

    def test_non_streaming_period_does_not_count_toward_stall(self):
        report = self.report([
            sample(1_000_000, kind="stall_begin", stall_requested_ms=500),
            sample(1_510_000, kind="stall_end", stall_requested_ms=500, streaming=False),
        ], requested_stall_ms=500)
        self.assertEqual(report["stalls"][0]["status"], "invalid")


    def test_measured_window_filters_other_processes_and_old_log_records(self):
        tool = load("analyze_independent_runtime", self)
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            logcat = directory / "logcat.txt"
            diagnostics = directory / "runtime-prefab.jsonl"
            diagnostics.write_text(
                '{"kind":"mark","outcome":"ready","utc":"2026-01-01T00:00:00Z","elapsed_s":0}\n',
                encoding="utf-8")
            events = [
                (1767225599.9, 25, sample(0, kind="stall_begin", stall_requested_ms=500)),
                (1767225604.0, 25, sample(4_000_000)),
                (1767225604.1, 26, sample(4_100_000, generation=84)),
                (1767225605.0, 25, sample(5_000_000, native_fresh_frames=9, frame_id=11)),
                (1767225605.1, 25, sample(5_100_000, generation=2)),
            ]
            logcat.write_text("".join(
                f"{stamp:.3f} {pid} 70 I Unity: HV_INDEPENDENT_PROGRESS {json.dumps(row)}\n"
                for stamp, pid, row in events
            ), encoding="utf-8")
            report = tool.analyze_run(logcat, diagnostics, 4, 1, 25, capture_start_epoch=1767225600)
        self.assertEqual(report["status"], "observed")
        self.assertEqual(report["rates_hz"]["native_fresh_frames"], 1)
        self.assertEqual(report["stalls"], [])

    def test_wrong_probe_field_type_is_invalid(self):
        report = self.report([sample(0), sample(1_000_000, actual_backend=["gpu"])])
        self.assertEqual(report["status"], "invalid")

    def test_detects_stall_with_requested_execution_fallback(self):
        report = self.report([
            sample(1_000_000, kind="stall_begin", stall_requested_ms=500, execution_mode="compatible"),
            sample(1_510_000, kind="stall_end", stall_requested_ms=500, execution_mode="compatible",
                   frame_id=15, native_input_frames=20, native_fresh_frames=12),
        ], requested_stall_ms=500, expected_execution="native")
        self.assertEqual(report["stalls"][0]["status"], "invalid")


class Reverse:
    """Fake device map with the actual list, no-rebind and remove side effects."""
    def __init__(self, mappings=None, fail_add=False):
        self.mappings = dict(mappings or {})
        self.fail_add = fail_add

    def __call__(self, *args):
        if args == ("reverse", "--list"):
            return "".join(f"UsbFfs {local} {target}\n" for local, target in self.mappings.items())
        if args[:2] == ("reverse", "--no-rebind") and len(args) == 4:
            if args[2] in self.mappings or self.fail_add:
                raise RuntimeError("adb could not acquire mapping")
            self.mappings[args[2]] = args[3]
            return ""
        if args[:2] == ("reverse", "--remove") and len(args) == 3:
            del self.mappings[args[2]]
            return ""
        raise ValueError(args)


class RunnerTests(unittest.TestCase):
    def ownership(self, fake):
        tool = load("run_independent_runtime", self)
        return tool.owned_reverse(fake, 18569)

    def test_preexisting_mapping_for_remote_port_is_preserved(self):
        fake = Reverse({"tcp:18569": "tcp:5000"})
        with self.assertRaisesRegex(RuntimeError, "already"):
            with self.ownership(fake):
                self.fail("the run acquired an existing port")
        self.assertEqual(fake.mappings, {"tcp:18569": "tcp:5000"})

    def test_cleanup_after_failure_removes_only_the_owned_port(self):
        fake = Reverse({"tcp:5000": "tcp:18569"})
        with self.assertRaisesRegex(LookupError, "failure"):
            with self.ownership(fake):
                self.assertEqual(fake.mappings["tcp:18569"], "tcp:18569")
                raise LookupError("failure")
        self.assertEqual(fake.mappings, {"tcp:5000": "tcp:18569"})

    def test_cleanup_does_not_remove_mapping_replaced_by_other_controller(self):
        fake = Reverse()
        with self.ownership(fake):
            fake.mappings["tcp:18569"] = "tcp:9900"
        self.assertEqual(fake.mappings, {"tcp:18569": "tcp:9900"})

    def test_failed_acquisition_performs_no_removal(self):
        fake = Reverse({"tcp:2222": "tcp:18569"}, fail_add=True)
        with self.assertRaisesRegex(RuntimeError, "acquire"):
            with self.ownership(fake):
                self.fail("the run acquired a rejected mapping")
        self.assertEqual(fake.mappings, {"tcp:2222": "tcp:18569"})

    def test_hash_receipt_tracks_compressed_libraries_and_model_assets(self):
        tool = load("run_independent_runtime", self)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp, "candidate.apk")
            with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("lib/arm64-v8a/libhumanvision.so", b"native version 1")
                archive.writestr("lib/arm64-v8a/libhumanvision_input.so", b"input version 2")
                archive.writestr("assets/Runtime/modelpacks/pose/weights.bin", b"model")
                archive.writestr("assets/other.txt", b"unimportant")
            receipt = tool.describe_apk(path)
            self.assertEqual(receipt["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertEqual(set(receipt["native_libraries"]),
                             {"lib/arm64-v8a/libhumanvision.so", "lib/arm64-v8a/libhumanvision_input.so"})
            self.assertEqual(receipt["model_assets"],
                             {"assets/Runtime/modelpacks/pose/weights.bin":
                              hashlib.sha256(b"model").hexdigest()})

    def test_apk_for_other_package_is_rejected_before_install(self):
        tool = load("run_independent_runtime", self)
        with self.assertRaisesRegex(RuntimeError, "package"):
            tool.validate_apk_badging("package: name='com.blazetc.other' versionCode='1'")

    def test_apk_activity_is_verified_before_install(self):
        tool = load("run_independent_runtime", self)
        with self.assertRaisesRegex(RuntimeError, "activity"):
            tool.validate_apk_badging(
                "package: name='com.blazetc.humanvision.runtimeacceptance.independent' versionCode='1'\n"
                "launchable-activity: name='com.example.OtherActivity' label='' icon=''")


    def test_ready_poll_retains_owned_diagnostics_when_capture_aborts(self):
        tool = load("run_independent_runtime", self)
        diagnostics = ("/storage/emulated/0/Android/data/"
                       "com.blazetc.humanvision.runtimeacceptance.independent/"
                       "files/RuntimePrefabDiagnostics/runtime-prefab-20261123-083000-000.jsonl")
        receipt = {}

        def interrupted_read(*args, **options):
            raise RuntimeError("fixture disconnected before readiness")

        with tempfile.TemporaryDirectory() as folder:
            logcat = Path(folder, "logcat.txt")
            logcat.write_text(
                f"1767225601.000 10 12 I Unity: HV_RT_DIAGNOSTICS session={diagnostics}\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "fixture disconnected"):
                tool.wait_for_ready(interrupted_read, logcat, {}, 1767225600, 1, receipt)
        self.assertEqual(receipt["diagnostics_device_path"], diagnostics)

    def test_logged_diagnostics_path_cannot_escape_owned_app_directory(self):
        tool = load("run_independent_runtime", self)
        actual = "1770000000.00 15 21 I Unity: HV_RT_DIAGNOSTICS session="
        with self.assertRaisesRegex(RuntimeError, "diagnostics path"):
            tool.diagnostics_path(actual + "/storage/emulated/0/Download/../secret.jsonl")


if __name__ == "__main__":
    unittest.main()
