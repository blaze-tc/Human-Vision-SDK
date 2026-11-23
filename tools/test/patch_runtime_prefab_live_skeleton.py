"""Safely upgrade ordinary Update skeleton queries and one shared preview pane.

Owned source sections are checked before replacing them. Private controls,
diagnostics, exact-photo capture and unrelated extensions are preserved.
The CLI validates all files/backups before writing and keeps original bytes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

QUERY_PREFIX = '        private const int Capacity = 8;\n        private const int JointCount = RuntimeSkeletonReader.JointCount;\n        private static readonly int[] Parents = ReadParents();\n        private readonly Vector2[,] points = new Vector2[Capacity, JointCount];\n        private readonly bool[,] valid = new bool[Capacity, JointCount];\n        private readonly bool[] visible = new bool[Capacity];\n\n        internal int VisibleBodies { get; private set; }\n        internal int VisibleJoints { get; private set; }\n        internal int VisibleBones { get; private set; }\n\n        internal void Sample(HumanVisionSdk sdk)\n        {\n            ClearGeometry();\n            if (sdk == null) return;\n            // Ordinary business Update reads the latest valid SDK coordinates.\n            // The SDK applies confidence, source identity and body/hand age limits.\n            // No preview texture, new result event or matching frame is required.\n            int capacity = System.Math.Min(Capacity, sdk.GetMaxBodies());\n            for (int slot = 0; slot < capacity; slot++) {\n                int count = 0;\n                for (int joint = 0; joint < JointCount; joint++) {\n                    bool accepted = sdk.TryGetJointNormalizedPosition(slot, (HumanVisionCanonicalJointId)joint, out var uv);\n                    valid[slot, joint] = accepted && uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1;\n                    points[slot, joint] = uv;\n                    if (valid[slot, joint]) ++count;\n                }\n                if (count == 0) continue;\n                visible[slot] = true;\n                ++VisibleBodies;\n                VisibleJoints += count;\n                for (int joint = 0; joint < JointCount; joint++) {\n                    int parent = Parents[joint];\n                    if (parent >= 0 && valid[slot, joint] && valid[slot, parent] &&\n                        points[slot, joint] != points[slot, parent]) ++VisibleBones;\n                }\n            }\n        }\n\n        internal void Clear() => ClearGeometry();\n\n        private void ClearGeometry()\n        {\n            VisibleBodies = VisibleJoints = VisibleBones = 0;\n            System.Array.Clear(visible, 0, visible.Length);\n        }\n\n'

SUPPORTED_PREFIXES = ['0678019bc132cd32db09c9770d5422ce199ac94ceaf3809e0cb31b0884a30df5', '37f71a94da1c80f2bcac13c11a1b896472ad6f66e29f9ea474c5399d62968689']

HELPERS = '        private void DrawUnifiedPreviewCard(Rect left, Rect right)\n        {\n            var card = new Rect(left.x, left.y, right.xMax - left.x, Mathf.Max(left.height, right.height));\n            DrawCard(card, "实时预览 · 骨骼与识别区域", live, inputLine, showRegions: true, drawSkeleton: showSkeleton);\n            DrawFullscreenButton(card);\n        }\n\n        private void DrawPreviewImage(Rect image, Texture texture, bool showRegions, bool drawSkeleton, float strokeWidth = 2)\n        {\n            if (texture != null) DrawCameraImage(image, texture);\n            if (showRegions && recognition != null && recognition.UseRegions) DrawRegions(image);\n            if (drawSkeleton) liveSkeleton.Draw(image, strokeWidth);\n        }\n\n'

NEW_REFRESH = '        private void RefreshLivePreview(HumanVisionSdk sdk)\n        {\n            input = default;\n            hasInput = sdk != null && sdk.InputState == InputSourceState.Streaming &&\n                sdk.TryGetInputFrame(-1, out input) && input.Texture != null && input.Width > 0 && input.Height > 0;\n            if (startup != null && !startup.PreviewEnabled) hasInput = false;\n            live = hasInput ? input.Texture : null;\n            if (showSkeleton) liveSkeleton.Sample(sdk);\n            else liveSkeleton.Clear();\n        }\n'

OLD_REFRESHES = ['        private void RefreshLivePreview(HumanVisionSdk sdk)\n        {\n            input = default;\n            hasInput = sdk != null && sdk.InputState == InputSourceState.Streaming &&\n                sdk.TryGetInputFrame(-1, out input) && input.Texture != null && input.Width > 0 && input.Height > 0;\n            live = hasInput ? input.Texture : null;\n            if (hasInput && showSkeleton) liveSkeleton.Sample(sdk, in input);\n            else liveSkeleton.Clear();\n        }\n', '        private void RefreshLivePreview(HumanVisionSdk sdk)\n        {\n            input = default;\n            hasInput = sdk != null && sdk.InputState == InputSourceState.Streaming &&\n                sdk.TryGetInputFrame(-1, out input) && input.Texture != null && input.Width > 0 && input.Height > 0;\n            live = hasInput ? input.Texture : null;\n            if (showSkeleton) liveSkeleton.Sample(sdk);\n            else liveSkeleton.Clear();\n        }\n', '        private void RefreshLivePreview(HumanVisionSdk sdk)\n        {\n            input = default;\n            hasInput = sdk != null && sdk.InputState == InputSourceState.Streaming &&\n                sdk.TryGetInputFrame(-1, out input) && input.Texture != null && input.Width > 0 && input.Height > 0;\n            if (startup != null && !startup.PreviewEnabled) hasInput = false;\n            live = hasInput ? input.Texture : null;\n            liveSkeleton.Configure(recognition);\n            if (hasInput && showSkeleton && (!matchedPreviewRequested || fullscreenPreview)) liveSkeleton.Sample(sdk, in input);\n            else liveSkeleton.Clear();\n        }\n']

OLD_FULLSCREEN = '                if (live != null) {\n                    Rect image = RuntimePrefabLiveSkeleton.Fit(screen, live.width, live.height, fullscreenCrop);\n                    DrawCameraImage(image, live, showSkeleton, 2 * scale);\n'

INTERMEDIATE_FULLSCREEN = '                if (live != null || showSkeleton) {\n                    int width = live != null ? live.width : 1280;\n                    int height = live != null ? live.height : 720;\n                    Rect image = RuntimePrefabLiveSkeleton.Fit(screen, width, height, fullscreenCrop);\n                    DrawPreviewImage(image, live, true, showSkeleton, 2 * scale);\n'

NEW_FULLSCREEN = '                if (live != null || showSkeleton || recognition != null && recognition.UseRegions) {\n                    int width = live != null ? live.width : 1280;\n                    int height = live != null ? live.height : 720;\n                    Rect image = RuntimePrefabLiveSkeleton.Fit(screen, width, height, fullscreenCrop);\n                    DrawPreviewImage(image, live, true, showSkeleton, 2 * scale);\n'

PUBLIC_PANE = '                DrawCard(liveCard, "实时输入 · 识别区域", live, inputLine, showRegions: true);\n                DrawCard(poseCard, "实时预览 · 骨骼叠加", live, poseLine, drawSkeleton: showSkeleton);\n                DrawFullscreenButton(poseCard);\n'

PRIVATE_MAIN_REPLACEMENTS = [('                Texture posePreview = ResolvePosePreview(out var displayed);\n                string poseLabel = matchedPreviewRequested && showSkeleton ? "Prefab 精确同帧" : "实时预览 · 骨骼叠加";\n#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)\n                string liveTitle = hasInput && !string.IsNullOrEmpty(streamLabel) ? "当前码流 · " + streamLabel : "实时输入 · 识别区域";\n                DrawCard(liveCard, liveTitle, live, inputLine, showRegions: true, highlightDetail: probeDecodeMismatch);\n#else\n                DrawCard(liveCard, "实时输入 · 识别区域", live, inputLine, showRegions: true);\n#endif\n                string poseDetail = !showSkeleton ? "骨骼关闭" : matchedPreviewRequested ? poseLine : "采样骨骼叠加在最新图像上";\n                DrawCard(poseCard, poseLabel, posePreview, poseDetail, emptyText: "等待当前码流",\n                    drawSkeleton: showSkeleton && !matchedPreviewRequested);\n                DrawFullscreenButton(poseCard);\n                RecordDrawnPreview(posePreview, in displayed);\n', '                DrawUnifiedPreviewCard(liveCard, poseCard);\n                var displayed = default(HumanVisionPresentedFrame);\n                RecordDrawnPreview(live, in displayed);\n'), ('if (hasTexture || showRegions) {', 'if (hasTexture || showRegions || drawSkeleton) {'), ('                if (hasTexture) DrawCameraImage(image, texture, drawSkeleton);\n                if (showRegions && recognition != null && recognition.UseRegions) DrawRegions(image);', '                DrawPreviewImage(image, texture, showRegions, drawSkeleton);'), ('if (!hasTexture) GUI.Label(window, emptyText, badge);', 'if (!hasTexture && (!drawSkeleton || liveSkeleton.VisibleJoints == 0)) GUI.Label(window, emptyText, badge);'), ('                    !showSkeleton ? "骨骼关闭" : matchedPreviewRequested ? $"{displayed.BodyCount} 人" :\n                    $"{liveSkeleton.VisibleBodies} 人  ·  {visibleJoints} 个点  ·  {visibleBones} 条线", small);', '                    !showSkeleton ? "骨骼关闭" :\n                    $"{liveSkeleton.VisibleBodies} 人  ·  {visibleJoints} 个点  ·  {visibleBones} 条线", small);'), ('missingSkeletonEvidence = validSkeletons == 0 || (matchedPreviewRequested && RuntimeIndependentAcceptanceProbe.PreviewEnabled && showSkeleton && validPresented == 0);', 'missingSkeletonEvidence = validSkeletons == 0;')]

PRIVATE_PREVIEW_REPLACEMENTS = [('        private void RefreshLivePreview(HumanVisionSdk sdk)\n        {\n            input = default;\n            hasInput = sdk != null && sdk.InputState == InputSourceState.Streaming &&\n                sdk.TryGetInputFrame(-1, out input) && input.Texture != null && input.Width > 0 && input.Height > 0;\n            if (startup != null && !startup.PreviewEnabled) hasInput = false;\n            live = hasInput ? input.Texture : null;\n            liveSkeleton.Configure(recognition);\n            if (hasInput && showSkeleton && (!matchedPreviewRequested || fullscreenPreview)) liveSkeleton.Sample(sdk, in input);\n            else liveSkeleton.Clear();\n        }\n', '        private void DrawUnifiedPreviewCard(Rect left, Rect right)\n        {\n            var card = new Rect(left.x, left.y, right.xMax - left.x, Mathf.Max(left.height, right.height));\n            DrawCard(card, "实时预览 · 骨骼与识别区域", live, inputLine, showRegions: true, drawSkeleton: showSkeleton);\n            DrawFullscreenButton(card);\n        }\n\n        private void DrawPreviewImage(Rect image, Texture texture, bool showRegions, bool drawSkeleton, float strokeWidth = 2)\n        {\n            if (texture != null) DrawCameraImage(image, texture);\n            if (showRegions && recognition != null && recognition.UseRegions) DrawRegions(image);\n            if (drawSkeleton) liveSkeleton.Draw(image, strokeWidth);\n        }\n\n        private void RefreshLivePreview(HumanVisionSdk sdk)\n        {\n            input = default;\n            hasInput = sdk != null && sdk.InputState == InputSourceState.Streaming &&\n                sdk.TryGetInputFrame(-1, out input) && input.Texture != null && input.Width > 0 && input.Height > 0;\n            if (startup != null && !startup.PreviewEnabled) hasInput = false;\n            live = hasInput ? input.Texture : null;\n            if (showSkeleton) liveSkeleton.Sample(sdk);\n            else liveSkeleton.Clear();\n        }\n'), ('                if (live != null) {\n                    Rect image = RuntimePrefabLiveSkeleton.Fit(screen, live.width, live.height, fullscreenCrop);\n                    DrawCameraImage(image, live, showSkeleton, 2 * scale);\n', '                if (live != null || showSkeleton || recognition != null && recognition.UseRegions) {\n                    int width = live != null ? live.width : 1280;\n                    int height = live != null ? live.height : 720;\n                    Rect image = RuntimePrefabLiveSkeleton.Fit(screen, width, height, fullscreenCrop);\n                    DrawPreviewImage(image, live, true, showSkeleton, 2 * scale);\n'), ('        private HumanVisionRuntimePresentationMode OrdinaryPresentationMode =>\n            matchedPreviewRequested && showSkeleton && !fullscreenPreview\n                ? HumanVisionRuntimePresentationMode.MatchedOverlay\n                : HumanVisionRuntimePresentationMode.Disabled;', '        private HumanVisionRuntimePresentationMode OrdinaryPresentationMode =>\n            HumanVisionRuntimePresentationMode.Disabled;'), ('previewEval.Observe(matchedPreviewRequested ? "matched" : "live", texture, in input, in displayed, matched, view.Sdk);', 'previewEval.Observe("live", texture, in input, in displayed, matched, view.Sdk);')]


def _normalize(text):
    return text.replace("\r\n", "\n")

def _replace(text, before, after):
    if before == after:
        return text
    if text.count(before) == 1:
        return text.replace(before, after, 1)
    if text.count(after) == 1:
        return text
    raise ValueError("Unknown or ambiguous owned source section; refusing to overwrite it")

def _restore(original, text):
    return text.replace("\n", "\r\n" if "\r\n" in original else "\n")

def patch_live_skeleton(text):
    value = _normalize(text)
    begin = "        private const int Capacity = 8;"
    end = "        internal void Draw("
    if value.count(begin) != 1 or value.count(end) != 1:
        raise ValueError("Unknown skeleton source anchors")
    start, stop = value.index(begin), value.index(end)
    previous = value[start:stop]
    if previous != QUERY_PREFIX:
        if hashlib.sha256(previous.encode()).hexdigest() not in SUPPORTED_PREFIXES:
            raise ValueError("Unknown private skeleton prefix; review before applying")
        value = value[:start] + QUERY_PREFIX + value[stop:]
    return _restore(text, value)

def _patch_query_preview(text):
    value = _normalize(text)
    if HELPERS + NEW_REFRESH not in value:
        matches = [item for item in OLD_REFRESHES if value.count(item) == 1]
        if len(matches) != 1:
            raise ValueError("Unknown preview Update caller")
        value = value.replace(matches[0], HELPERS + NEW_REFRESH, 1)
    value = _replace(value, INTERMEDIATE_FULLSCREEN if INTERMEDIATE_FULLSCREEN in value else OLD_FULLSCREEN, NEW_FULLSCREEN)
    before, after = PRIVATE_PREVIEW_REPLACEMENTS[2]
    if "private HumanVisionRuntimePresentationMode OrdinaryPresentationMode" in value:
        value = _replace(value, before, after)
    before, after = PRIVATE_PREVIEW_REPLACEMENTS[3]
    if "previewEval.Observe(" in value:
        value = _replace(value, before, after)
    return _restore(text, value)

def _patch_query_acceptance(text):
    value = _normalize(text)
    private = 'emptyText: "等待当前码流"' in value or 'RecordDrawnPreview(' in value
    if private:
        for before, after in PRIVATE_MAIN_REPLACEMENTS:
            value = _replace(value, before, after)
    else:
        value = _replace(value, PUBLIC_PANE, '                DrawUnifiedPreviewCard(liveCard, poseCard);\n')
        value = _replace(value, 'if (hasTexture || showRegions) {', 'if (hasTexture || showRegions || drawSkeleton) {')
        old = '                if (hasTexture) DrawCameraImage(image, texture, drawSkeleton);\n                if (showRegions && recognition != null) DrawRegions(image);'
        intermediate = '                if (hasTexture) DrawCameraImage(image, texture);\n                if (drawSkeleton) liveSkeleton.Draw(image, 2);\n                if (showRegions && recognition != null) DrawRegions(image);'
        new = '                DrawPreviewImage(image, texture, showRegions, drawSkeleton);'
        if intermediate in value:
            value = _replace(value, intermediate, new)
        else:
            value = _replace(value, old, new)
        value = _replace(value, 'if (!hasTexture) GUI.Label(window, "等待输入 / 结果", badge);', 'if (!hasTexture && (!drawSkeleton || liveSkeleton.VisibleJoints == 0)) GUI.Label(window, "等待输入 / 结果", badge);')
    return _restore(text, value)

SIDEBAR_ON_GUI = '        private void OnGUI()\n        {\n            if (startup == null) return;\n            InitStyles();\n            if (fullscreenPreview) { DrawFullscreenPreview(); return; }\n            var previousMatrix = GUI.matrix;\n            var safe = Screen.safeArea;\n            if (safe.width <= 0 || safe.height <= 0) safe = new Rect(0, 0, Screen.width, Screen.height);\n            float scale = Mathf.Min(safe.width / 1440f, safe.height / 866f);\n            if (scale <= 0) return;\n            GUI.matrix = Matrix4x4.identity;\n            Box(new Rect(0, 0, Screen.width, Screen.height), Background);\n            GUI.matrix = Matrix4x4.TRS(new Vector3(safe.x + (safe.width - 1440f * scale) / 2f,\n                Screen.height - safe.yMax + (safe.height - 866f * scale) / 2f, 0),\n                Quaternion.identity, Vector3.one * scale);\n            try {\n                GetExpandedCards(scale, out var sidebar, out var preview, out _);\n                GUI.Label(new Rect(28, 12, 1050, 52), "Human Vision  /  实时识别", heading);\n                GUI.Label(new Rect(1160, 24, 252, 34), $"SDK  {HumanVisionSdk.Version}", badge);\n                DrawControlSidebar(sidebar);\n                DrawUnifiedPreviewCard(sidebar, preview);\n                ObserveSidebarPreview();\n                DrawSidebarModal();\n            } finally { GUI.matrix = previousMatrix; GUI.enabled = true; }\n        }\n'

SIDEBAR_OLD_ON_GUI = ['        private void OnGUI()\n        {\n            if (startup == null) return;\n            InitStyles();\n            if (fullscreenPreview) {\n                DrawFullscreenPreview();\n                return;\n            }\n            var previousMatrix = GUI.matrix;\n            float scale = Mathf.Min(Screen.width / 1440f, Screen.height / 866f);\n            if (scale <= 0) return;\n            GUI.matrix = Matrix4x4.identity;\n            Box(new Rect(0, 0, Screen.width, Screen.height), Background);\n            GUI.matrix = Matrix4x4.TRS(new Vector3((Screen.width - 1440f * scale) / 2f, (Screen.height - 866f * scale) / 2f, 0),\n                Quaternion.identity, Vector3.one * scale);\n            try {\n                GetExpandedCards(scale, out var liveCard, out var poseCard, out var controls);\n                DismissCapacityMenu();\n                GUI.Label(new Rect(28, 12, 900, 52), "Human Vision  /  Runtime Prefab 验收", heading);\n                GUI.Label(new Rect(30, 65, 135, 30), FixedQuality ? "固定模型等级" : "模型等级", small);\n                DrawModelButton(new Rect(177, 64, 60, 32), ModelInputQuality.Low, "低");\n                DrawModelButton(new Rect(242, 64, 60, 32), ModelInputQuality.Medium, "中");\n                DrawModelButton(new Rect(307, 64, 60, 32), ModelInputQuality.High, "高");\n                if (IsAndroid && HasQuality(startup.AccelerationMode, ModelInputQuality.UltraLow)) {\n                    DrawModelButton(new Rect(372, 64, 82, 32), ModelInputQuality.UltraLow, "超低");\n                }\n                GUI.enabled = measuring || !operating;\n                if (GUI.Button(new Rect(458, 64, 165, 32), measuring ? "取消一键测试" : "一键测试", button)) {\n                    if (measuring) cancelMeasurement = true;\n                    else Run(MeasureAcceptance());\n                }\n                GUI.enabled = true;\n                if (GUI.Button(new Rect(635, 64, 208, 32), "导出 JSONL / 复制路径", button) && diagnostics.Export()) {\n                    GUIUtility.systemCopyBuffer = diagnostics.ExportPath;\n                    status = diagnostics.Status;\n                }\n                GUI.Label(new Rect(894, 65, 140, 30), "分析设备", small);\n                DrawBackendButton(new Rect(1020, 64, 105, 32), HumanVisionAccelerationMode.Cpu, "CPU");\n                DrawBackendButton(new Rect(1133, 64, 105, 32), HumanVisionAccelerationMode.Graphics, "GPU");\n                DrawBackendButton(new Rect(1246, 64, 166, 32), HumanVisionAccelerationMode.Neural, "NPU · RK3588");\n                GUI.enabled = true;\n                Box(new Rect(1188, 24, 224, 34), Inset);\n                GUI.Label(new Rect(1188, 24, 224, 34), $"SDK  {HumanVisionSdk.Version}", badge);\n\n                string addressLabel = startup.SourceKind == InputKind.WebCamera ? "设备名称" :\n                    startup.SourceKind == InputKind.Video ? "视频路径" : "RTSP 地址";\n                GUI.Label(new Rect(28, 104, 118, 34), addressLabel, body);\n                rtspUrl = GUI.TextField(new Rect(146, 104, 1060, 34), rtspUrl, field);\n                GUI.enabled = !operating;\n                if (GUI.Button(new Rect(1224, 104, 188, 34), "连接 / 应用地址", selected)) Run(Connect());\n                GUI.enabled = true;\n\n                DrawUnifiedPreviewCard(liveCard, poseCard);\n\n                Box(controls, Panel);\n                GUI.Label(new Rect(45, 633, 870, 32), sdkLine, body);\n                GUI.Label(new Rect(930, 633, 466, 32),\n                    showSkeleton\n                    ? $"{liveSkeleton.VisibleBodies} 人  ·  {visibleJoints} 个点  ·  {visibleBones} 条线" : "", small);\n                GUI.Label(new Rect(45, 674, 145, 33), "区域数量", body);\n                GUI.enabled = !operating;\n                if (GUI.Button(CapacityButton, $"{maxBodies} 人  ▾", capacityMenuOpen ? selected : button))\n                    capacityMenuOpen = !capacityMenuOpen;\n                GUI.enabled = view != null && !operating;\n                if (GUI.Button(new Rect(437, 674, 128, 33), "重启 SDK", button)) Run(Restart());\n                if (GUI.Button(new Rect(578, 674, 128, 33), "停止 SDK", button)) Run(StopRuntime());\n                if (GUI.Button(new Rect(719, 674, 128, 33), "卸载 Prefab", button)) Run(Unload());\n                GUI.enabled = !operating;\n                if (GUI.Button(new Rect(935, 674, 205, 33), showSkeleton ? "关闭骨骼" : "开启骨骼", button))\n                    showSkeleton = !showSkeleton;\n                GUI.enabled = !operating && CanTakePhoto;\n                if (GUI.Button(new Rect(1153, 674, 243, 33), "拍照（精确同帧）", button)) Run(TakeExactPhoto());\n                GUI.enabled = true;\n                GUI.Label(new Rect(45, 720, 1349, 32), status ?? "", small);\n                GUI.Label(new Rect(45, 754, 1349, 31), telemetry.PerformanceLine, small);\n                GUI.Label(new Rect(45, 785, 1349, 31), telemetry.ReadLine, small);\n                GUI.Label(new Rect(29, 833, 1376, 29), telemetry.StageLine, small);\n                if (capacityMenuOpen) DrawCapacityMenu();\n            } finally {\n                GUI.matrix = previousMatrix;\n                GUI.enabled = true;\n            }\n        }\n', '        private void OnGUI()\n        {\n            if (startup == null) return;\n            InitStyles();\n            if (fullscreenPreview) {\n                DrawFullscreenPreview();\n                return;\n            }\n            var previousMatrix = GUI.matrix;\n            float scale = Mathf.Min(Screen.width / 1440f, Screen.height / 866f);\n            if (scale <= 0) return;\n            GUI.matrix = Matrix4x4.identity;\n            Box(new Rect(0, 0, Screen.width, Screen.height), Background);\n            GUI.matrix = Matrix4x4.TRS(new Vector3((Screen.width - 1440f * scale) / 2f, (Screen.height - 866f * scale) / 2f, 0),\n                Quaternion.identity, Vector3.one * scale);\n            try {\n                GetExpandedCards(scale, out var liveCard, out var poseCard, out var controls);\n                DismissCapacityMenu();\n                GUI.Label(new Rect(28, 12, 900, 52), "Human Vision  /  Runtime Prefab 验收", heading);\n#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)\n                DrawThermalControls();\n#else\n                GUI.Label(new Rect(30, 65, 135, 30), FixedQuality ? "固定模型等级" : "模型等级", small);\n                GUI.enabled = !operating && !FixedQuality;\n                DrawModelButton(new Rect(177, 64, 80, 32), ModelInputQuality.Low, "低");\n                DrawModelButton(new Rect(265, 64, 80, 32), ModelInputQuality.Medium, "中");\n                DrawModelButton(new Rect(353, 64, 80, 32), ModelInputQuality.High, "高");\n                GUI.enabled = !operating;\n                GUI.Label(new Rect(894, 65, 140, 30), "分析设备", small);\n                DrawBackendButton(new Rect(1020, 64, 105, 32), HumanVisionAccelerationMode.Cpu, "CPU");\n                DrawBackendButton(new Rect(1133, 64, 105, 32), HumanVisionAccelerationMode.Graphics, "GPU");\n                GUI.enabled = !operating && IsAndroid;\n                DrawBackendButton(new Rect(1246, 64, 166, 32), HumanVisionAccelerationMode.Neural, "NPU · RK3588");\n#endif\n                GUI.enabled = true;\n                Box(new Rect(1188, 24, 224, 34), Inset);\n                GUI.Label(new Rect(1188, 24, 224, 34), $"SDK  {HumanVisionSdk.Version}", badge);\n#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)\n                DrawInteractiveHeader();\n#endif\n\n#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)\n                DrawThermalAddress();\n#else\n                string addressLabel = startup.SourceKind == InputKind.WebCamera ? "设备名称" :\n                    startup.SourceKind == InputKind.Video ? "视频路径" : "RTSP 地址";\n                GUI.Label(new Rect(28, 104, 118, 34), addressLabel, body);\n                rtspUrl = GUI.TextField(new Rect(146, 104, 1060, 34), rtspUrl, field);\n                GUI.enabled = !operating;\n                if (GUI.Button(new Rect(1224, 104, 188, 34), "连接 / 应用地址", selected)) Run(Connect());\n                GUI.enabled = true;\n#endif\n\n                DrawUnifiedPreviewCard(liveCard, poseCard);\n                var displayed = default(HumanVisionPresentedFrame);\n                RecordDrawnPreview(live, in displayed);\n\n                Box(controls, Panel);\n                GUI.Label(new Rect(45, 633, 870, 32), sdkLine, body);\n                GUI.Label(new Rect(930, 633, 466, 32),\n                    !showSkeleton ? "骨骼关闭" :\n                    $"{liveSkeleton.VisibleBodies} 人  ·  {visibleJoints} 个点  ·  {visibleBones} 条线", small);\n                GUI.Label(new Rect(45, 674, 145, 33), "人数上限", body);\n                GUI.enabled = !operating;\n                if (GUI.Button(CapacityButton, $"{maxBodies} 人  ▾", capacityMenuOpen ? selected : button))\n                    capacityMenuOpen = !capacityMenuOpen;\n                GUI.enabled = view != null && !operating;\n                if (GUI.Button(new Rect(437, 674, 128, 33), "重启 SDK", button)) Run(Restart());\n                if (GUI.Button(new Rect(578, 674, 128, 33), "停止 SDK", button)) Run(StopRuntime());\n                if (GUI.Button(new Rect(719, 674, 128, 33), "卸载 Prefab", button)) Run(Unload());\n                GUI.enabled = true;\n                if (GUI.Button(new Rect(852, 674, 133, 33), showSkeleton ? "关闭骨骼" : "开启骨骼", button)) {\n                    showSkeleton = !showSkeleton;\n                    RefreshPresentationMode();\n#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)\n                    diagnostics.Mark(thermalPhase, "skeleton_visibility", NativePreviewBounds, showSkeleton ? "on" : "off");\n#endif\n                }\n                GUI.enabled = !operating && CanTakePhoto;\n                if (GUI.Button(new Rect(993, 674, 403, 33), CanTakePhoto ? "拍照（精确同帧）" :\n#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)\n                    PhotoUnavailableReason(),\n#else\n                    "等待可拍照输入",\n#endif\n                    button)) Run(TakeExactPhoto());\n                GUI.enabled = true;\n                GUI.Label(new Rect(45, 720, 1349, 32), status ?? "", small);\n                GUI.Label(new Rect(45, 754, 1349, 31), telemetry.PerformanceLine, small);\n                GUI.Label(new Rect(45, 785, 1349, 31), telemetry.ReadLine, small);\n                GUI.Label(new Rect(29, 833, 1376, 29), telemetry.StageLine, small);\n                if (capacityMenuOpen) DrawCapacityMenu();\n#if UNITY_EDITOR || (HV_ANDROID_RTSP_ACCEPTANCE && UNITY_ANDROID)\n                DrawBasicOptions();\n#endif\n            } finally {\n                GUI.matrix = previousMatrix;\n                GUI.enabled = true;\n            }\n        }\n']

SIDEBAR_MAIN_COMMON = [('private static readonly Rect LiveCard = new Rect(28, 152, 680, 460);', 'private static readonly Rect LiveCard = new Rect(28, 86, 480, 752);'), ('private static readonly Rect PoseCard = new Rect(732, 152, 680, 460);', 'private static readonly Rect PoseCard = new Rect(528, 86, 884, 752);'), ('new Rect(card.x + 16, card.y + 418, card.width - 32, 34), detail,', 'new Rect(card.x + 16, card.yMax - 42, card.width - 32, 34), detail,')]

SIDEBAR_PREVIEW_REPLACEMENTS = [('var card = new Rect(left.x, left.y, right.xMax - left.x, Mathf.Max(left.height, right.height));', 'var card = right;'), ('        private static void GetExpandedCards(float scale, out Rect left, out Rect right, out Rect controls)\n        {\n            float extraSide = Mathf.Max(0, (Screen.width / scale - 1440f) * .5f);\n            left = new Rect(LiveCard.x - extraSide, LiveCard.y, LiveCard.width + extraSide, LiveCard.height);\n            right = new Rect(PoseCard.x, PoseCard.y, PoseCard.width + extraSide, PoseCard.height);\n            controls = new Rect(28 - extraSide, 624, 1384 + extraSide * 2, 205);\n        }\n', '        private static void GetExpandedCards(float scale, out Rect left, out Rect right, out Rect controls)\n        {\n            float availableWidth = Screen.safeArea.width > 0 ? Screen.safeArea.width : Screen.width;\n            float extraSide = Mathf.Max(0, (availableWidth / scale - 1440f) * .5f);\n            left = new Rect(LiveCard.x - extraSide, LiveCard.y, LiveCard.width, LiveCard.height);\n            right = new Rect(PoseCard.x - extraSide, PoseCard.y, PoseCard.width + extraSide * 2, PoseCard.height);\n            controls = left;\n        }\n'), ('private static Rect CardImageWindow(Rect card) => new Rect(card.x + 16, card.y + 49, card.width - 32, 365);', 'private static Rect CardImageWindow(Rect card) => new Rect(card.x + 16, card.y + 49, card.width - 32, Mathf.Max(1, card.height - 100));')]

def patch_preview(text):
    value = _normalize(text)
    if SIDEBAR_PREVIEW_REPLACEMENTS[0][1] not in value:
        value = _normalize(_patch_query_preview(text))
    for before, after in SIDEBAR_PREVIEW_REPLACEMENTS:
        value = _replace(value, before, after)
    return _restore(text, value)

def patch_acceptance(text):
    value = _normalize(text)
    if SIDEBAR_ON_GUI not in value:
        value = _normalize(_patch_query_acceptance(text))
        matches = [method for method in SIDEBAR_OLD_ON_GUI if value.count(method) == 1]
        if len(matches) != 1:
            raise ValueError("Unknown acceptance GUI method; review before applying the sidebar")
        value = value.replace(matches[0], SIDEBAR_ON_GUI, 1)
    for before, after in SIDEBAR_MAIN_COMMON:
        value = _replace(value, before, after)
    return _restore(text, value)


def stage(project, backup, apply=False):
    project, backup = Path(project).resolve(), Path(backup).resolve()
    if project.name != "Human-Vision-SDK-Test":
        raise ValueError("Only Human-Vision-SDK-Test is supported")
    folder = Path("Assets/HumanVision/Samples/RuntimePrefab")
    transforms = [("RuntimePrefabLiveSkeleton.cs", patch_live_skeleton), ("RuntimePrefabAcceptancePreview.cs", patch_preview), ("RuntimePrefabAcceptance.cs", patch_acceptance)]
    plans = []
    for name, transform in transforms:
        relative = folder / name
        target = project / relative
        original = target.read_bytes()
        payload = transform(original.decode("utf-8-sig")).encode("utf-8")
        if original.startswith(b"\xef\xbb\xbf"):
            payload = b"\xef\xbb\xbf" + payload
        saved = backup / relative
        if original != payload and saved.exists() and saved.read_bytes() != original:
            raise ValueError("A different immutable backup exists: " + str(saved))
        plans.append((relative, target, original, payload, saved))
    relative = folder / "RuntimePrefabAcceptanceSidebar.cs"
    target = project / relative
    payload = (Path(__file__).parent / "unity/interactive/RuntimePrefabAcceptanceSidebar.cs").read_bytes()
    original = target.read_bytes() if target.exists() else None
    if original is not None and original != payload and hashlib.sha256(original).hexdigest() not in ['d94217cee68441cedd9522ecda56a692621adbea3105c99c75f19687266224ef', 'ba84c88fb101dd8cfabbb5b19e3639874be6f950e12ed6007d2c3840a3207913', '350580d0bda86b1051c9c10a0b93e5f1c688601e34dbd8de6aa157feb4f108aa']:
        raise ValueError("Unknown existing sidebar source; refusing to overwrite it")
    saved = backup / relative
    if original is not None and original != payload and saved.exists() and saved.read_bytes() != original:
        raise ValueError("A different immutable sidebar backup exists: " + str(saved))
    plans.append((relative, target, original, payload, saved))
    receipt = {"project": str(project), "applied": apply, "files": []}
    for relative, target, original, payload, saved in plans:
        changed = original != payload
        receipt["files"].append({"path": relative.as_posix(), "changed": changed, "previous_sha256": hashlib.sha256(original).hexdigest() if original is not None else None, "sha256": hashlib.sha256(payload).hexdigest()})
        if not apply or not changed:
            continue
        saved.parent.mkdir(parents=True, exist_ok=True)
        if original is not None and not saved.exists():
            shutil.copy2(target, saved)
        target.write_bytes(payload)
    return receipt

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--backup", type=Path, required=True, help="Immutable backup directory for all three private sources")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    print(json.dumps(stage(args.project, args.backup, args.apply), ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
