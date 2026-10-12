using System;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVision.Demo
{
    /// <summary>Exceptional readback failure must not free a queued copy's target.
    /// This owner survives the source component and releases only after GPU completion.</summary>
    internal sealed class DeferredGpuTextureRelease : MonoBehaviour
    {
        private RenderTexture texture;
        private GraphicsFence fence;
        internal static void Retire(RenderTexture texture, GraphicsFence fence)
        {
            try { if (fence.passed) { Release(texture); return; } }
            catch (InvalidOperationException) { }
            catch (NotSupportedException) { }
            var owner=new GameObject("HumanVision failed copy retirement") { hideFlags=HideFlags.HideAndDontSave };
            if(Application.isPlaying)DontDestroyOnLoad(owner);
            var pending=owner.AddComponent<DeferredGpuTextureRelease>();
            pending.texture=texture;pending.fence=fence;
        }
        private void Update()
        {
            try { if(!fence.passed)return; }
            catch(InvalidOperationException) { return; }
            catch(NotSupportedException) { return; }
            Release(texture);texture=null;
            if(Application.isPlaying)Destroy(gameObject);else DestroyImmediate(gameObject);
        }
        private static void Release(RenderTexture value)
        {
            value.Release();
            if(Application.isPlaying)Destroy(value);else DestroyImmediate(value);
        }
    }
}
