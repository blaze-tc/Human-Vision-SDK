"""Arithmetic/rejection fixtures only; these are never device or pose evidence."""
import copy
import unittest
try:
    from tools.test import formal_demo_observation_analysis as subject
except ImportError:
    subject = None


def fixture():
    manifest = {key: "a" * 64 for key in (
        "apk_sha256", "native_sha256", "model_index_sha256", "package_index_sha256",
        "recorder_sha256", "source_evidence_sha256")}
    manifest.update(commit="9d75379", profile="android_local", source_rate_hz=25)
    header = dict(kind="header", schema=1, run_id="fixture-not-device", profile="android_local",
                  gpu=True, runtime=True, max_bodies=8, start_unity_us=100000000,
                  duration_us=60000000, warmup_us=10000000, window_us=40000000,
                  begin_sequence=100, begin_source_frame=0)
    rows = [header, dict(kind="sample", elapsed_us=0, sequence=100, submitted=100,
                         processed_sequence=100, runtime_drops=0, bridge_drops=0,
                         bridge_copies=0, cpu_readbacks=0, source_state=2, error=False)]
    for index, (elapsed, count) in enumerate([(9999999, 7), (10000000, 0),
                                               (49999999, 2), (50000000, 7)], 1):
        now = 900000000 + elapsed
        rows.append(dict(kind="source", elapsed_us=elapsed, source_id=1, generation=1,
                         frame_id=index, published_us=now-100000, source_us=now-120000,
                         source_clock=1, source_clock_id=0, timestamp_kind=0, pts_us=index*40000,
                         width=1280, height=720, rotation=0, mirror=False, row_origin=0,
                         color_space=1, resource_token=1))
        joint = dict(px=1., py=2., nx=.1, ny=.2, confidence=.8, valid=True, derived=False,
                     observation_us=100000000+elapsed-100000, prediction_ms=0.)
        body = dict(track_id=1, stable_track_id=1, region_index=-1, observation_us=joint["observation_us"],
                    confidence=.8, x=1., y=2., width=3., height=4.,
                    canonical_joints=[copy.deepcopy(joint) for _ in range(32)],
                    joints=[copy.deepcopy(joint) for _ in range(17)],
                    hand_joints=[copy.deepcopy(joint) for _ in range(6)])
        rows.append(dict(kind="observation", elapsed_us=elapsed, sequence=100+index,
                         source_frame_id=index, source_timestamp_us=100000000+elapsed-100000,
                         unity_before_us=100000000+elapsed, unity_after_us=100000000+elapsed+10,
                         input_now_us=now, body_count=count,
                         processed_sequence=100+index, submitted=100+index, runtime_drops=0,
                         bodies=[copy.deepcopy(body) for _ in range(count)]))
    rows.extend([dict(kind="sample", elapsed_us=60000000, sequence=104, submitted=104,
                     processed_sequence=104, runtime_drops=0, bridge_drops=0,
                     bridge_copies=4, cpu_readbacks=0, source_state=2, error=False),
                 dict(kind="footer", elapsed_us=60000000, end_sequence=104, end_source_frame=4,
                      observation_count=4, source_count=4, sample_count=2, overflow=False,
                      interrupted=False, hand_only_events=3, copy_invalid=False)])
    return manifest, rows


