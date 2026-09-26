import copy
import json
import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch

from tools.test.android_topdown_gate_analysis import analyze
from tools.test.prepare_android_topdown_eval import prepare
from tools.test.summarize_android_topdown_gate import summarize


def valid_evidence():
    observations = [dict(id=i, source_frame_id=i, t_ms=i * (1000 / 30),
                         body_count=1, native_body_count=1, drawn_body_count=1,
                         rendered_body_count=1,
                         source_presentation_lag=0, region_revision=7,
                         age_ms=60, sensor_age_ms=70) for i in range(1800)]
    return dict(schema_version=1, interval=2, expected_interval=2,
                capacity=1, apk_capacity=1, configured_people=1,
                runtime_max_bodies=1, region_count=1,
                apk_sha256='a' * 64, expected_apk_sha256='a' * 64,
                profile_sha256='b' * 64, expected_profile_sha256='b' * 64,
                model_sha256={'detector':'c'*64,'body':'d'*64},
                expected_model_sha256={'detector':'c'*64,'body':'d'*64},
                native_library_sha256='e'*64, expected_native_library_sha256='e'*64,
                backend='NCNN Vulkan', copy_path='AHB Vulkan', capture_provenance='SENSOR_VERIFIED',
                sensor_capture_verified=True, cpu_full_frame_readbacks=0,
                same_pid_at_end=True, install_identity_verified=True,
                stream_ready_log_epoch=100.0, source_kind='live_camera',
                serial='e7c07019', device_fingerprint='device/build',
                package='com.example.i2c1', pid=12, renderer_evidence_verified=True,
                device_failures=[],
                runtime_telemetry=[dict(interval=2, capacity=1, configured_people=1,
                    max_bodies=1, backend='NCNN Vulkan', copy_path='AHB Vulkan')],
                runtime_backend_samples=['NCNN Vulkan'],
                gpu_capture_requested=1800, bridge_no_free_slot_drops=0,
                bridge_superseded_ready_drops=0, gpu_copy_errors=0, gpu_import_errors=0,
                max_overlay_lag_frames=10, detector_max_capture_gap_ms=180,
                new_track_detector_age_ms=120, region_revision=7,
                visible_person_count=1, warmup_seconds=5, duration_seconds=60,
                observations=observations, thermal=dict(duration_seconds=900,
                backend='NCNN Vulkan', frequency_samples=[1000,950], timing_samples_ms=[65,72],
                capacity=1, interval=2, apk_sha256='a'*64,
                serial='e7c07019', device_fingerprint='device/build',
                package='com.example.i2c1', pid=12, same_pid_at_end=True,
                source_kind='live_camera', log_start_epoch=170, log_end_epoch=1070,
                logcat_sha256='f'*64, evidence_verified=True,
                samples=[dict(t_seconds=t, gpu_frequency_mhz=1000 if t == 0 else 950,
                    fresh_frames_1s=30, fresh_fps_rolling_10s=30, fresh_fps_rolling_60s=30,
                    sensor_age_p50_ms=55, sensor_age_p95_ms=75,
                    max_sensor_age_ms=100, bridge_drop_fraction=0,
                    copy_errors=0, import_errors=0, cpu_full_frame_readbacks=0,
                    visible_person_count=1, body_count=1, native_body_count=1,
                    drawn_body_count=1, rendered_body_count=1,
                    interval=2, capacity=1, configured_people=1,
                    max_bodies=1, copy_path='AHB Vulkan',
                    backend='NCNN Vulkan', capture_provenance='SENSOR_VERIFIED')
                    for t in range(901)]))


