"""Static guard for the Android-only destruction fault boundary.

The Windows native tests cannot construct an Android ncnn Vulkan session, so
keep a source-level regression check for the second fault decision after the
final AHB role's Vulkan submit/wait may itself fail.
"""

import unittest
from pathlib import Path


SOURCE = (Path(__file__).resolve().parents[2] /
          "runtime/plugins/backend/ncnn/ncnn_android_session.cpp")


class PreparedDestructorSafetyTest(unittest.TestCase):
    def test_rechecks_gpu_fault_after_finishing_active_role(self):
        code = SOURCE.read_text(encoding="utf-8")
        destructor = code.split("AndroidSession::~AndroidSession() {", 1)[1].split(
            "bool AndroidSession::ParseModel", 1)[0]
        final_role = destructor.index("FinishObservation(*active_consumer_, ignored);")
        resource_release = destructor.index("for (auto& slot : slots_) slot.reset();")
        self.assertIn("RequiresProcessLifetimeRetention(terminal_gpu_fault_)",
                      destructor[final_role:resource_release])


if __name__ == "__main__":
    unittest.main()
