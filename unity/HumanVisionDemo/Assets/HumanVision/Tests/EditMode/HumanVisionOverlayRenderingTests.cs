using System.Reflection;
using HumanVision.Demo;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class HumanVisionOverlayRenderingTests
    {
        [Test]
        public void InvalidPresentationRestoresRendererMaterialOnNextRebuild()
        {
            var canvasObject = new GameObject("Overlay Test Canvas", typeof(Canvas));
            try
            {
                var overlayObject = new GameObject("Overlay", typeof(RectTransform), typeof(CanvasRenderer), typeof(HumanVisionOverlay));
                overlayObject.transform.SetParent(canvasObject.transform, false);
                var overlay = overlayObject.GetComponent<HumanVisionOverlay>();
                overlay.SetMaterialDirty();
                overlay.Rebuild(CanvasUpdate.PreRender);
                Assert.That(overlay.canvasRenderer.materialCount, Is.EqualTo(1));

                // Startup/loop invalidation uses this same event path. No inference is needed.
                typeof(HumanVisionOverlay).GetMethod("OnPresentationFrameChanged",
                    BindingFlags.Instance | BindingFlags.NonPublic).Invoke(overlay, null);
                Assert.That(overlay.canvasRenderer.materialCount, Is.Zero);
                overlay.Rebuild(CanvasUpdate.PreRender);

                Assert.That(overlay.canvasRenderer.materialCount, Is.EqualTo(1),
                    "Clearing a stale mesh must not permanently remove its rendering material.");
                Assert.That(overlay.canvasRenderer.GetMaterial(), Is.Not.Null);
            }
            finally
            {
                Object.DestroyImmediate(canvasObject);
            }
        }
    }
}
