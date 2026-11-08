using UnityEditor;
using UnityEngine;

namespace HumanVision.Editor
{
    /// <summary>在当前场景创建仅需一个用户总控的 SDK；不强制创建显示 UI。</summary>
    public static class HumanVisionSdkMenu
    {
        /// <summary>优先使用 Project 当前选择的资产，其次使用项目默认资产或旧版场景默认资产。</summary>
        internal static HumanVisionSdkPreset GetOrCreateDefaultPreset()
        {
            if (Selection.activeObject is HumanVisionSdkPreset selected) return selected;
            var preset = HumanVisionSdkPreset.LoadProjectDefault();
            if (preset != null) return preset;
            const string legacyPath = "Assets/HumanVisionSdk/HumanVisionSdkConfiguration.asset";
            var legacy = AssetDatabase.LoadAssetAtPath<HumanVisionSdkPreset>(legacyPath);
            return legacy != null ? legacy : CreateProjectDefaultPreset();
        }

        [MenuItem("HumanVision/Create Default SDK Configuration", false, 1)]
        private static void SelectProjectDefaultPreset()
        {
            var preset = HumanVisionSdkPreset.LoadProjectDefault();
            if (preset == null) preset = CreateProjectDefaultPreset();
            if (preset == null) return;
            Selection.activeObject = preset;
            EditorGUIUtility.PingObject(preset);
        }

        private static HumanVisionSdkPreset CreateProjectDefaultPreset()
        {
            const string folder = "Assets/Resources";
            const string path = folder + "/HumanVisionSdkConfiguration.asset";
            if (AssetDatabase.LoadMainAssetAtPath(path) != null) {
                Debug.LogError("已有同名资源占用配置路径，请先检查 " + path);
                return null;
            }
            if (!AssetDatabase.IsValidFolder(folder)) AssetDatabase.CreateFolder("Assets", "Resources");
            var preset = ScriptableObject.CreateInstance<HumanVisionSdkPreset>();
            AssetDatabase.CreateAsset(preset, path);
            Undo.RegisterCreatedObjectUndo(preset, "Create Human Vision SDK Configuration");
            AssetDatabase.SaveAssets();
            return preset;
        }
        /// <summary>支持 Hierarchy 上下文父对象和 Undo；配置通过总控 Inspector 修改。</summary>
        [MenuItem("GameObject/Human Vision/Create Scene SDK (Legacy)", false, 12)]
        public static void CreateSdk(MenuCommand command)
        {
            Undo.IncrementCurrentGroup();
            int group = Undo.GetCurrentGroup();
            Undo.SetCurrentGroupName("Create Human Vision SDK");
            var owner = new GameObject("Human Vision SDK");
            GameObjectUtility.SetParentAndAlign(owner, command.context as GameObject);
            // 先设置父对象，再记录完整对象快照，避免 Undo 恢复悬空的父指针。
            owner.AddComponent<HumanVisionSdk>().ConfigurationAsset = GetOrCreateDefaultPreset();
            Undo.RegisterCreatedObjectUndo(owner, "Create Human Vision SDK");
            Selection.activeGameObject = owner;
            Undo.CollapseUndoOperations(group);
        }

        /// <summary>创建跨场景 SDK 根 Prefab。保持根级对象以便运行时接管生命周期。</summary>
        [MenuItem("HumanVision/Create SDK", false, 0)]
        [MenuItem("GameObject/Human Vision/Create SDK", false, 10)]
        public static void CreateRuntimeSdk()
        {
            var prefab = Resources.Load<GameObject>("HumanVisionRuntime");
            if (prefab == null) { Debug.LogError("HumanVisionRuntime prefab is missing from the SDK package."); return; }
            Undo.IncrementCurrentGroup();
            int group = Undo.GetCurrentGroup();
            Undo.SetCurrentGroupName("Create Human Vision Runtime");
            var preset = GetOrCreateDefaultPreset();
            var owner = (GameObject)PrefabUtility.InstantiatePrefab(prefab);
            var sdk = owner.GetComponent<HumanVisionSdk>();
            sdk.ConfigurationAsset = preset;
            sdk.InitializeOnStart = true;
            PrefabUtility.RecordPrefabInstancePropertyModifications(sdk);
            Undo.RegisterCreatedObjectUndo(owner, "Create Human Vision Runtime");
            Selection.activeGameObject = owner;
            Undo.CollapseUndoOperations(group);
        }
    }
}
