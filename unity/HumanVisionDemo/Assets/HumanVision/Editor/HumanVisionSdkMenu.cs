using UnityEditor;
using UnityEngine;

namespace HumanVision.Editor
{
    /// <summary>在当前场景创建仅需一个用户总控的 SDK；不强制创建显示 UI。</summary>
    public static class HumanVisionSdkMenu
    {
        /// <summary>优先使用 Project 当前选择的配置资产，否则复用或创建项目默认资产；从不覆盖已有参数。</summary>
        internal static HumanVisionSdkPreset GetOrCreateDefaultPreset()
        {
            if (Selection.activeObject is HumanVisionSdkPreset selected) return selected;
            const string folder = "Assets/HumanVisionSdk";
            const string path = folder + "/HumanVisionSdkConfiguration.asset";
            var preset = AssetDatabase.LoadAssetAtPath<HumanVisionSdkPreset>(path);
            if (preset != null) return preset;
            if (!AssetDatabase.IsValidFolder(folder)) AssetDatabase.CreateFolder("Assets", "HumanVisionSdk");
            preset = ScriptableObject.CreateInstance<HumanVisionSdkPreset>();
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
