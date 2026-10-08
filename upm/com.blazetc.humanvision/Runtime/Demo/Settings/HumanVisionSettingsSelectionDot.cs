using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Demo
{
    /// <summary>下拉选中标记：直接绘制圆点，无需额外图片；Toggle 控制显示和隐藏。</summary>
    [RequireComponent(typeof(CanvasRenderer))]
    public sealed class HumanVisionSettingsSelectionDot : MaskableGraphic
    {
        public override Texture mainTexture => Texture2D.whiteTexture;
        protected override void OnPopulateMesh(VertexHelper helper)
        {
            helper.Clear();
            var rect=rectTransform.rect; var center=rect.center;
            float radius=Mathf.Min(rect.width,rect.height)*.5f;
            const int segments=24;
            helper.AddVert(center,color,Vector2.zero);
            for(int i=0;i<segments;i++) {
                float angle=i*Mathf.PI*2/segments;
                helper.AddVert(center+new Vector2(Mathf.Cos(angle),Mathf.Sin(angle))*radius,color,Vector2.zero);
            }
            for(int i=0;i<segments;i++) helper.AddTriangle(0,i+1,(i+1)%segments+1);
        }
    }
}
