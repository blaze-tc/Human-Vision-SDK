from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/package'))
from stage_rknn_folded_candidate import validate, stage


class FoldedStagingTests(unittest.TestCase):
    def test_real_candidate_and_immutable_receipts(self):
        files = validate(ROOT/'out/four-path-20261011/folded-fp',
                         ROOT/'out/four-path-20261011/folded-output.receipt.json')
        self.assertEqual(len(files), 4)

    def test_changed_graph_receipt_is_rejected_before_runtime_mutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            graph = root/'graph.json'
            graph.write_text('{"deployment_ready":true}')
            runtime = root/'runtime'
            with self.assertRaises(ValueError):
                stage(runtime, ROOT/'out/four-path-20261011/folded-fp', graph)
            self.assertFalse(runtime.exists())


if __name__ == '__main__':
    unittest.main()
