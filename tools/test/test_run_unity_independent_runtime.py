"""Exercise runner validation and cleanup with file logs and a local REST boundary stub."""
import contextlib
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
from urllib.parse import urlsplit

import run_unity_independent_runtime as runner


ROOT = Path(__file__).resolve().parents[2]


def sample(second, **changes):
    row = {
        'kind': 'sample', 'elapsed_us': int(second * 1_000_000),
        'source_id': 1, 'generation': 1, 'config_epoch': 1, 'region_revision': 1,
        'frame_id': int(second * 10), 'native_input_frames': int(second * 30),
        'native_fresh_frames': int(second * 10), 'unity_callbacks': int(second * 30),
        'unity_deliveries': int(second * 30), 'unity_renders': int(second * 30),
        'input_errors': 0, 'runtime_errors': 0, 'result_age_us': 1000,
        'actual_source': 'input.rtsp.native', 'actual_image_service': 'image.cpu',
        'actual_backend': 'backend.ort.cpu', 'execution_mode': 'native',
        'capability_level': 'native', 'preview_enabled': True, 'target_fps': 30,
        'streaming': True, 'stall_requested_ms': 0, 'gpu_load_passes': 0,
        'cpu_load_workers': 0,
    }
    row.update(changes)
    return row


class Response:
    def __init__(self, value):
        self.value = value

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        pass

    def read(self):
        return json.dumps(self.value).encode()


class EditorBoundary:
    """Keep filesystem effects real; replace only the external Unity REST service."""
    def __init__(self, project, diagnostics, rows, console_errors, console_failure):
        self.project, self.diagnostics, self.rows = project, diagnostics, rows
        self.console_errors, self.console_failure = console_errors, console_failure

    def open(self, request, timeout):
        path = urlsplit(request.full_url).path
        if path == '/health':
            return Response({'status': 'ok', 'projectName': 'Human-Vision-SDK-Test'})
        if path == '/skill/scene_get_info':
            return Response({'status': 'success', 'result': {'sceneName': 'RuntimePrefabAcceptance', 'isDirty': False}})
        if path == '/skill/editor_get_state':
            return Response({'status': 'success', 'result': {'isPlaying': False, 'isCompiling': False}})
        if path == '/skill/editor_play':
            # The real Demo consumes the fixture, while the probe config persists.
            (self.project / 'Library/HumanVisionAcceptanceFixture.txt').unlink()
            text = '\n'.join('HV_INDEPENDENT_PROGRESS ' + json.dumps(row) for row in self.rows)
            (self.diagnostics / 'independent-progress-test.log').write_text(text + '\n', encoding='utf-8')
            return Response({'status': 'success', 'result': {'success': True, 'mode': 'playing'}})
        if path == '/skill/editor_stop':
            return Response({'status': 'success', 'result': {'success': True, 'mode': 'stopped'}})
        if path == '/skill/console_get_logs':
            if self.console_failure:
                raise OSError('diagnostics unavailable')
            return Response({'status': 'success', 'result': {'count': self.console_errors,
                'logs': [{'message': 'native failure'}] * self.console_errors, 'source': 'console'}})
        raise AssertionError('Unexpected external operation: ' + path)


