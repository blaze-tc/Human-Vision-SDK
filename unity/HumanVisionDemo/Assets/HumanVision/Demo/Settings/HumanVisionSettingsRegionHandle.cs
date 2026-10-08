using System;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace HumanVision.Demo
{
    /// <summary>UI 区域使用 SDK 的左上角归一化坐标。右下角拖动缩放，其他位置拖动移动。</summary>
    public sealed class HumanVisionSettingsRegionHandle : MonoBehaviour, IBeginDragHandler, IDragHandler, IPointerDownHandler
    {
        public int Index;
        public bool Editable;
        public Action<int, Rect> Changed;
        public Rect Region { get; private set; }
        private Rect start;
        private Vector2 startPointer;
        private bool resize;
        private RectTransform Parent => (RectTransform)transform.parent;
        public void SetRegion(Rect region, bool editable)
        {
            Region = region; Editable = editable;
            var rect = (RectTransform)transform;
            rect.anchorMin = new Vector2(region.xMin, 1 - region.yMax);
            rect.anchorMax = new Vector2(region.xMax, 1 - region.yMin);
            rect.offsetMin = rect.offsetMax = Vector2.zero;
            GetComponent<Image>().raycastTarget = editable;
        }
        private bool Pointer(PointerEventData data, out Vector2 point)
        {
            if (!RectTransformUtility.ScreenPointToLocalPointInRectangle(Parent, data.position, data.pressEventCamera, out var local)) { point = default; return false; }
            var rect = Parent.rect;
            if (rect.width <= 0 || rect.height <= 0) { point = default; return false; }
            point = new Vector2((local.x-rect.xMin)/rect.width, (rect.yMax-local.y)/rect.height); return true;
        }
        public void OnBeginDrag(PointerEventData data)
        {
            if (!Editable || !Pointer(data, out startPointer)) return;
            start = Region;
            resize = Mathf.Abs((startPointer.x-start.xMax)*Parent.rect.width) < 28 && Mathf.Abs((startPointer.y-start.yMax)*Parent.rect.height) < 28;
        }
        public void OnPointerDown(PointerEventData data) { if(Editable) Changed?.Invoke(Index,Region); }
        public void OnDrag(PointerEventData data)
        {
            if (!Editable || !Pointer(data, out var point)) return;
            var next = MoveOrResize(start, point-startPointer, resize);
            SetRegion(next, true); Changed?.Invoke(Index, next);
        }
        /// <summary>左上归一化草稿几何；拖动限制在图像内部，组内重叠由 Apply 的整体验证拒绝。</summary>
        public static Rect MoveOrResize(Rect start, Vector2 delta, bool resize)
        {
            if (float.IsNaN(delta.x) || float.IsNaN(delta.y) || float.IsInfinity(delta.x) || float.IsInfinity(delta.y)) return start;
            var next = start;
            if (resize) { next.width = Mathf.Clamp(start.width+delta.x, .03f, 1-start.x); next.height = Mathf.Clamp(start.height+delta.y, .03f, 1-start.y); }
            else { next.x = Mathf.Clamp(start.x+delta.x, 0, 1-start.width); next.y = Mathf.Clamp(start.y+delta.y, 0, 1-start.height); }
            return next;
        }
    }
}

