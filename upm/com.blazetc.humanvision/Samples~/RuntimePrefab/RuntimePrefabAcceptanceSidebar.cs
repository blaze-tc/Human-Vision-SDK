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
            diagnostics.Mark(diagnosticPhase, "manual_log_collection", RequestedInputSize, status, view?.Sdk);
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
