using HumanVision;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        [Header("SDK 场景对象")]
        [SerializeField] private HumanVisionRuntimeView sceneRuntimeView;

        [Header("骨骼场景对象")]
        [SerializeField] private Camera skeletonCamera;
        [SerializeField] private Shader skeletonLineShader;
        [SerializeField] private RuntimePrefabSceneSkeleton.Body[] skeletonBodies;

        [Header("界面文字")]
        [SerializeField] private Text[] sceneLabels;

        private Font displayFont;
        private RuntimePrefabSceneSkeleton sceneSkeleton;
        private bool rememberConnectedInput;

        private bool BindSceneSkeleton()
        {
            if (sceneSkeleton != null)
                return true;

            var renderer = new RuntimePrefabSceneSkeleton();
            if (!renderer.Bind(skeletonCamera, skeletonBodies, skeletonLineShader))
            {
                Debug.LogError("RuntimePrefabAcceptance 缺少骨骼相机、着色器或骨骼节点的 Inspector 引用。", this);
                return false;
            }
            sceneSkeleton = renderer;
            return true;
        }

        private void ApplySceneFont()
        {
            if (sceneLabels == null || sceneLabels.Length == 0)
                return;

            // 沿用设备的中文字体。字体仅初始化一次，所有 Text 在场景中已引用。
            displayFont = Font.CreateDynamicFontFromOSFont(new[]
            {
                "Microsoft YaHei UI", "Microsoft YaHei", "Noto Sans CJK SC", "Droid Sans Fallback", "Arial"
            }, 22);
            if (displayFont == null)
                return;

            foreach (Text label in sceneLabels)
            {
                if (label != null)
                    label.font = displayFont;
            }
        }

        private void DisposeSceneFont()
        {
            if (displayFont != null)
                Destroy(displayFont);
        }

    }
}
