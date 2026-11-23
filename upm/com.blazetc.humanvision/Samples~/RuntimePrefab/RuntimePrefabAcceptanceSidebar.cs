using HumanVision;
using HumanVision.Demo;
using UnityEngine;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        partial void DrawSidebarChoices(float width, ref float y, ref bool handled);
        partial void DrawSidebarModal();
        partial void DrawSidebarMeasurement(float width, ref float y);
        partial void DrawSidebarAdditionalModels(float width, ref float y);
        partial void ObserveSidebarPreview();
        partial void SidebarPhotoReason(ref string reason);
        private Vector2 sidebarScroll;
        private Vector2 sidebarLogScroll;
        private float sidebarLogContentHeight = 1000;
        private float sidebarContentHeight = 1600;
        private GUIStyle sidebarTextStyle;
        private readonly GUIContent sidebarContent = new GUIContent();

        private void DrawControlSidebar(Rect area)
        {
            Box(area, Panel);
            float logHeight = Mathf.Clamp(area.height * .36f, 220, 320);
            var viewport = new Rect(area.x + 14, area.y + 12, area.width - 28, area.height - logHeight - 32);
            var logPanel = new Rect(area.x + 10, area.yMax - logHeight - 10, area.width - 20, logHeight);
            var logViewport = new Rect(logPanel.x + 4, logPanel.y + 8, logPanel.width - 8, logPanel.height - 16);
            float width = viewport.width - 22;
            sidebarScroll = GUI.BeginScrollView(viewport, sidebarScroll,
                new Rect(0, 0, width, Mathf.Max(viewport.height, sidebarContentHeight)), false, true);
            try {
                float y = 0;
                bool handled = false;
                DrawSidebarChoices(width, ref y, ref handled);
                if (!handled) DrawStandaloneSidebarChoices(width, ref y);
                DrawSidebarRuntimeActions(width, ref y);
                sidebarContentHeight = y + 16;
            } finally { GUI.EndScrollView(); GUI.enabled = true; }
            Box(logPanel, Inset);
            width = logViewport.width - 22;
            sidebarLogScroll = GUI.BeginScrollView(logViewport, sidebarLogScroll,
                new Rect(0, 0, width, Mathf.Max(logViewport.height, sidebarLogContentHeight)), false, true);
            try {
                float y = 0;
                DrawSidebarText("实时状态 / 日志", width, ref y, true);
                DrawSidebarText(telemetry.PerformanceLine, width, ref y);
                if (view != null) {
                    string previewError = view.Sdk.PreviewError;
                    if (!string.IsNullOrEmpty(previewError)) DrawSidebarText("原图预览不可用：" + previewError, width, ref y);
                    else if (view.Sdk.State == HumanVisionSdkState.Error && !string.IsNullOrEmpty(view.Sdk.LastError))
                        DrawSidebarText(view.Sdk.LastError, width, ref y);
                }
                DrawSidebarText($"{liveSkeleton.VisibleBodies} 人 · {visibleJoints} 个点 · {visibleBones} 条线", width, ref y);
                DrawSidebarText(status ?? "", width, ref y);
                DrawSidebarText(sdkLine, width, ref y);
                DrawSidebarText(inputLine, width, ref y);
                DrawSidebarText(telemetry.ReadLine, width, ref y);
                DrawSidebarText(telemetry.StageLine, width, ref y);
                sidebarLogContentHeight = y + 16;
            } finally { GUI.EndScrollView(); GUI.enabled = true; }
        }

        private void DrawSidebarText(string text, float width, ref float y, bool section = false)
        {
            if (string.IsNullOrEmpty(text)) return;
            if (sidebarTextStyle == null) sidebarTextStyle = new GUIStyle(small) { wordWrap = true, fontSize = 18 };
            sidebarContent.text = text;
            var style = section ? body : sidebarTextStyle;
            float height = Mathf.Max(section ? 32 : 28, style.CalcHeight(sidebarContent, width));
            GUI.Label(new Rect(0, y, width, height), sidebarContent, style);
            y += height + (section ? 10 : 8);
        }

        private void DrawStandaloneSidebarChoices(float width, ref float y)
        {
            DrawSidebarText("分析设备", width, ref y, true);
            float third = (width - 16) / 3;
            DrawBackendButton(new Rect(0, y, third, 36), HumanVisionAccelerationMode.Cpu, "CPU");
            DrawBackendButton(new Rect(third + 8, y, third, 36), HumanVisionAccelerationMode.Graphics, "GPU");
            DrawBackendButton(new Rect((third + 8) * 2, y, third, 36), HumanVisionAccelerationMode.Neural, "NPU");
            GUI.enabled = true; y += 48;
            DrawSidebarText("模型等级", width, ref y, true);
            DrawModelButton(new Rect(0, y, third, 36), ModelInputQuality.Low, "低");
            DrawModelButton(new Rect(third + 8, y, third, 36), ModelInputQuality.Medium, "中");
            DrawModelButton(new Rect((third + 8) * 2, y, third, 36), ModelInputQuality.High, "高");
            GUI.enabled = true; y += 48;
            DrawSidebarAdditionalModels(width, ref y);
            DrawSidebarText($"输入源：{startup.SourceKind}", width, ref y, true);
            rtspUrl = GUI.TextField(new Rect(0, y, width, 36), rtspUrl, field); y += 44;
            GUI.enabled = !operating;
            if (GUI.Button(new Rect(0, y, width, 36), "连接 / 应用输入", selected)) Run(Connect());
            GUI.enabled = true; y += 48;
            DrawSidebarText("更多输入与识别参数由 SDK 配置资产设置。", width, ref y);
            DrawSidebarMeasurement(width, ref y);
            if (GUI.Button(new Rect(0, y, width, 36), "导出 JSONL / 复制路径", button)) {
                diagnostics.Export(); if (!string.IsNullOrEmpty(diagnostics.ExportPath)) GUIUtility.systemCopyBuffer = diagnostics.ExportPath;
                status = diagnostics.Status;
            }
            y += 50;
        }

        private void DrawSidebarRuntimeActions(float width, ref float y)
        {
            DrawSidebarText("人数上限", width, ref y, true);
            GUI.enabled = !operating;
            float quarter = (width - 24) / 4;
            for (int count = 1; count <= MaxCapacity; count++) {
                int index = count - 1;
                if (GUI.Button(new Rect((index % 4) * (quarter + 8), y + (index / 4) * 42, quarter, 34),
                    count + " 人", count == maxBodies ? selected : button)) SetCapacity(count);
            }
            y += 92;
            float third = (width - 16) / 3;
            GUI.enabled = view != null && !operating;
            if (GUI.Button(new Rect(0, y, third, 36), "重启 SDK", button)) Run(Restart());
            if (GUI.Button(new Rect(third + 8, y, third, 36), "停止 SDK", button)) Run(StopRuntime());
            if (GUI.Button(new Rect((third + 8) * 2, y, third, 36), "卸载", button)) Run(Unload());
            GUI.enabled = !operating; y += 46;
            if (GUI.Button(new Rect(0, y, width, 36), showSkeleton ? "关闭骨骼" : "开启骨骼", button)) showSkeleton = !showSkeleton;
            y += 44;
            GUI.enabled = !operating && CanTakePhoto;
            if (GUI.Button(new Rect(0, y, width, 36), "拍照（精确同帧）", button)) Run(TakeExactPhoto());
            GUI.enabled = true; y += 44;
            if (!CanTakePhoto) {
                string reason = "等待可拍照输入或当前平台不支持精确同帧照片";
                SidebarPhotoReason(ref reason); DrawSidebarText(reason, width, ref y);
            }
            y += 8;
        }

    }
}
