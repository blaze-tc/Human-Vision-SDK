using UnityEditor;
using UnityEngine;

namespace HumanVision.Editor
{
    /// <summary>在当前场景创建仅需一个用户总控的 SDK；不强制创建显示 UI。</summary>
    public static class HumanVisionSdkMenu
    {
        /// <summary>支持 Hierarchy 上下文父对象和 Undo；配置通过总控 Inspector 修改。</summary>
        [MenuItem("HumanVision/Create SDK", false, 0)]
        [MenuItem("GameObject/Human Vision/Create SDK", false, 10)]
        public static void CreateSdk(MenuCommand command)
        {
            Undo.IncrementCurrentGroup();
            int group = Undo.GetCurrentGroup();
            Undo.SetCurrentGroupName("Create Human Vision SDK");
            var owner = new GameObject("Human Vision SDK");
            GameObjectUtility.SetParentAndAlign(owner, command.context as GameObject);
            // 先设置父对象，再记录完整对象快照，避免 Undo 恢复悬空的父指针。
            owner.AddComponent<HumanVisionSdk>();
            Undo.RegisterCreatedObjectUndo(owner, "Create Human Vision SDK");
            Selection.activeGameObject = owner;
            Undo.CollapseUndoOperations(group);
        }
    }
}
