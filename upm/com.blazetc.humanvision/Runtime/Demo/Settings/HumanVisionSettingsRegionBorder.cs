using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Demo
{
    /// <summary>真正的空心线框；不用 Outline 复制整个 Image 四边，避免遮住视频和骨骼。</summary>
    [RequireComponent(typeof(CanvasRenderer))]
    public sealed class HumanVisionSettingsRegionBorder : MaskableGraphic
    {
        [SerializeField, Min(1)] private float lineWidth = 5;
        protected override void OnPopulateMesh(VertexHelper helper)
        {
            helper.Clear(); var bounds = rectTransform.rect;
            BuildFrame(helper,bounds,lineWidth+3,Color.black);
            // 在黑色衬线内画彩色边，复杂背景上依然明显，框内不生成任何顶点。
            bounds.xMin+=1.5f; bounds.xMax-=1.5f; bounds.yMin+=1.5f; bounds.yMax-=1.5f;
            BuildFrame(helper,bounds,lineWidth,color);
        }
        public static void BuildFrame(VertexHelper helper,Rect bounds,float width,Color color)
        {
            width=Mathf.Min(width,Mathf.Min(bounds.width,bounds.height)*.5f);
            Quad(helper,new Rect(bounds.xMin,bounds.yMin,bounds.width,width),color);
            Quad(helper,new Rect(bounds.xMin,bounds.yMax-width,bounds.width,width),color);
            Quad(helper,new Rect(bounds.xMin,bounds.yMin+width,width,bounds.height-2*width),color);
            Quad(helper,new Rect(bounds.xMax-width,bounds.yMin+width,width,bounds.height-2*width),color);
        }
        private static void Quad(VertexHelper helper,Rect rect,Color32 tint)
        {
            int first=helper.currentVertCount;
            helper.AddVert(new Vector2(rect.xMin,rect.yMin),tint,Vector2.zero); helper.AddVert(new Vector2(rect.xMin,rect.yMax),tint,Vector2.zero);
            helper.AddVert(new Vector2(rect.xMax,rect.yMax),tint,Vector2.zero); helper.AddVert(new Vector2(rect.xMax,rect.yMin),tint,Vector2.zero);
            helper.AddTriangle(first,first+1,first+2); helper.AddTriangle(first,first+2,first+3);
        }
    }
}
