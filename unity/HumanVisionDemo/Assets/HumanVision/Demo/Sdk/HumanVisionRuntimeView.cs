using System;
using System.Collections;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

namespace HumanVision
{
    /// <summary>A borrowed image and pose publication. Read or copy it on the Unity main thread.</summary>
    public readonly struct HumanVisionPresentedFrame
    {
        public ulong SourceId { get; }
        public ulong SourceGeneration { get; }
        /// <summary>The Runtime submission ID used by the skeleton result.</summary>
        public long SourceFrameId { get; }
        /// <summary>The original camera, video or RTSP publication ID.</summary>
        public long InputFrameId { get; }
        public long ResultSequence { get; }
        public long SourceTimestampUs { get; }
        public int BodyCount { get; }
        public Texture ColorTexture { get; }
        public RenderTexture OverlayTexture { get; }

        internal HumanVisionPresentedFrame(ulong sourceId, ulong generation, long sourceFrameId, long inputFrameId,
            long sequence, long timestampUs, int count, Texture color, RenderTexture overlay)
        {
            SourceId = sourceId; SourceGeneration = generation; SourceFrameId = sourceFrameId;
            InputFrameId = inputFrameId; ResultSequence = sequence; SourceTimestampUs = timestampUs;
            BodyCount = count; ColorTexture = color; OverlayTexture = overlay;
        }
    }

    /// <summary>Persistent, opt-in SDK owner and the source of matched pose and image data for gameplay.</summary>
    [DisallowMultipleComponent, RequireComponent(typeof(HumanVisionSdk)), DefaultExecutionOrder(200)]
    public sealed class HumanVisionRuntimeView : MonoBehaviour
    {
        private const string PrefabResource = "HumanVisionRuntime";
        public const double DisplayFrameHoldSeconds = .5d;
        [SerializeField] private Camera offscreenCamera;
        private HumanVisionRuntimeDrawing drawing;
        private HumanVisionPresentedFrame current;
        private VideoPlayerFrameSource currentBridge;
        private long currentRevision;
        private double lastComposedAt;
        private int currentWidth, currentHeight;
        private bool published, pairAvailable, unloading;
        private readonly HumanVisionCanonicalJoint[] joints = new HumanVisionCanonicalJoint[SkeletonImagePlane.JointCount];

        public static HumanVisionRuntimeView Instance { get; private set; }
        public HumanVisionSdk Sdk { get; private set; }
        public Camera OffscreenCamera => offscreenCamera;
        /// <summary>Keep the last composed overlay visible while this input waits for another matched result.</summary>
        public bool RetainLastOverlayDuringPairGaps { get; set; }
        /// <summary>Use the model-sized image for split RTSP pairs. Set before initializing the SDK.</summary>
        public bool UseAnalysisFrameForMatchedPreview
        {
            get => Sdk.CaptureAnalysisRuntimeViewFrames;
            set => Sdk.CaptureAnalysisRuntimeViewFrames = value;
        }
        /// <summary>Whether a composed image for the current binding stopped advancing for the preview timeout.</summary>
        public bool DisplayedFrameExpired => HasRetainedPreviewTimedOut &&
            Sdk.TryGetInputFrame(-1, out var input) && CanDisplay(in input);
        /// <summary>Exact texture retention requires a graphics fence. Live preview remains available without it.</summary>
        public bool SupportsMatchedFrames => SystemInfo.supportsGraphicsFence;
        /// <summary>Invoked on the main thread after a new matched result and its drawing have been submitted.</summary>
        public event Action<HumanVisionPresentedFrame> FramePresented;

        /// <summary>Create or reuse the package prefab. Start input explicitly via the returned Sdk.</summary>
        public static HumanVisionRuntimeView Create()
        {
            if (Instance != null) return Instance;
            var asset = Resources.Load<GameObject>(PrefabResource);
            var prefab = asset != null ? asset.GetComponent<HumanVisionRuntimeView>() : null;
            if (prefab == null) throw new InvalidOperationException("HumanVisionRuntime prefab is missing from the SDK package.");
            return Instantiate(prefab);
        }

        private void Awake()
        {
            Sdk = GetComponent<HumanVisionSdk>();
            if (Instance != null && Instance != this) {
                Sdk.enabled = false;
                Destroy(gameObject);
                return;
            }
            if (transform.parent != null) transform.SetParent(null, false);
            DontDestroyOnLoad(gameObject);
            Instance = this;
            Sdk.CaptureRuntimeViewFrames = true;
            if (offscreenCamera == null) {
                var cameraOwner = new GameObject("Offscreen Camera");
                cameraOwner.transform.SetParent(transform, false);
                cameraOwner.transform.localPosition = Vector3.back;
                offscreenCamera = cameraOwner.AddComponent<Camera>();
            }
            drawing = new HumanVisionRuntimeDrawing(transform, offscreenCamera);
        }

        /// <summary>Release input and native resources before destroying the persistent prefab.</summary>
        public IEnumerator Unload()
        {
            if (unloading || Instance != this) yield break;
            unloading = true;
            ClearFrame(true);
            yield return Sdk.Shutdown();
            Destroy(gameObject);
            yield return null;
        }

        /// <summary>Borrow the newest camera image independently of pose latency.</summary>
        public Texture GetLiveTexture() => Sdk != null ? Sdk.GetColorImageTex() : null;

