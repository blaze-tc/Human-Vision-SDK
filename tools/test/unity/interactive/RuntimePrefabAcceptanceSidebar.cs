#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)
using System.IO;
using UnityEngine;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        private string diagnosticExportPath;
        private string diagnosticExportStatus;

        private string CollectDiagnosticLogs()
        {
            diagnostics.Mark(thermalPhase, "manual_log_collection", NativePreviewBounds, status, view?.Sdk);
            bool exported = diagnostics.Export();
            string path = diagnostics.ExportPath;
            diagnosticExportPath = exported || File.Exists(path) ? path : "";
            diagnosticExportStatus = diagnostics.Status;
            if (!string.IsNullOrEmpty(diagnosticExportPath))
            {
                GUIUtility.systemCopyBuffer = diagnosticExportPath;
                diagnosticExportStatus += " · 路径已复制";
            }
            return diagnosticExportPath;
        }
    }
}
#endif
