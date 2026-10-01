"""Host regressions compiling the GPU admission code extracted from the Unity source.

No Unity instance, GPU, model or synthetic inference result is used.
"""
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "unity/HumanVisionDemo/Assets/HumanVision/Demo/VideoPlayerFrameSource.cs"
MIRROR = ROOT / "upm/com.blazetc.humanvision/Runtime/Demo/VideoPlayerFrameSource.cs"


def production_policy(source):
    marker = "    internal struct GpuFrameAdmissionPolicy"
    if marker in source:
        start = source.index(marker)
        opening = source.index("{", start)
        depth = 1
        end = opening + 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        return source[start:end]
    # RED uses the existing production condition and accepted-submit deadline,
    # extracted verbatim from SubmitExternalGpuTexture, rather than a guessed model.
    method = source.split("private bool SubmitExternalGpuTexture(", 1)[1].split(
        "public bool CanPresentResult", 1)[0]
    condition = re.search(r"if \((now < _nextLiveSubmitTime)\)", method).group(1)
    assignment = re.search(r"(_nextLiveSubmitTime = now \+ 1.0 / 30.0;)", method).group(1)
    return """internal struct GpuFrameAdmissionPolicy {
        double _nextLiveSubmitTime;
        internal bool CanSubmit(double now) { return !(CONDITION); }
        internal void RecordAccepted() { ASSIGNMENT }
        internal void Reset() { _nextLiveSubmitTime = 0; }
        internal double now;
    }""".replace("CONDITION", condition).replace("ASSIGNMENT", assignment).replace(
        "return !(", "this.now = now; return !(")


HARNESS = r"""
class Program {
    static void Require(bool condition, string message) {
        if (!condition) throw new Exception(message);
    }
    static int fails;
    static void Case(string name, Action run) {
        try { run(); Console.WriteLine("PASS " + name); }
        catch (Exception error) { fails++; Console.WriteLine("FAIL " + name + ": " + error.Message); }
    }
    static void Main() {
        Case("25fps quantized onto 60Hz retains every fresh input", () => {
            var gate = new GpuFrameAdmissionPolicy();
            int accepted = 0;
            for (int i = 0; i < 2500; ++i) {
                double now = 100 + Math.Ceiling(i * 60.0 / 25.0) / 60.0 + (i % 2) * 0.0005;
                if (gate.CanSubmit(now)) { gate.RecordAccepted(); accepted++; }
            }
            Require(accepted == 2500, "accepted " + accepted + "/2500");
        });
        Case("60fps source stays bounded to 30fps with small burst", () => {
            var gate = new GpuFrameAdmissionPolicy();
            int accepted = 0;
            for (int i = 0; i < 6000; ++i) {
                double now = 100 + i / 60.0;
                if (gate.CanSubmit(now)) { gate.RecordAccepted(); accepted++; }
                Require(accepted <= Math.Floor(i / 2.0) + 2, "rate budget exceeded");
            }
            Require(accepted >= 2990, "30fps cap unnecessarily underfilled: " + accepted);
        });
        Case("backend rejection retains credit", () => {
            var gate = new GpuFrameAdmissionPolicy();
            for (int i = 0; i < 100; ++i) Require(gate.CanSubmit(100), "rejected retry spent credit");
            gate.RecordAccepted();
            Require(!gate.CanSubmit(100), "successful submission did not spend credit");
            Require(gate.CanSubmit(100 + 1.0 / 30.0), "next interval unavailable");
        });
        Case("pause accumulates at most two admissions", () => {
            var gate = new GpuFrameAdmissionPolicy();
            Require(gate.CanSubmit(100), "initial admission"); gate.RecordAccepted();
            int burst = 0;
            while (burst < 100 && gate.CanSubmit(1000)) { gate.RecordAccepted(); burst++; }
            Require(burst == 2, "pause burst was " + burst);
            Require(!gate.CanSubmit(1000 + 1.0 / 60.0), "unbounded catchup after pause");
        });
        Case("reset and clock reversal allow a new session", () => {
            var gate = new GpuFrameAdmissionPolicy();
            Require(gate.CanSubmit(100), "initial"); gate.RecordAccepted();
            gate.Reset();
            Require(gate.CanSubmit(0), "session reset retained debt"); gate.RecordAccepted();
            Require(gate.CanSubmit(1), "forward"); gate.RecordAccepted();
            Require(gate.CanSubmit(0.5), "clock reversal retained debt"); gate.RecordAccepted();
            Require(!gate.CanSubmit(0.5), "clock reversal admitted unbounded repeats");
        });
        Case("invalid clock cannot poison subsequent valid admission", () => {
            var gate = new GpuFrameAdmissionPolicy();
            Require(!gate.CanSubmit(double.NaN), "NaN admitted");
            Require(!gate.CanSubmit(double.PositiveInfinity), "infinity admitted");
            Require(!gate.CanSubmit(-1), "negative clock admitted");
            Require(gate.CanSubmit(0), "valid clock poisoned");
        });
        Case("admission path allocates zero bytes after warmup", () => {
            var gate = new GpuFrameAdmissionPolicy();
            for (int i = 0; i < 10000; ++i) if (gate.CanSubmit(i / 60.0)) gate.RecordAccepted();
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int i = 10000; i < 110000; ++i) if (gate.CanSubmit(i / 60.0)) gate.RecordAccepted();
            long allocated = GC.GetAllocatedBytesForCurrentThread() - before;
            Require(allocated == 0, "allocated " + allocated);
        });
        Environment.ExitCode = fails == 0 ? 0 : 1;
    }
}
"""


