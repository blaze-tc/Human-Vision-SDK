using HumanVision.Demo;
using HumanVision.Input;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.UI;

namespace HumanVision.Editor
{
    /// <summary>用 Editor API 创建并保存普通 UGUI、Prefab 和场景；运行时不依赖此生成器。</summary>
    public static class HumanVisionSettingsDemoBuilder
    {
        public const string Folder = "Assets/HumanVisionSettingsDemo";
        public const string ScenePath = Folder + "/HumanVisionSettingsDemo.unity";
        public const string PrefabPath = Folder + "/HumanVisionSettingsDemo.prefab";
        /// <summary>当前场景的设置界面；可 Undo，再手动保存到自己的场景。</summary>
        [MenuItem("GameObject/Human Vision/Create SDK Settings UI", false, 11)]
        public static void CreateInCurrentScene(MenuCommand command)
        {
            var root = CreateRoot();
            GameObjectUtility.SetParentAndAlign(root, command.context as GameObject);
            Undo.RegisterCreatedObjectUndo(root, "Create Human Vision Settings"); Selection.activeGameObject = root;
        }
        private static GameObject CreateRoot()
        {
            var root = new GameObject("Human Vision Settings Demo");
            var sdk = root.AddComponent<HumanVisionSdk>(); sdk.InitializeOnStart = false;
            var view = HumanVisionSettingsView.Create(root.transform);
            root.AddComponent<HumanVisionSettingsController>().Configure(sdk, view);
            return root;
        }
        /// <summary>在独立文件夹生成场景和 Prefab。新版本不覆盖已有用户布局。</summary>
        [MenuItem("HumanVision/Create SDK settings demo assets")]
        public static void GenerateAssets() => GenerateAssetsAt(Folder);
        /// <summary>
        /// 显式升级旧设置界面的分辨率控件。只移除宽高输入及空行，保留 SDK、
        /// Controller、字体和用户布局；调用者负责保存场景/Prefab。
        /// </summary>
        public static void UpgradeResolutionControls(HumanVisionSettingsView view)
        {
            if (view == null) throw new System.ArgumentNullException(nameof(view));
            if (EditorApplication.isPlaying) throw new System.InvalidOperationException("请先退出 Play Mode。");
            var serialized = new SerializedObject(view);
            var choice = serialized.FindProperty("captureChoice").objectReferenceValue as Dropdown;
            if (choice == null) throw new System.InvalidOperationException("设置界面缺少分辨率下拉框。");
            var fields = serialized.FindProperty("fields");
            var removed = new System.Collections.Generic.List<InputField>();
            for (int i = fields.arraySize - 1; i >= 0; i--) {
                var item = fields.GetArrayElementAtIndex(i); var field = item.objectReferenceValue as InputField;
                if (field == null || (field.name != "Width" && field.name != "Height")) continue;
                removed.Add(field); item.objectReferenceValue = null; fields.DeleteArrayElementAtIndex(i);
            }
            serialized.ApplyModifiedPropertiesWithoutUndo();
            foreach (var field in removed) {
                // 标准生成器的列只有标签和输入。自定义容器仅删除输入本身。
                var column = field.transform.parent;
                var row = column != null ? column.parent : null;
                if (column != null && (column.name == "Width field" || column.name == "Height field"))
                    Object.DestroyImmediate(column.gameObject);
                else Object.DestroyImmediate(field.gameObject);
                if (row != null && row.childCount == 0) Object.DestroyImmediate(row.gameObject);
            }
            int previous = choice.transform.GetSiblingIndex() - 1;
            if (previous >= 0) {
                var title = choice.transform.parent.GetChild(previous).GetComponent<Text>();
                if (title != null && (title.text == "采集预设" || title.text == "采集分辨率")) title.text = "采集分辨率";
            }
            choice.ClearOptions(); choice.AddOptions(new System.Collections.Generic.List<string> { "640 × 480", "1280 × 720", "1920 × 1080", "3840 × 2160" });
            choice.SetValueWithoutNotify(1);
            var existingHint = choice.transform.parent.Find("Capture resolution hint");
            if (existingHint == null) {
                var hint = InputPreviewCanvas.Label(choice.transform.parent, "相机请求采集尺寸；RTSP 限制本地输出；视频按源尺寸播放。", 52);
                hint.name = "Capture resolution hint"; hint.font = choice.captionText.font; hint.fontSize = 18;
                hint.transform.SetSiblingIndex(choice.transform.GetSiblingIndex() + 1);
            } else {
                var hint = existingHint.GetComponent<Text>();
                if (hint != null) { hint.text = "相机请求采集尺寸；RTSP 限制本地输出；视频按源尺寸播放。"; EditorUtility.SetDirty(hint); }
            }
            EditorUtility.SetDirty(choice); EditorUtility.SetDirty(view);
        }
        /// <summary>独立目录生成，保留 Untitled 场景为备份；便于自动化测试和项目自定义路径。</summary>
        public static void GenerateAssetsAt(string folder)
        {
            if (!folder.StartsWith("Assets/", System.StringComparison.Ordinal) || folder.Contains("..") || folder.IndexOf('/', 7) >= 0)
                throw new System.ArgumentException("生成目录须为 Assets 的直接子目录。");
            if (!AssetDatabase.IsValidFolder(folder)) AssetDatabase.CreateFolder("Assets", folder.Substring(7));
            string scenePath = folder + "/HumanVisionSettingsDemo.unity", prefabPath = folder + "/HumanVisionSettingsDemo.prefab";
            if (AssetDatabase.LoadAssetAtPath<SceneAsset>(scenePath) != null || AssetDatabase.LoadAssetAtPath<GameObject>(prefabPath) != null) {
                Debug.Log("设置 Demo 已存在；可直接打开或在新场景执行 Create SDK Settings UI。不会覆盖现有布局。"); return;
            }
            Scene previous = SceneManager.GetActiveScene();
            if (previous.IsValid() && string.IsNullOrEmpty(previous.path)) {
                string backup = AssetDatabase.GenerateUniqueAssetPath(folder + "/PreviousScene.unity");
                if (!EditorSceneManager.SaveScene(previous, backup)) throw new System.IO.IOException("无法保留原 Untitled 场景。");
                Debug.Log("原 Untitled 场景已保留到 " + backup);
            }
            Scene scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Additive);
            try {
                SceneManager.SetActiveScene(scene); var root = CreateRoot();
                PrefabUtility.SaveAsPrefabAsset(root, prefabPath);
                if (!EditorSceneManager.SaveScene(scene, scenePath)) throw new System.IO.IOException("无法保存设置 Demo 场景。");
            } finally { EditorSceneManager.CloseScene(scene, true); if (previous.IsValid()) SceneManager.SetActiveScene(previous); }
            AssetDatabase.SaveAssets(); Debug.Log("已创建 " + scenePath + " 和可编辑 Prefab。");
        }
        /// <summary>自动化保存后直接复用此场景构建 Windows x64；无测试场景混入发行样例。</summary>
        public static void BuildWindowsVerification()
        {
            GenerateAssets();
            var path = System.Environment.GetEnvironmentVariable("HV_SDK_PLAYER_OUTPUT");
            if (string.IsNullOrEmpty(path)) throw new System.ArgumentException("HV_SDK_PLAYER_OUTPUT 未设置。");
            var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions { scenes = new[] { ScenePath }, locationPathName = path,
                target = BuildTarget.StandaloneWindows64, options = BuildOptions.Development });
            if (report.summary.result != UnityEditor.Build.Reporting.BuildResult.Succeeded) throw new System.InvalidOperationException("Windows SDK Demo 构建失败。");
        }
    }
}
