import pathlib, re, subprocess, sys, tempfile, unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
ANALYZER = ROOT / 'tools/test/analyze_android_input_gate.py'
ARCHIVE = ROOT / 'out/input/task7-device/20261002T1111585811869Z/device-logcat.txt'


class AccountingTests(unittest.TestCase):
    def analyze(self, suffix='', raw=None):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder)
            (path / 'device-logcat.txt').write_text((raw if raw is not None else ARCHIVE.read_text(encoding='utf-8', errors='replace')) + suffix, encoding='utf-8')
            result = subprocess.run([sys.executable, str(ANALYZER), folder], capture_output=True, text=True)
            return result.returncode, result.stdout

    def test_balanced_real_archive(self):
        self.assertEqual(self.analyze()[0], 0)

    def test_early_zero_final_decoder_leak(self):
        self.assertNotEqual(self.analyze('\ndecoder_resources_closed=true active_images=1 active_ahb_references=1 active_owned_fds=1\n')[0], 0)

    def test_unmatched_release_hold(self):
        self.assertNotEqual(self.analyze('\nrelease_fd_held=188 active_owned_fds=1\n')[0], 0)

    def test_negative_owned_count(self):
        self.assertNotEqual(self.analyze('\ndecoded_lease_returned=true active_images=0 active_ahb_references=0 active_owned_fds=-1\n')[0], 0)

    def test_repeated_fd_reuse_balanced(self):
        raw = re.sub(r'(release_fd_(?:held|transferred_to_AImage)=)\d+', r'\g<1>188', ARCHIVE.read_text(encoding='utf-8', errors='replace'))
        self.assertEqual(self.analyze(raw=raw)[0], 0)

    def test_transfer_without_hold(self):
        self.assertNotEqual(self.analyze('\nrelease_fd_transferred_to_AImage=188 active_owned_fds=0\n')[0], 0)

    def test_duplicate_hold_before_transfer(self):
        self.assertNotEqual(self.analyze('\nrelease_fd_held=188 active_owned_fds=1\nrelease_fd_held=188 active_owned_fds=2\nrelease_fd_transferred_to_AImage=188 active_owned_fds=1\n')[0], 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
