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
            bool hasSceneAsset = serializedObject.FindProperty("configurationAsset").objectReferenceValue != null;
            bool hasAsset = hasSceneAsset || HumanVisionSdkPreset.LoadProjectDefault() != null;
            if (hasAsset) DrawPropertiesExcluding(serializedObject, "options");
            else DrawPropertiesExcluding(serializedObject);
            serializedObject.ApplyModifiedProperties();
            if (hasSceneAsset) EditorGUILayout.HelpBox("启动参数来自场景配置资产。初始化代码可传入自己的启动参数。", MessageType.Info);
            else if (hasAsset) EditorGUILayout.HelpBox("未绑定场景资产。启动参数来自项目默认配置，可通过 HumanVision → Create Default SDK Configuration 选择。", MessageType.Info);
        }
    }
}
