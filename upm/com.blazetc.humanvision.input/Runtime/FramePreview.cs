using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Input
{
    [RequireComponent(typeof(RawImage))]
    public sealed class FramePreview : MonoBehaviour
    {
        private IHumanVisionFrameSource source;
        private RawImage image;

        public void Bind(IHumanVisionFrameSource value)
        {
            source = value;
            if (image == null) image = GetComponent<RawImage>();
            Refresh();
        }

        public void Refresh()
        {
            if (image == null) image = GetComponent<RawImage>();
            var destroyed = source is UnityEngine.Object component && component == null;
            var texture = source == null || destroyed ? null : source.CurrentTexture;
            if (image.texture != texture) image.texture = texture;
        }

        private void LateUpdate() { Refresh(); }
        private void OnDisable() { if (image != null) image.texture = null; }
    }
}
