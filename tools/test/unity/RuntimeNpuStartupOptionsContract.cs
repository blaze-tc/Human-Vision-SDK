using System;
using System.Reflection;
using System.Text;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;

// Pure startup-parameter/serialization checks. No GameObject, SDK initialization,
// graphics call, source, model, device or NPU backend is started by this contract.
internal static class RuntimeNpuStartupOptionsContract
{
    private static int checks;
    private static void Check(bool value, string message)
    {
        ++checks;
        if (!value) throw new InvalidOperationException(message);
    }
    private static HumanVisionSdkOptions Draft() => new HumanVisionSdkOptions {
        SourceKind = InputKind.Video, AccelerationMode = HumanVisionAccelerationMode.Cpu,
        InputQuality = ModelInputQuality.High, OpenInputOnInitialize = false,
        PreviewEnabled = false, UseModelSizedAnalysisInput = false, AutoSelectRtspStream = true
    };
    private static void Reject(string selectedCase, string address)
    {
        var options = Draft();
        try {
            RuntimeIndependentAcceptanceProbe.ApplyNpuStartupOptions(options, selectedCase, address, true, true);
            throw new InvalidOperationException("Invalid startup input was accepted.");
        } catch (ArgumentException) { }
        Check(options.SourceKind == InputKind.Video && options.AccelerationMode == HumanVisionAccelerationMode.Cpu &&
            options.InputQuality == ModelInputQuality.High && !options.OpenInputOnInitialize,
            "Rejected parameters must not partially mutate the draft.");
    }
    public static int Main()
    {
        try {
            const string url = "rtsp://private.invalid:8554/explicit-current-stream";
            foreach (string selectedCase in new[] { "A", "B", "C" }) {
                foreach (bool mirrored in new[] { false, true }) {
                    var options = Draft(); var recognition = options.Recognition;
                    RuntimeIndependentAcceptanceProbe.ApplyNpuStartupOptions(options, selectedCase, url, !mirrored, mirrored);
                    Check(options.SourceKind == InputKind.Rtsp && options.Input.RtspUrl == url, "Explicit RTSP source changed.");
                    Check(options.AccelerationMode == HumanVisionAccelerationMode.Neural && options.InputQuality == ModelInputQuality.Low &&
                        !options.UseWindowsCpu, "Cold startup must select Neural/Low without a CPU fallback.");
                    Check(options.ExecutionPolicy == (selectedCase == "C" ? HumanVisionExecutionPolicy.CompatibleUnityInput :
                        HumanVisionExecutionPolicy.IndependentRequired), "Wrong A/B/C execution policy.");
                    Check(options.PreviewEnabled == (selectedCase != "B") && options.OpenInputOnInitialize && options.UseModelSizedAnalysisInput,
                        "Wrong preview/open/model-sized startup booleans.");
                    Check(options.RtspTcp == !mirrored && options.Input.Mirror == mirrored && !options.AutoSelectRtspStream,
                        "TCP/mirror/current URL policy was not preserved.");
                    Check(ReferenceEquals(recognition, options.Recognition), "Startup probe must not change recognition settings.");
                }
            }
            Reject("D", url); Reject("", url); Reject("A", ""); Reject("B", "https://private.invalid/stream");
            var add = typeof(RuntimeIndependentAcceptanceProbe).GetMethod("Add", BindingFlags.Static | BindingFlags.NonPublic);
            var json = new StringBuilder("{");
            add.Invoke(null, new object[] { json, "value", "\t\r\n\0\"\\" });
            Check(json.ToString() == "{,\"value\":\"\\u0009\\u000d\\u000a\\u0000\\\"\\\\\"", "Control characters must be escaped in diagnostic JSON.");
            var redact = typeof(RuntimeIndependentAcceptanceProbe).GetMethod("DiagnosticText", BindingFlags.Static | BindingFlags.NonPublic);
            string redacted = (string)redact.Invoke(null, new object[] { "before rtsp://name:password@private.invalid/path?token=secret after" });
            Check(redacted == "before [rtsp-url-redacted] after", "Startup evidence must remove the complete RTSP URL.");
            Console.WriteLine("NPU startup parameter/JSON checks=" + checks + " passed; no source or backend initialized.");
            return 0;
        } catch (Exception error) {
            Console.WriteLine("NPU startup contract failed: " + error.GetBaseException().Message);
            return 1;
        }
    }
}