class FormalObservationTests(unittest.TestCase):
    def analyze(self, mutate=None):
        self.assertIsNotNone(subject, "Formal production observation analyzer is not implemented")
        manifest, rows = fixture()
        if mutate:
            mutate(manifest, rows)
        return subject.analyze(manifest, rows)

    def test_half_open_window_counts_whole_frames_including_empty_partial(self):
        result = self.analyze()
        self.assertEqual("VALID", result["manager_observation_validity"])
        self.assertEqual(2, result["window_observations"])
        self.assertEqual(.05, result["manager_complete_observation_fps"])
        self.assertEqual({"0": 1, "2": 1}, result["body_count_histogram"])
        self.assertEqual(1, result["zero_body_frames"])
        self.assertEqual("FAIL", result["target_30_fps"])
        self.assertEqual("INCONCLUSIVE", result["native_coverage_validity"])
        self.assertIsNone(result["native_fresh_body_frames"])
        self.assertIsNone(result["decoder_drops"])
        self.assertEqual(100.0, result["publication_age_ms"]["p50"])
        self.assertEqual(120.0, result["source_observation_age_ms"]["p95"])
        self.assertEqual(12, result["max_clock_interval_error_us"])

    def test_internal_native_sequence_gap_is_invalid_and_not_reconstructed(self):
        def gap(manifest, rows):
            rows[7].update(sequence=104, processed_sequence=104)
            rows[9].update(sequence=105, processed_sequence=105)
            rows[-2].update(sequence=105, processed_sequence=105)
            rows[-1].update(end_sequence=105)
        result = self.analyze(gap)
        self.assertEqual("INVALID", result["manager_observation_validity"])
        self.assertIsNone(result["manager_complete_observation_fps"])
        self.assertEqual("INVALID", result["native_coverage_validity"])
        self.assertEqual(.05, result["retained_manager_delivery_fps"])
        self.assertEqual(100, result.get("retained_publication_age_ms", {}).get("p50"))
        self.assertTrue(any("sequence" in item for item in result["invalid_reasons"]))

    def test_missing_prefix_and_suffix_are_invalid(self):
        for change in (lambda m, r: r[0].update(begin_sequence=99),
                       lambda m, r: r[-1].update(end_sequence=105)):
            with self.subTest(change=change):
                self.assertEqual("INVALID", self.analyze(change)["manager_observation_validity"])

    def test_counter_reset_and_duplicate_result_or_source_frame_are_invalid(self):
        for change in (lambda m, r: r[-2].update(submitted=1),
                       lambda m, r: r[7].update(sequence=102),
                       lambda m, r: r[7].update(source_frame_id=2),
                       lambda m, r: r[6].update(frame_id=2)):
            with self.subTest(change=change):
                self.assertEqual("INVALID", self.analyze(change)["manager_observation_validity"])

    def test_full_bodies_and_nonpredicted_joints_required(self):
        for change in (lambda m, r: r[7]["bodies"].pop(),
                       lambda m, r: r[7]["bodies"][0]["canonical_joints"].pop(),
                       lambda m, r: r[7]["bodies"][0]["canonical_joints"][0].update(prediction_ms=1),
                       lambda m, r: r[7]["bodies"][0]["joints"][0].update(px=float("nan"))):
            with self.subTest(change=change):
                self.assertEqual("INVALID", self.analyze(change)["manager_observation_validity"])

    def test_missing_join_source_generation_switch_and_source_gap_invalid(self):
        for change in (lambda m, r: r.pop(6),
                       lambda m, r: r[6].update(generation=2),
                       lambda m, r: r[6].update(frame_id=30)):
            with self.subTest(change=change):
                self.assertEqual("INVALID", self.analyze(change)["manager_observation_validity"])

    def test_clock_quantization_is_included_in_five_ms_limit(self):
        at_limit = self.analyze(lambda m, r: r[7].update(unity_after_us=r[7]["unity_before_us"]+4998))
        self.assertEqual("VALID", at_limit["manager_observation_validity"])
        over = self.analyze(lambda m, r: r[7].update(unity_after_us=r[7]["unity_before_us"]+4999))
        self.assertEqual("INVALID", over["manager_observation_validity"])

    def test_pts_never_used_as_age_and_unmapped_local_decode_age_unavailable(self):
        def change(manifest, rows):
            for row in rows:
                if row["kind"] == "source":
                    row.update(source_clock=2, source_clock_id=44, timestamp_kind=1,
                               source_us=8, pts_us=-5000000000)
        result = self.analyze(change)
        self.assertEqual("VALID", result["manager_observation_validity"])
        self.assertIsNone(result["source_observation_age_ms"]["p50"])
        self.assertEqual(100., result["publication_age_ms"]["p50"])

    def test_short_run_overflow_error_readback_and_missing_identity_invalid(self):
        for change in (lambda m, r: r[-1].update(elapsed_us=59999999),
                       lambda m, r: r[-1].update(overflow=True),
                       lambda m, r: r[-2].update(error=True),
                       lambda m, r: r[-2].update(cpu_readbacks=1),
                       lambda m, r: m.pop("apk_sha256")):
            with self.subTest(change=change):
                self.assertEqual("INVALID", self.analyze(change)["manager_observation_validity"])

    def test_cpu_native_clock_not_subtracted_from_unity(self):
        result = self.analyze(lambda m, r: r[0].update(gpu=False))
        self.assertEqual("INVALID", result["manager_observation_validity"])
        self.assertIsNone(result["publication_age_ms"]["p50"])

    def test_malformed_field_types_fail_closed(self):
        result = self.analyze(lambda m, r: r[6].update(source_us="bad-clock"))
        self.assertEqual("INVALID", result["manager_observation_validity"])

    def test_high_delivery_rate_is_inconclusive_without_native_public_counters(self):
        manifest, rows = fixture()
        manifest["source_rate_hz"] = 60
        header, sample, source, observation, end_sample, footer = rows[0], rows[1], rows[4], rows[5], rows[-2], rows[-1]
        header.update(begin_sequence=0)
        sample.update(sequence=0, submitted=0, processed_sequence=0)
        observations, sources = [], []
        for index in range(1200):
            elapsed = 10000000+index*33333
            new_source = dict(source, frame_id=index+1, elapsed_us=elapsed,
                              published_us=900000000+elapsed-100000,
                              source_us=900000000+elapsed-120000)
            sources.append(new_source)
            observations.append(dict(observation, sequence=index+1, source_frame_id=index+1,
                                     elapsed_us=elapsed, unity_before_us=100000000+elapsed,
                                     unity_after_us=100000010+elapsed, input_now_us=900000000+elapsed,
                                     source_timestamp_us=100000000+elapsed-100000,
                                     submitted=index+1, processed_sequence=index+1))
        end_sample.update(sequence=1200, submitted=1200, processed_sequence=1200)
        footer.update(end_sequence=1200, end_source_frame=1200, observation_count=1200, source_count=1200)
        result = subject.analyze(manifest, [header, sample]+sources+observations+[end_sample, footer])
        self.assertEqual(30, result["manager_complete_observation_fps"])
        self.assertEqual("INCONCLUSIVE", result["target_30_fps"])

    def test_review_bad_clock_rows_do_not_contribute_retained_ages(self):
        def shifted_mapping(manifest, rows):
            rows[7]["unity_before_us"] += 10000000
            rows[7]["unity_after_us"] += 10000000
        changes = [shifted_mapping,
                   lambda m, r: r[6].update(source_us=-1),
                   lambda m, r: r[7].update(source_timestamp_us=-1),
                   lambda m, r: r[6].update(source_clock_id=7),
                   lambda m, r: r[7].update(input_now_us=2**63)]
        for change in changes:
            with self.subTest(change=change):
                result = self.analyze(change)
                self.assertEqual("INVALID", result["manager_observation_validity"])
                for field, expected in (("retained_publication_age_ms", 100.),
                                        ("retained_source_observation_age_ms", 120.),
                                        ("retained_manager_source_age_ms", 100.005)):
                    self.assertEqual(1, result[field]["count"])
                    self.assertAlmostEqual(expected, result[field]["p95"])

    def test_review_invalid_all_source_ids_have_no_retained_age(self):
        def change(manifest, rows):
            for row in rows:
                if row["kind"] == "source":
                    row["source_id"] = -1
        result = self.analyze(change)
        self.assertEqual("INVALID", result["manager_observation_validity"])
        for field in ("retained_publication_age_ms", "retained_source_observation_age_ms",
                      "retained_manager_source_age_ms"):
            self.assertEqual(0, result[field]["count"])
            self.assertIsNone(result[field]["p95"])

    def test_review_publication_and_observation_clock_regression_excludes_bad_source(self):
        for change in (lambda m, r: r[6].update(published_us=1, source_us=0),
                       lambda m, r: r[6].update(source_us=r[4]["source_us"]-1)):
            with self.subTest(change=change):
                result = self.analyze(change)
                self.assertEqual("INVALID", result["manager_observation_validity"])
                self.assertEqual(1, result["retained_publication_age_ms"]["count"])
                self.assertEqual(100., result["retained_publication_age_ms"]["p95"])

    def test_review_changed_clock_origin_or_kind_cannot_mix_age_distributions(self):
        def changed_origin(manifest, rows):
            for row in rows:
                if row["kind"] == "source":
                    row.update(source_clock=2, timestamp_kind=1, source_clock_id=44)
            rows[6]["source_clock_id"] = 45
        for change in (changed_origin, lambda m, r: r[6].update(timestamp_kind=1)):
            with self.subTest(change=change):
                result = self.analyze(change)
                self.assertEqual("INVALID", result["manager_observation_validity"])
                self.assertIsNone(result["retained_publication_age_ms"]["p95"])
                self.assertEqual(0, result["retained_publication_age_ms"]["count"])

    def test_review_typed_public_integer_overflows_rejected(self):
        for kind, field, bad in (("source", "source_id", 2**64),
                                 ("source", "generation", 2**64),
                                 ("source", "resource_token", 2**64),
                                 ("source", "source_clock_id", 2**64),
                                 ("source", "published_us", 2**63),
                                 ("source", "pts_us", -(2**63)-1),
                                 ("source", "width", 2**31),
                                 ("observation", "input_now_us", 2**63),
                                 ("observation", "source_frame_id", 2**63),
                                 ("observation", "body_count", 2**31),
                                 ("header", "begin_sequence", True),
                                 ("footer", "elapsed_us", 2**63),
                                 ("sample", "runtime_drops", 2**63)):
            def change(manifest, rows):
                for row in rows:
                    if row["kind"] == kind:
                        row[field] = bad
            with self.subTest(kind=kind, field=field, bad=bad):
                self.assertEqual("INVALID", self.analyze(change)["manager_observation_validity"])

    def test_review_exact_integer_bounds_equal_clocks_and_regressing_pts_allowed(self):
        def change(manifest, rows):
            for row in rows:
                if row["kind"] == "source":
                    row.update(source_id=2**64-1, generation=2**64-1, resource_token=2**64-1,
                               source_clock=2, timestamp_kind=1, source_clock_id=2**64-1)
            rows[4]["pts_us"] = 2**63-1
            rows[6].update(published_us=rows[4]["published_us"], source_us=rows[4]["source_us"],
                           pts_us=-(2**63))
        self.assertEqual("VALID", self.analyze(change)["manager_observation_validity"])

    def test_review_body_and_joint_integer_overflows_are_not_retained(self):
        for change in (lambda m, r: r[7]["bodies"][0].update(track_id=2**31),
                       lambda m, r: r[7]["bodies"][0].update(stable_track_id=2**63),
                       lambda m, r: r[7]["bodies"][0].update(region_index=2**31),
                       lambda m, r: r[7]["bodies"][0]["canonical_joints"][0].update(observation_us=2**63)):
            with self.subTest(change=change):
                result = self.analyze(change)
                self.assertEqual("INVALID", result["manager_observation_validity"])
                self.assertEqual(1, result["retained_publication_age_ms"]["count"])

    def test_review_input_clock_cannot_map_age_before_unity_origin(self):
        result = self.analyze(lambda m, r: r[7].update(input_now_us=2**63-1))
        self.assertEqual("INVALID", result["manager_observation_validity"])
        self.assertEqual(1, result["retained_publication_age_ms"]["count"])
        self.assertEqual(100., result["retained_publication_age_ms"]["p95"])

    def test_review_input_monotonic_now_and_epoch_mapping_cannot_regress_or_change(self):
        for change in (lambda m, r: r[7].update(input_now_us=r[5]["input_now_us"]-1),
                       lambda m, r: r[7].update(input_now_us=r[7]["input_now_us"]+10000)):
            with self.subTest(change=change):
                result = self.analyze(change)
                self.assertEqual("INVALID", result["manager_observation_validity"])
                self.assertEqual(1, result["retained_publication_age_ms"]["count"])

    def test_review_integer_signed_bounds_for_body_ids_are_exact(self):
        def change(manifest, rows):
            for body in rows[7]["bodies"]:
                body.update(track_id=2**31-1, stable_track_id=2**63-1, region_index=2**31-1)
        self.assertEqual("VALID", self.analyze(change)["manager_observation_validity"])

    def test_review_raw_delivery_count_independent_of_rejected_clock_age(self):
        result = self.analyze(lambda m, r: r[7].update(unity_after_us=r[7]["unity_before_us"]+4999))
        self.assertEqual("INVALID", result["manager_observation_validity"])
        self.assertIsNone(result["manager_complete_observation_fps"])
        self.assertEqual(2, result["window_observations"])
        self.assertEqual({"0": 1, "2": 1}, result["body_count_histogram"])
        self.assertEqual(.05, result["retained_manager_delivery_fps"])
        self.assertEqual(1, result["retained_publication_age_ms"]["count"])
        self.assertEqual("INVALID", result["target_30_fps"])

    def test_review_raw_delivery_count_independent_of_source_age_join_or_clock_fields(self):
        for change in (lambda m, r: r[6].update(source_us=-1),
                       lambda m, r: r[7].update(input_now_us=2**63),
                       lambda m, r: r.pop(6)):
            with self.subTest(change=change):
                result = self.analyze(change)
                self.assertEqual("INVALID", result["manager_observation_validity"])
                self.assertEqual(.05, result["retained_manager_delivery_fps"])
                self.assertEqual(1, result["retained_publication_age_ms"]["count"])

    def test_review_raw_delivery_requires_valid_event_placement_and_full_body_data(self):
        for change in (lambda m, r: r[7].update(unity_before_us=r[7]["unity_before_us"]+10000000),
                       lambda m, r: r[7]["bodies"].pop(),
                       lambda m, r: r[7].update(source_frame_id=2**63)):
            with self.subTest(change=change):
                result = self.analyze(change)
                self.assertEqual("INVALID", result["manager_observation_validity"])
                self.assertEqual(.025, result["retained_manager_delivery_fps"])

    def test_review_positive_readback_errors_rejected_even_when_summary_false(self):
        result = self.analyze(lambda m, r: r[-2].update(readback_errors=1, error=False))
        self.assertEqual("INVALID", result["manager_observation_validity"])
        self.assertIsNone(result["manager_complete_observation_fps"])


if __name__ == "__main__":
    unittest.main()