class GpuFrameAdmissionTests(unittest.TestCase):
    def test_gpu_wiring_spends_only_after_backend_acceptance(self):
        source = SOURCE.read_text(encoding="utf-8-sig")
        method = source.split("private bool SubmitExternalGpuTexture(", 1)[1].split(
            "public bool CanPresentResult", 1)[0]
        self.assertIn("if (!_gpuAdmission.CanSubmit(now))", method)
        self.assertIn("if (SourceWidth != texture.width || SourceHeight != texture.height) _gpuAdmission.Reset();", method)
        self.assertLess(method.index("_gpuAdmission.CanSubmit(now)"), method.index("manager.SubmitAndroidGpuFrame("))
        self.assertLess(method.index("if (!accepted)"), method.index("_gpuAdmission.RecordAccepted();"))
        self.assertNotIn("_nextLiveSubmitTime", method)
        stop = source.split("private void StopCurrentVideo()", 1)[1].split(
            "private void ReleaseReadbackResources()", 1)[0]
        self.assertIn("_gpuAdmission.Reset();", stop)
        cpu = source.split("private bool SubmitExternalCpuTexture(", 1)[1].split(
            "private bool SubmitExternalGpuTexture(", 1)[0]
        self.assertIn("if (now < _nextLiveSubmitTime) return false;", cpu)
        self.assertIn("_nextLiveSubmitTime = _livePendingTime + 1.0 / 30.0;", cpu)
        self.assertNotIn("_gpuAdmission", cpu)

    def test_actual_production_policy(self):
        source = SOURCE.read_text(encoding="utf-8-sig")
        if "internal struct GpuFrameAdmissionPolicy" in source:
            self.assertEqual(production_policy(source), production_policy(MIRROR.read_text(encoding="utf-8-sig")))
        with tempfile.TemporaryDirectory(prefix="hv-admission-") as directory:
            project = pathlib.Path(directory)
            (project / "Admission.csproj").write_text(
                '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
                '<OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework>'
                '<LangVersion>9</LangVersion></PropertyGroup></Project>', encoding="utf-8")
            (project / "Program.cs").write_text(
                "using System;\n" + production_policy(source) + HARNESS, encoding="utf-8")
            result = subprocess.run(["dotnet", "run", "--project", str(project / "Admission.csproj"),
                                     "--configuration", "Release"], capture_output=True, text=True)
            print(result.stdout, end="")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