        /// <summary>Get the currently drawn pair. False means no matched and fresh result. Do not release its textures.</summary>
        public bool TryGetFrame(out HumanVisionPresentedFrame frame)
        {
            frame = default;
            if (!pairAvailable || !Sdk.HasFreshResult || current.ColorTexture == null ||
                !Sdk.TryGetInputFrame(-1, out var input) || !CanDisplay(in input)) return false;
            frame = current;
            return true;
        }

        /// <summary>Borrow the displayed composition and its original frame identity. This display-only query leaves ColorTexture empty.</summary>
        public bool TryGetDisplayedFrame(out HumanVisionPresentedFrame frame)
        {
            frame = default;
            if (!published || HasRetainedPreviewTimedOut || (!pairAvailable && !RetainLastOverlayDuringPairGaps) ||
                !Sdk.TryGetInputFrame(-1, out var input) || !CanDisplay(in input) ||
                !current.OverlayTexture.IsCreated()) return false;
            frame = DisplayOnlyFrame();
            return true;
        }

        private HumanVisionPresentedFrame DisplayOnlyFrame() =>
            new HumanVisionPresentedFrame(current.SourceId, current.SourceGeneration, current.SourceFrameId,
                current.InputFrameId, current.ResultSequence, current.SourceTimestampUs, current.BodyCount,
                null, current.OverlayTexture);

        private bool HasRetainedPreviewTimedOut => RetainLastOverlayDuringPairGaps && published &&
            Time.realtimeSinceStartupAsDouble - lastComposedAt >= DisplayFrameHoldSeconds;

        private bool CanDisplay(in HumanVisionTextureFrame input) =>
            published && isActiveAndEnabled && !unloading && currentBridge == Sdk.FrameBridge &&
            currentRevision == Sdk.RecognitionRevision && input.SourceId == current.SourceId &&
            input.Generation == current.SourceGeneration && current.OverlayTexture != null &&
            input.Width == currentWidth && input.Height == currentHeight;

        /// <summary>Get the stable ID in a drawn slot. Slots can have gaps; use Sdk.GetMaxBodies() as the traversal limit.</summary>
        public bool TryGetBodyId(int index, out long id)
        {
            id = TryGetFrame(out _) ? drawing.GetBodyId(index) : 0;
            return id > 0;
        }

        /// <summary>Borrow the actual drawn joint object. Do not modify or retain it as a historical observation.</summary>
        public bool TryGetJointTransform(int index, HumanVisionCanonicalJointId joint, out Transform point)
        {
            point = TryGetFrame(out _) ? drawing.GetJoint(index, (int)joint) : null;
            return point != null;
        }

        /// <summary>Read the position used to draw this joint, in Unity world units on the image plane.</summary>
        public bool TryGetJointPosition(int index, HumanVisionCanonicalJointId joint, out Vector3 position)
        {
            position = default;
            if (!TryGetJointTransform(index, joint, out var point)) return false;
            position = point.position;
            return true;
        }

        private void LateUpdate()
        {
            if (drawing == null || unloading) return;
            if (!Sdk.TryGetRuntimeViewResult(out var input, out var color, out long inputFrameId)) {
                if (RetainLastOverlayDuringPairGaps && CanDisplay(in input)) {
                    if (pairAvailable) current = DisplayOnlyFrame();
                    pairAvailable = false;
                    return;
                }
                ClearFrame(!Sdk.IsRunning);
                return;
            }

            long sequence = Sdk.ResultSequence;
            long frameId = Sdk.SourceFrameId;
            bool isNew = !published || currentBridge != Sdk.FrameBridge || currentRevision != Sdk.RecognitionRevision ||
                sequence != current.ResultSequence || input.SourceId != current.SourceId || input.Generation != current.SourceGeneration;
            bool changed = drawing.SetSize(color.width, color.height);
            int count = 0;
            int capacity = Sdk.GetMaxBodies();
            for (int slot = 0; slot < HumanVisionRuntimeDrawing.Capacity; slot++) {
                if (slot < capacity && Sdk.CopySkeletonByIndex(slot, joints, out var metadata) &&
                    metadata.ResultSequence == sequence && metadata.SourceFrameId == frameId) {
                    changed |= drawing.SetBody(slot, metadata.StableTrackId, joints);
                    count++;
                } else changed |= drawing.HideBody(slot);
            }

            if (isNew || changed) drawing.Draw(color);
            current = new HumanVisionPresentedFrame(input.SourceId, input.Generation, frameId, inputFrameId,
                sequence, Sdk.SourceTimestampUs, count, color, drawing.Target);
            currentBridge = Sdk.FrameBridge;
            currentRevision = Sdk.RecognitionRevision;
            currentWidth = input.Width;
            currentHeight = input.Height;
            published = true;
            pairAvailable = true;
            if (isNew) {
                lastComposedAt = Time.realtimeSinceStartupAsDouble;
                FramePresented?.Invoke(current);
            }
        }

        private void ClearFrame(bool release)
        {
            if (published || release && drawing?.Target != null) drawing?.Clear(release);
            current = default; currentBridge = null; published = pairAvailable = false;
        }

        private void OnDisable() => ClearFrame(true);
        private void OnDestroy()
        {
            if (Instance == this) Instance = null;
            drawing?.Dispose();
        }
    }
}