class GateAnalysisTests(unittest.TestCase):
    def test_valid_fixture(self):
        self.assertEqual(analyze(valid_evidence())['result'], 'PROVISIONAL_PASS')
        candidate = valid_evidence()
        candidate.pop('thermal')
        self.assertTrue(analyze(candidate)['timing_eligible'])
        self.assertEqual(analyze(candidate)['result'], 'FAIL')

    def test_rejects_contract_violations(self):
        cases = {
            'duplicate observation': lambda x: x['observations'][1].update(id=0),
            'body summed fps': lambda x: x.update(reported_fresh_fps=60),
            'detector gap': lambda x: x.update(detector_max_capture_gap_ms=201),
            'detector age': lambda x: x.update(new_track_detector_age_ms=201),
            'model hash drift': lambda x: x['model_sha256'].update(body='f'*64),
            'ORT fallback': lambda x: x.update(backend='ORT CPU'),
            'CPU readback': lambda x: x.update(cpu_full_frame_readbacks=1),
            'stale Region': lambda x: x['observations'][0].update(region_revision=6),
            'bridge drops': lambda x: x.update(bridge_no_free_slot_drops=19),
            'P95 age': lambda x: [o.update(sensor_age_ms=101) for o in x['observations']],
            'FPS': lambda x: x.update(observations=x['observations'][:1750]),
            'missing thermal': lambda x: x.pop('thermal'),
            'empty visible scene': lambda x: [o.update(body_count=0,native_body_count=0,drawn_body_count=0) for o in x['observations']],
            'person discovery late': lambda x: [o.update(body_count=0,native_body_count=0,drawn_body_count=0) for o in x['observations'][:10]],
            'intermittent visible dropout': lambda x: x['observations'][900].update(body_count=0,native_body_count=0,drawn_body_count=0),
            'hidden native bodies': lambda x: [o.update(drawn_body_count=0,source_presentation_lag=11) for o in x['observations']],
            'facade slots without rendered mesh': lambda x: [o.update(rendered_body_count=0) for o in x['observations']],
            'missing renderer proof': lambda x: x.pop('renderer_evidence_verified'),
            'unverified sensor time': lambda x: x.update(capture_provenance='UNITY_OBSERVED',sensor_capture_verified=False),
            'persisted capacity mismatch': lambda x: x.update(configured_people=4),
            'runtime capacity mismatch': lambda x: x.update(runtime_max_bodies=4),
            'APK capacity mismatch': lambda x: x.update(apk_capacity=2),
            'Region count mismatch': lambda x: x.update(region_count=4),
            'capacity two only one visible': lambda x: [x['thermal'].update(capacity=2), x.update(capacity=2,apk_capacity=2,
                configured_people=2,runtime_max_bodies=2,region_count=2,
                runtime_telemetry=[dict(interval=2,capacity=2,configured_people=2,max_bodies=2,
                    backend='NCNN Vulkan',copy_path='AHB Vulkan')])],
            'capacity two draws only one': lambda x: [x['thermal'].update(capacity=2), x.update(capacity=2,apk_capacity=2,
                configured_people=2,runtime_max_bodies=2,region_count=2,visible_person_count=2,
                runtime_telemetry=[dict(interval=2,capacity=2,configured_people=2,max_bodies=2,
                    backend='NCNN Vulkan',copy_path='AHB Vulkan')]) or
                [o.update(body_count=2,native_body_count=2,drawn_body_count=1) for o in x['observations']]],
            'runtime interval drift': lambda x: x['runtime_telemetry'].append(dict(
                interval=4,capacity=1,configured_people=1,max_bodies=1,
                backend='NCNN Vulkan',copy_path='AHB Vulkan')),
            'runtime backend drift': lambda x: x['runtime_telemetry'].append(dict(
                interval=2,capacity=1,configured_people=1,max_bodies=1,
                backend='ORT CPU',copy_path='AHB Vulkan')),
            'thermal throttling with FPS loss': lambda x: x['thermal']['samples'][-1].update(gpu_frequency_mhz=200,fresh_fps_rolling_10s=15),
            'thermal timing degraded': lambda x: x['thermal']['samples'][-1].update(fresh_fps_rolling_10s=15),
            'thermal 60 second rate degraded': lambda x: x['thermal']['samples'][-1].update(fresh_fps_rolling_60s=20),
            'thermal invalid frequency': lambda x: x['thermal']['samples'][-1].update(gpu_frequency_mhz=-1),
            'thermal missing samples': lambda x: x['thermal'].pop('samples'),
            'thermal gap': lambda x: x['thermal']['samples'].pop(500),
            'thermal age degraded': lambda x: x['thermal']['samples'][-1].update(sensor_age_p95_ms=101),
            'thermal APK drift': lambda x: x['thermal'].update(apk_sha256='f'*64),
            'thermal stale PID': lambda x: x['thermal'].update(pid=999),
            'thermal other device': lambda x: x['thermal'].update(serial='other-device'),
            'thermal stale timeline': lambda x: x['thermal'].update(log_start_epoch=50),
            'thermal missing log hash': lambda x: x['thermal'].pop('logcat_sha256'),
            'thermal unverified collector': lambda x: x['thermal'].update(evidence_verified=False),
            'thermal and run missing PID': lambda x: [x.pop('pid'), x['thermal'].pop('pid')],
            'thermal and run empty serial': lambda x: [x.update(serial=''), x['thermal'].update(serial='')],
            'thermal and run missing fingerprint': lambda x: [x.pop('device_fingerprint'), x['thermal'].pop('device_fingerprint')],
            'thermal and run missing package': lambda x: [x.pop('package'), x['thermal'].pop('package')],
            'missing Region revision at both levels': lambda x: [x.update(region_revision=None)] +
                [o.update(region_revision=None) for o in x['observations']],
            'source frames swapped': lambda x: [x['observations'][0].update(source_frame_id=1),
                x['observations'][1].update(source_frame_id=0)],
            'result sequences swapped': lambda x: [x['observations'][0].update(id=1),
                x['observations'][1].update(id=0)],
            'thermal empty while person visible': lambda x: [s.update(
                native_body_count=0, drawn_body_count=0) for s in x['thermal']['samples']],
            'thermal overlay hides one body': lambda x: x['thermal']['samples'][-1].update(drawn_body_count=0),
            'thermal mesh hidden': lambda x: x['thermal']['samples'][-1].update(rendered_body_count=0),
            'thermal interval drift': lambda x: x['thermal']['samples'][-1].update(interval=6),
            'thermal capacity drift': lambda x: x['thermal']['samples'][-1].update(capacity=2,max_bodies=2,configured_people=2),
            'native worker fatal': lambda x: x.update(device_failures=['Detector output contract invalid']),
            'package PID changed': lambda x: x.update(same_pid_at_end=False),
            'missing visible annotation': lambda x: x.update(visible_person_count=None),
            'skip install unverified': lambda x: x.update(install_identity_verified=False),
            'missing install identity': lambda x: x.pop('install_identity_verified'),
            'missing PID continuity': lambda x: x.pop('same_pid_at_end'),
            'missing stream readiness': lambda x: x.pop('stream_ready_log_epoch'),
            'missing Region revision': lambda x: x.pop('region_revision'),
            'video source in camera gate': lambda x: x.update(source_kind='video_diagnostic'),
        }
        for name, mutation in cases.items():
            with self.subTest(name=name):
                x = copy.deepcopy(valid_evidence())
                mutation(x)
                self.assertEqual(analyze(x)['result'], 'FAIL', name)

    def test_rolling_window_catches_exit_boundary(self):
        evidence = valid_evidence()
        times = [i * 10000 / 290 for i in range(290)] + [10001 + i * 1000 / 30 for i in range(1500)]
        evidence['observations'] = evidence['observations'][:len(times)]
        for observation, timestamp in zip(evidence['observations'], times):
            observation['t_ms'] = timestamp
        result = analyze(evidence)
        self.assertEqual(result['metrics']['min_rolling_10s_fps'], 28.9)
        self.assertIn('fresh_fps_rolling_10s', result['failures'])

    def test_one_second_result_freeze_is_reported(self):
        evidence = valid_evidence()
        evidence['observations'] = [o for o in evidence['observations']
            if not 20000 <= o['t_ms'] < 21034]
        self.assertIn('one_second_result_freeze', analyze(evidence)['failures'])

    def test_required_fields_fail_closed_when_missing_or_malformed(self):
        for field in ('pid','serial','device_fingerprint','package','region_revision',
                      'runtime_telemetry','runtime_backend_samples','renderer_evidence_verified',
                      'source_kind','stream_ready_log_epoch','capacity','interval',
                      'apk_sha256','profile_sha256','native_library_sha256',
                      'model_sha256','observations','thermal'):
            with self.subTest(level='run', field=field):
                evidence = valid_evidence()
                evidence.pop(field)
                self.assertEqual(analyze(evidence)['result'], 'FAIL')
        for field in ('id','source_frame_id','t_ms','region_revision','body_count',
                      'native_body_count','drawn_body_count','rendered_body_count',
                      'sensor_age_ms'):
            with self.subTest(level='observation', field=field):
                evidence = valid_evidence()
                evidence['observations'][5].pop(field)
                self.assertEqual(analyze(evidence)['result'], 'FAIL')
        for field in ('interval','capacity','configured_people','max_bodies',
                      'visible_person_count','body_count','native_body_count',
                      'drawn_body_count','rendered_body_count','copy_path','backend',
                      'capture_provenance','fresh_frames_1s','fresh_fps_rolling_10s',
                      'fresh_fps_rolling_60s','sensor_age_p50_ms','sensor_age_p95_ms',
                      'max_sensor_age_ms','bridge_drop_fraction','copy_errors',
                      'import_errors','cpu_full_frame_readbacks','gpu_frequency_mhz',
                      't_seconds'):
            with self.subTest(level='thermal_sample', field=field):
                evidence = valid_evidence()
                evidence['thermal']['samples'][-1].pop(field)
                self.assertEqual(analyze(evidence)['result'], 'FAIL')
        for field, bad in (('duration_seconds','bad'), ('warmup_seconds','bad'),
                           ('observations',[None])):
            with self.subTest(level='malformed', field=field):
                evidence = valid_evidence()
                evidence[field] = bad
                self.assertEqual(analyze(evidence)['result'], 'FAIL')
        evidence = valid_evidence()
        evidence['thermal']['duration_seconds'] = 'bad'
        self.assertEqual(analyze(evidence)['result'], 'FAIL')

    def test_live_overlay_does_not_use_file_playback_frame_lag(self):
        evidence = valid_evidence()
        for observation in evidence['observations']:
            observation['source_presentation_lag'] = 100
        self.assertEqual(analyze(evidence)['result'], 'PROVISIONAL_PASS')

    def test_device_summary_preserves_runtime_failures(self):
        report = dict(interval=2, capacity=1, apk_sha256='a'*64,
            hashes=dict(interval=2,capacity=1,apk_sha256='a'*64,
                profile_sha256='b'*64,libhumanvision_sha256='e'*64,
                detector_sha256='c'*64,body_sha256='d'*64),
            intended_duration_seconds=65, package='com.example.i2c1', pid=12,
            same_pid_at_end=True, fatal_lines=['FATAL EXCEPTION: Vulkan device lost'],
            log_lines=[' 100.000  12  18 I Unity   : HV_TOPDOWN_BOOT interval=2 capacity=1 saved_people=1'])
        self.assertTrue(summarize(report)['device_failures'])

    def test_eval_video_teardown_drains_gpu_lease_before_texture_release(self):
        source = (Path(__file__).parent / 'TopDownEvalVideoSource.cs').read_text(encoding='utf-8')
        self.assertIn('private void OnDisable() { ReleaseVideoResources(); }', source)
        self.assertIn('private void OnDestroy() { ReleaseVideoResources(); }', source)
        teardown = source.split('private void ReleaseVideoResources()', 1)[1]
        self.assertLess(teardown.index('EndAndroidGpuSourceLease()'),
                        teardown.index('_texture.Release()'))

    def test_interval_profiles_are_distinct_and_deterministic(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            pack = temp / 'pack'
            for role in ('detector', 'body'):
                (pack / role).mkdir(parents=True)
                (pack / role / 'model.bin').write_bytes(role.encode())
            (pack / 'modelpack.json').write_text('{"profile_sha256":"baseline"}', encoding='utf-8')
            with patch('tools.test.prepare_android_topdown_eval.verify_pack'):
                hashes = []
                for interval in range(2, 7):
                    output = temp / str(interval)
                    record = prepare(pack, root / 'profiles/android-ncnn-vulkan.json', output, interval)
                    self.assertEqual(json.loads((output / 'profiles/android-ncnn-vulkan.json').read_text())['detector']['cadence_interval_frames'], interval)
                    hashes.append(record['profile_sha256'])
                self.assertEqual(len(set(hashes)), 5)
                again = prepare(pack, root / 'profiles/android-ncnn-vulkan.json', temp / 'repeat', 4)
                self.assertEqual(again['profile_sha256'], hashes[2])

    def test_device_summary_never_promotes_observed_clock_or_saved_capacity(self):
        hashes = dict(interval=4, capacity=1, apk_sha256='a'*64,
            profile_sha256='b'*64, libhumanvision_sha256='e'*64,
            detector_sha256='c'*64, body_sha256='d'*64)
        report = dict(interval=4, capacity=1, apk_sha256='a'*64,
            hashes=hashes, intended_duration_seconds=65,
            annotated_visible_person_count=1, package='com.example.i4c1', pid=12,
            same_pid_at_end=True, log_lines=[
                ' 100.000  12  18 I Unity   : HV_TOPDOWN_BOOT interval=4 capacity=1 saved_people=1',
                ' 105.000  12  18 I Unity   : HV_TOPDOWN_STATS configured_people=4 max_bodies=4 region_count=4 capture_requested=0 bridge_no_slot=0 bridge_superseded=0 copy_errors=0 import_errors=0 provenance=1 copy_path=1',
                ' 106.000  12  18 I Unity   : HV_TOPDOWN_STATS configured_people=4 max_bodies=4 region_count=4 capture_requested=30 bridge_no_slot=0 bridge_superseded=0 copy_errors=0 import_errors=0 provenance=1 copy_path=1',
                ' 106.010  12  18 I Unity   : HV_TOPDOWN_NATIVE Actual backend=backend.ncnn.vulkan'])
        evidence = summarize(report)
        self.assertEqual(evidence['configured_people'], 4)
        self.assertEqual(evidence['capture_provenance'], 'UNITY_OBSERVED')
        self.assertFalse(evidence['sensor_capture_verified'])
        self.assertIn('capacity_binding', analyze(evidence)['failures'])
        self.assertIn('sensor_capture_provenance', analyze(evidence)['failures'])


if __name__ == '__main__':
    unittest.main()
