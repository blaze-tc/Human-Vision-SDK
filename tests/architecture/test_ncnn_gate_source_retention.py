"""Guard Android-only Unity source ownership at terminal GPU fault.

The ordinary editor license gate prevents compiling the isolated development
player in CI, so this checks the source of the critical pre/post-End contract.
The physical device gate remains mandatory.
"""

import os
import unittest
from pathlib import Path


SOURCE = Path(os.environ.get("HV_GATE_SOURCE_OVERRIDE") or
              Path(__file__).resolve().parents[2] /
              "unity/HumanVisionDemo/Assets/HumanVision/Demo/Live/HumanVisionAndroidGpuGate.cs")


class GateSourceRetentionTest(unittest.TestCase):
    def test_terminal_state_stops_all_sources_and_retains_unproved_objects(self):
        code = SOURCE.read_text(encoding="utf-8")
        self.assertTrue("HV_AndroidGpuGateMustRetainSource()" in code)
        end = code.split("private void EndSource()", 1)[1].split("private static void RetainTerminalTexture", 1)[0]
        self.assertLess(end.index("HV_AndroidGpuGateMustRetainSource()"),
                        end.index("HV_AndroidGpuGateEnd()"))
        self.assertIn("retainSource |= HV_AndroidGpuGateMustRetainSource()", end)
        self.assertIn("RetainTerminalTexture(_source)", end)
        self.assertIn("RetainTerminalTexture(_preparedFixture)", end)
        self.assertIn("RetainTerminalTexture(_camera)", end)
        self.assertIn("if (_terminalGpuFault) return;", code.split("private void Update()", 1)[1])
        self.assertIn("if (_terminalGpuFault) RetainedTerminalCommands.Add(_commands)", code)


if __name__ == "__main__":
    unittest.main()
