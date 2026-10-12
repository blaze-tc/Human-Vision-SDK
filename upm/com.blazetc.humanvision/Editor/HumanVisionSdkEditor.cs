using UnityEditor;
using UnityEngine;

namespace HumanVision.Editor
{
    /// <summary>资产驱动的新总控只显示资产入口和显示映射；无资产的旧场景保留内嵌配置编辑。</summary>
    [CustomEditor(typeof(HumanVisionSdk))]
    public sealed class HumanVisionSdkEditor : UnityEditor.Editor
    {
        public override void OnInspectorGUI()
        {
            serializedObject.Update();
            bool hasAsset = serializedObject.FindProperty("configurationAsset").objectReferenceValue != null;
            if (hasAsset) DrawPropertiesExcluding(serializedObject, "options");
            else DrawPropertiesExcluding(serializedObject);
            serializedObject.ApplyModifiedProperties();
            if (hasAsset) EditorGUILayout.HelpBox("启动默认值来自配置资产。输入源地址可在初始化代码中覆盖；运行时修改请调用 SDK API。", MessageType.Info);
        }
    }
}