class UnityIndependentRunnerTests(unittest.TestCase):
    def run_case(self, rows=None, *, extra=(), console_errors=0, console_failure=False, original=None):
        # Name the break: bad logs must change the actual CLI result; cleanup must
        # restore on-disk state even if the external diagnostics service fails.
        with tempfile.TemporaryDirectory(prefix='unity-runner-test-', dir=ROOT / 'out') as directory:
            root = Path(directory)
            project, diagnostics, output = root / 'Human-Vision-SDK-Test', root / 'diagnostics', root / 'receipt'
            (project / 'Library').mkdir(parents=True)
            (project / 'Assets/Plugins/x86_64').mkdir(parents=True)
            diagnostics.mkdir()
            source = root / 'fixture.mp4'
            source.write_bytes(b'only hashed, never decoded by this validation test')
            files = [project / 'Library/HumanVisionAcceptanceFixture.txt', project / 'Library/HumanVisionIndependentProbe.txt']
            if original is not None:
                for file, content in zip(files, original):
                    file.write_bytes(content)
            boundary = EditorBoundary(project, diagnostics,
                copy.deepcopy(rows if rows is not None else [sample(i) for i in range(1, 13)]),
                console_errors, console_failure)
            argv = ['runner', '--project', str(project), '--output', str(output), '--source', str(source),
                '--fixture-url', 'rtsp://127.0.0.1/fixture', '--diagnostics-root', str(diagnostics),
                '--seconds', '10', '--warmup', '0', *extra]
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(runner.urllib.request, 'build_opener', return_value=boundary), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                try:
                    code = runner.main()
                except Exception as error:
                    code = error
            receipt = json.loads((output / 'receipt.json').read_text()) if (output / 'receipt.json').exists() else None
            restored = [file.read_bytes() if file.exists() else None for file in files]
            return code, receipt, restored

    def assert_invalid(self, rows=None, **kwargs):
        code, receipt, _ = self.run_case(rows, **kwargs)
        self.assertNotEqual(receipt['summary']['status'], 'observed')
        self.assertEqual(code, 2)

    def test_accepts_steady_native_progress(self):
        code, receipt, _ = self.run_case()
        self.assertEqual(code, 0)
        self.assertEqual(receipt['summary']['status'], 'observed')
        self.assertEqual(receipt['summary']['duration_us'], 10_000_000)
        self.assertEqual(receipt['summary']['fps']['native_fresh_frames'], 10)

    def test_missing_requested_stall_fails(self):
        self.assert_invalid(extra=('--stall-ms', '2000'))

    def test_accepts_observed_stall_without_unity_consumption(self):
        rows = [sample(i, stall_requested_ms=2000) for i in range(1, 5)]
        rows += [sample(4.1, kind='stall_begin', stall_requested_ms=2000,
                unity_callbacks=123, unity_deliveries=123, unity_renders=123),
            sample(6.101, kind='stall_end', stall_requested_ms=2000,
                unity_callbacks=123, unity_deliveries=123, unity_renders=123)]
        rows += [sample(i, stall_requested_ms=2000) for i in range(7, 13)]
        code, receipt, _ = self.run_case(rows, extra=('--stall-ms', '2000'))
        self.assertEqual(code, 0)
        self.assertEqual(receipt['summary']['stalls'][0]['status'], 'observed')

    def test_stall_with_unity_consumption_fails(self):
        rows = [sample(i, stall_requested_ms=2000) for i in range(1, 5)]
        rows += [sample(4.1, kind='stall_begin', stall_requested_ms=2000),
            sample(6.101, kind='stall_end', stall_requested_ms=2000)]
        rows += [sample(i, stall_requested_ms=2000) for i in range(7, 13)]
        self.assert_invalid(rows, extra=('--stall-ms', '2000'))

    def test_wrong_requested_controls_fail_without_a_stall(self):
        for changes in ({'execution_mode': 'compatible'}, {'target_fps': 60},
                        {'preview_enabled': False}, {'actual_backend': 'backend.ncnn.vulkan'},
                        {'gpu_load_passes': 2}, {'cpu_load_workers': 1}):
            with self.subTest(changes=changes):
                self.assert_invalid([sample(i, **changes) for i in range(1, 13)])

    def test_intermediate_epoch_change_is_not_hidden_by_matching_endpoints(self):
        rows = [sample(i) for i in range(1, 13)]
        rows[5]['generation'] = 2
        self.assert_invalid(rows)

    def test_intermediate_counter_reset_fails(self):
        rows = [sample(i) for i in range(1, 13)]
        rows[5]['native_fresh_frames'] = 0
        self.assert_invalid(rows)

    def test_native_errors_during_window_fail(self):
        for field in ('input_errors', 'runtime_errors'):
            with self.subTest(field=field):
                self.assert_invalid([sample(i, **{field: int(i >= 5)}) for i in range(1, 13)])

    def test_console_errors_fail(self):
        self.assert_invalid(console_errors=1)

    def test_no_fresh_publication_in_the_window_fails(self):
        self.assert_invalid([sample(i, native_fresh_frames=10, frame_id=10) for i in range(1, 13)])

    def test_new_configuration_is_removed(self):
        _, _, restored = self.run_case()
        self.assertEqual(restored, [None, None])

    def test_diagnostics_failure_still_removes_new_configuration_and_writes_receipt(self):
        code, receipt, restored = self.run_case(console_failure=True)
        self.assertEqual(restored, [None, None])
        self.assertEqual(code, 2)
        self.assertIsNotNone(receipt)

    def test_diagnostics_failure_still_restores_existing_configuration(self):
        original = [b'old fixture\n', b'old probe configuration\n']
        _, _, restored = self.run_case(console_failure=True, original=original)
        self.assertEqual(restored, original)


if __name__ == '__main__':
    unittest.main()
