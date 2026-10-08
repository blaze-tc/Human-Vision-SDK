using UnityEditor;
using UnityEngine;

public static class SdkBasicUsageMenu
{
    [MenuItem("GameObject/Human Vision/SDK Starter", false, 10)]
    public static void Create(MenuCommand command)
    {
        var root = new GameObject("Human Vision Starter");
        Undo.RegisterCreatedObjectUndo(root, "Create Human Vision Starter");
        GameObjectUtility.SetParentAndAlign(root, command.context as GameObject);
        root.AddComponent<SdkBasicUsage>();
        Selection.activeGameObject = root;
    }
}
