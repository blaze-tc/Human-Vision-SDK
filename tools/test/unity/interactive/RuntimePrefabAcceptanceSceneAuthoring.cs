#if UNITY_EDITOR
using System;
using System.Linq;
using HumanVision;
using HumanVisionAcceptance;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

// 装配已存在的验收场景。运行时使用保存好的层级和 Inspector 引用。
public static class RuntimePrefabAcceptanceSceneAuthoring
{
    private const string ScenePath = "Assets/HumanVision/Samples/RuntimePrefab/RuntimePrefabAcceptance.unity";
    private const string RuntimePrefab = "Assets/HumanVision/Demo/Resources/HumanVisionRuntime.prefab";

    [MenuItem("Human Vision/Acceptance/Prepare Scene References")]
    public static void PrepareSceneReferences()
    {
        Scene scene = SceneManager.GetActiveScene();
        if (EditorApplication.isPlayingOrWillChangePlaymode || scene.path != ScenePath || scene.isDirty)
            throw new InvalidOperationException("请在编辑模式打开已保存的 RuntimePrefabAcceptance 场景。");

        var owner = scene.GetRootGameObjects().Select(root => root.GetComponent<RuntimePrefabAcceptance>())
            .SingleOrDefault(value => value != null);
        if (owner == null)
            throw new InvalidOperationException("没有找到 RuntimePrefabAcceptance 控制器。");

        var current = new SerializedObject(owner);
        if (current.FindProperty("sceneRuntimeView").objectReferenceValue != null)
            throw new InvalidOperationException("此场景已配置 Inspector 引用，请直接在 Hierarchy 和 Inspector 中维护。");

        var runtime = ResolveRuntimeView(scene);
        var shader = Shader.Find("Sprites/Default");
        if (runtime.OffscreenCamera == null || shader == null)
            throw new InvalidOperationException("缺少 SDK 离屏相机或骨骼着色器。");

        bool skeletonAlreadyAuthored = current.FindProperty("skeletonCamera").objectReferenceValue != null;
        try
        {
            if (skeletonAlreadyAuthored)
            {
                owner.BakeRuntimeView(runtime);
            }
            else
            {
                var skeleton = new GameObject("Live Skeleton Scene");
                SceneManager.MoveGameObjectToScene(skeleton, scene);
                var font = Resources.GetBuiltinResource<Font>("Arial.ttf");
                owner.BakeSceneLayout(font, runtime, skeleton.transform, shader);
            }

            // 旧值属于本机临时样本。场景不携带设备地址，成功连接后保存到本机用户设置。
            var settings = new SerializedObject(owner);
            var rtsp = settings.FindProperty("rtspUrl");
            if (rtsp.stringValue == "rtsp://192.168.1.40:554/Exercise-11.mp4")
            {
                rtsp.stringValue = "";
                settings.ApplyModifiedPropertiesWithoutUndo();
            }

            EditorUtility.SetDirty(owner);
            EditorSceneManager.MarkSceneDirty(scene);
            if (!EditorSceneManager.SaveScene(scene))
                throw new InvalidOperationException("验收场景未能保存。");
            Selection.activeObject = owner.gameObject;
        }
        catch
        {
            // 磁盘里的原场景仍可通过 Unity 重新打开，现场保留供排查。
            EditorSceneManager.MarkSceneDirty(scene);
            throw;
        }
    }

    private static HumanVisionRuntimeView ResolveRuntimeView(Scene scene)
    {
        var views = scene.GetRootGameObjects().SelectMany(root => root.GetComponentsInChildren<HumanVisionRuntimeView>(true)).ToArray();
        if (views.Length > 1)
            throw new InvalidOperationException("场景存在多份 HumanVisionRuntime，请先保留唯一的场景实例。");
        if (views.Length == 1)
            return views[0];

        var asset = AssetDatabase.LoadAssetAtPath<GameObject>(RuntimePrefab);
        if (asset == null)
            throw new InvalidOperationException("找不到 HumanVisionRuntime Prefab。");
        var instance = (GameObject)PrefabUtility.InstantiatePrefab(asset, scene);
        return instance.GetComponent<HumanVisionRuntimeView>();
    }
}
#endif
