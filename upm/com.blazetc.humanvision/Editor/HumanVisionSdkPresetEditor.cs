using UnityEditor;
using UnityEngine;

namespace HumanVision.Editor
{
    /// <summary>默认配置资产编辑器。语义计算模式与图像尺寸使用选项；不暴露旧 Windows 双开关。</summary>
    [CustomEditor(typeof(HumanVisionSdkPreset))]
    public sealed class HumanVisionSdkPresetEditor : UnityEditor.Editor
    {
        private static readonly string[] Sizes = { "640 × 480", "1280 × 720", "1920 × 1080", "3840 × 2160" };
        private static readonly int[] Widths = { 640, 1280, 1920, 3840 }, Heights = { 480, 720, 1080, 2160 };
        public override void OnInspectorGUI()
        {
            serializedObject.Update();
            var options = serializedObject.FindProperty("options");
            EditorGUILayout.HelpBox("资产保存默认参数；初始化代码可覆盖 Camera 设备名、RTSP 地址或视频路径。运行时不会修改资产。", MessageType.Info);
            Draw(options, "Recognition", "人数、区域和结果有效性", true);
            var mode = options.FindPropertyRelative("AccelerationMode");
            mode.intValue = EditorGUILayout.IntPopup("分析设备", mode.intValue, new[] { "CPU", "GPU", "NPU" }, new[] { 2, 0, 1 });
            options.FindPropertyRelative("UseWindowsCpu").boolValue = mode.intValue == 2;
            Draw(options, "InputQuality", "模型等级");
            EditorGUILayout.HelpBox("可用模式和模型等级取决于平台与已安装模型。NPU 需支持的 RK3588 设备；不支持时明确报错。图像尺寸不改变模型尺寸。", MessageType.None);
            Draw(options, "SourceKind", "默认输入类型");
            var input = options.FindPropertyRelative("Input");
            var width = input.FindPropertyRelative("RequestedWidth"); var height = input.FindPropertyRelative("RequestedHeight");
            int selected = -1;
            for (int i = 0; i < Widths.Length; i++) if (width.intValue == Widths[i] && height.intValue == Heights[i]) selected = i;
            EditorGUI.BeginChangeCheck();
            int next = EditorGUILayout.Popup(new GUIContent("图像尺寸", "相机请求采集尺寸；RTSP 本地输出上限；视频使用源尺寸。"), selected, Sizes);
            if (EditorGUI.EndChangeCheck() && next >= 0) { width.intValue = Widths[next]; height.intValue = Heights[next]; }
            if (selected < 0) EditorGUILayout.LabelField("已有自定义尺寸", width.intValue + " × " + height.intValue + "（选择后替换）");
            Draw(input, "RequestedFramesPerSecond", "请求输入 FPS");
            Draw(input, "Mirror", "镜像");
            Draw(input, "CameraDevice", "相机设备名（可由代码提供）");
            Draw(input, "RtspUrl", "RTSP 地址（可由代码提供）");
            Draw(input, "VideoPath", "视频路径（可由代码提供）");
            Draw(options, "RtspTcp", "RTSP 使用 TCP");
            Draw(options, "OpenInputOnInitialize", "初始化时打开输入");
            Draw(options, "InputOpenTimeoutSeconds", "输入启动超时（秒）");
            Draw(options, "RuntimeRoot", "运行资源目录（留空自动准备）");
            serializedObject.ApplyModifiedProperties();
        }
        private static void Draw(SerializedProperty owner, string name, string label, bool children = false)
        { EditorGUILayout.PropertyField(owner.FindPropertyRelative(name), new GUIContent(label), children); }
    }
}
