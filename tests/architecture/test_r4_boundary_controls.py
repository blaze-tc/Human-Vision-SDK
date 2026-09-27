import json
import unittest
from tools.test.verify_r4_boundary_controls import NAMES, STAGES, verify


class BoundaryControlEvidenceTests(unittest.TestCase):
    def evidence(self):
        lines=[]
        for stage in STAGES:
            lines.append('I HV_R4_PARITY: '+json.dumps({'stage':stage,'generation':1,
                'source_id':4,'slot':0,'passed':True,'mismatches':0,'max':0,'elements':60},separators=(',',':')))
            for name in NAMES:
                mismatch,first,error=(0,4294967295,0) if name=='clean' else (1,5,100)
                lines.append(f'I HV_R4_PARITY: gpu_boundary_control stage={stage} name={name} pass=1 generation=1 source_id=4 slot=0 baseline_clean=1 count=60 mismatches={mismatch} first={first} max={error}')
        return '\n'.join(lines)

    def test_complete_same_frame_evidence(self):
        self.assertEqual(len(verify(self.evidence())),24)

    def test_different_frame_cannot_prove_prior_boundary(self):
        with self.assertRaisesRegex(ValueError,'Missing clean same-frame'):
            verify(self.evidence().replace('"source_id":4','"source_id":5',1))

    def test_failure_label_cannot_replace_actual_mismatch(self):
        with self.assertRaisesRegex(ValueError,'did not fail'):
            verify(self.evidence().replace('mismatches=1 first=5 max=100','mismatches=0 first=5 max=100',1))

    def test_scheduled_controls_require_each_own_clean_preceding_frame(self):
        lines=[]
        for index,line in enumerate(x for x in self.evidence().splitlines() if 'gpu_boundary_control ' in x):
            source=index+10
            for stage in STAGES:
                lines.append('I HV_R4_PARITY: '+json.dumps({'stage':stage,'generation':1,
                    'source_id':source,'slot':0,'passed':True,'mismatches':0,'max':0,'elements':60},separators=(',',':')))
            lines.append(line.replace('source_id=4 ',f'source_id={source} '))
        text='\n'.join(lines)
        self.assertEqual(len(verify(text)),24)
        with self.assertRaisesRegex(ValueError,'Missing clean same-frame'):
            verify(text.replace('"source_id":10,','"source_id":999,',1))


if __name__=='__main__':
    unittest.main()
