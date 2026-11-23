#if UNITY_EDITOR
using UnityEditor;

namespace HumanVision.Editor
{
    [InitializeOnLoad]
    internal static class RuntimeControllerRetirementEditor
    {
        static RuntimeControllerRetirementEditor()
        {
            AssemblyReloadEvents.beforeAssemblyReload += NativeControllerRetirement.RequestCloseAll;
            EditorApplication.quitting += NativeControllerRetirement.RequestCloseAll;
        }
    }
}
#endif
