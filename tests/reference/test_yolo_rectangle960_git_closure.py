"""Exercise Git's actual clean/index pipeline without touching the real index."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FROZEN = (
    'tools/models/ncnn/yolo_rectangle960_gate_evidence.json',
    'tools/models/ncnn/yolo_rectangle960_golden_runner.cpp',
    'tools/models/ncnn/yolo_rectangle960_runner/CMakeLists.txt',
)


class Rectangle960GitClosureTests(unittest.TestCase):
    def test_git_stores_and_checks_out_exact_frozen_evidence_and_recipe_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            env = os.environ.copy()
            env['GIT_INDEX_FILE'] = str(Path(temporary) / 'isolated.index')

            def git(*args):
                return subprocess.run(
                    ['git', *args], cwd=ROOT, env=env, check=True,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                ).stdout

            git('read-tree', '--empty')
            git('add', '--', '.gitattributes', *FROZEN)
            attributes = git('check-attr', '--cached', 'text', 'whitespace', '--', *FROZEN).decode()
            for relative in FROZEN:
                with self.subTest(file=relative):
                    expected = (ROOT / relative).read_bytes()
                    stored = git('cat-file', 'blob', ':' + relative)
                    self.assertEqual(
                        hashlib.sha256(stored).hexdigest(), hashlib.sha256(expected).hexdigest(),
                        'Git clean conversion changed a frozen source/evidence identity',
                    )
                    self.assertEqual(stored, expected)
                    self.assertIn(relative + ': text: unset', attributes)
                    self.assertIn(relative + ': whitespace: cr-at-eol', attributes)

            checkout = Path(temporary) / 'checkout'
            checkout.mkdir()
            git('checkout-index', '--prefix=' + checkout.as_posix() + '/', '--', *FROZEN)
            for relative in FROZEN:
                with self.subTest(checkout=relative):
                    self.assertEqual((checkout / relative).read_bytes(), (ROOT / relative).read_bytes())


if __name__ == '__main__':
    unittest.main()
