using System;
using System.Threading;
using UnityEngine;

namespace HumanVision.Input
{
    /// <summary>GPU-only upright Unity-bottom-left pixels. Rotate clockwise, then mirror for display.</summary>
    public sealed class FrameTextureNormalizer : IDisposable
    {
        private readonly int threadId = Thread.CurrentThread.ManagedThreadId;
        private static readonly int TransformId = Shader.PropertyToID("_Transform");
        private Material material;
        private RenderTexture texture;

        public RenderTexture CurrentTexture { get { CheckThread(); return texture; } }

        // In Linear projects the shader samples sRGB inputs as linear; in Gamma it preserves
        // their sRGB code values. The linear RT avoids a second encoding on write.
        public FrameColorSpace OutputColorSpace { get; private set; }
        internal static FrameColorSpace EncodingFor(Texture input)
        {
            return QualitySettings.activeColorSpace == ColorSpace.Gamma && input.isDataSRGB
                ? FrameColorSpace.Srgb : FrameColorSpace.Linear;
        }

        public RenderTexture Update(Texture input, int rotationDegrees, bool verticalMirror, bool displayMirror)
        {
            Prepare(input, rotationDegrees);
            material.SetVector(TransformId, new Vector4(rotationDegrees / 90, verticalMirror ? 1 : 0, displayMirror ? 1 : 0, 0));
            var previous = RenderTexture.active;
            try { Graphics.Blit(input, texture, material); }
            finally { RenderTexture.active = previous; }
            return texture;
        }

        // Allocate/register/reserve retirement before queueing the first GPU command.
        internal RenderTexture Prepare(Texture input, int rotationDegrees)
        {
            CheckThread();
            if (input == null) throw new ArgumentNullException(nameof(input));
            OutputColorSpace = EncodingFor(input);
            if (rotationDegrees != 0 && rotationDegrees != 90 && rotationDegrees != 180 && rotationDegrees != 270)
                throw new ArgumentOutOfRangeException(nameof(rotationDegrees));
            int width = rotationDegrees % 180 == 0 ? input.width : input.height;
            int height = rotationDegrees % 180 == 0 ? input.height : input.width;
            if (width < 1 || height < 1) throw new ArgumentException("Input texture has no pixels.");
            if (material == null)
            {
                var shader = Resources.Load<Shader>("HumanVisionInputOrientation");
                if (shader == null || !shader.isSupported)
                    throw new InvalidOperationException("Human Vision input orientation shader is unavailable on this graphics API.");
                material = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
            }
            if (texture == null || texture.width != width || texture.height != height)
            {
                Destroy(texture);
                texture = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear)
                {
                    name = "HumanVision input", filterMode = FilterMode.Point, wrapMode = TextureWrapMode.Clamp
                };
                if (!texture.Create()) throw new InvalidOperationException("Could not create input RenderTexture.");
            }
            return texture;
        }

        public void Dispose()
        {
            CheckThread();
            Destroy(texture);
            texture = null;
            Destroy(material);
            material = null;
        }

        private void CheckThread()
        {
            if (Thread.CurrentThread.ManagedThreadId != threadId)
                throw new InvalidOperationException("Input textures must be managed on their owning Unity main thread.");
        }

        internal static void Destroy(UnityEngine.Object value)
        {
            if (value == null) return;
            var ownedTarget = value as RenderTexture;
            if (ownedTarget != null && RenderTexture.active == ownedTarget) RenderTexture.active = null;
            if (Application.isPlaying) UnityEngine.Object.Destroy(value);
            else UnityEngine.Object.DestroyImmediate(value);
        }
    }

    /// <summary>One reusable registration per source, retained independently of its MonoBehaviour.</summary>
    internal sealed class InputRetirementRegistration
    {
        internal readonly SourceRetirement Owner;
        internal readonly Action OnProgress;
        internal readonly Func<bool> HasPendingWorker;
        internal InputRetirementRegistration Next;
        internal bool Queued;
        internal InputRetirementRegistration(SourceRetirement owner, Action onProgress, Func<bool> hasPendingWorker)
        { Owner = owner; OnProgress = onProgress; HasPendingWorker = hasPendingWorker; }
    }

    /// <summary>Persistent main-thread polling continues after source disable/destroy.</summary>
    internal sealed class InputRetirementPump : MonoBehaviour
    {
        private static InputRetirementPump instance;
        private InputRetirementRegistration head;

        private static void EnsureInstance()
        {
            if (instance == null)
            {
                var go = new GameObject("HumanVision input retirement") { hideFlags = HideFlags.HideAndDontSave };
                DontDestroyOnLoad(go);
                instance = go.AddComponent<InputRetirementPump>();
            }
        }

        internal static void Watch(InputRetirementRegistration registration)
        {
            if (registration.Queued) return;
            EnsureInstance();
            registration.Next = instance.head;
            registration.Queued = true;
            instance.head = registration;
        }

        private void Update()
        {
            InputRetirementRegistration previous = null;
            var current = head;
            while (current != null)
            {
                var next = current.Next;
                current.Owner.Poll();
                current.OnProgress();
                if (current.Owner.PendingResourceCount == 0 && !current.HasPendingWorker())
                {
                    if (previous == null) head = next;
                    else previous.Next = next;
                    current.Next = null;
                    current.Queued = false;
                }
                else previous = current;
                current = next;
            }
        }
    }
}
