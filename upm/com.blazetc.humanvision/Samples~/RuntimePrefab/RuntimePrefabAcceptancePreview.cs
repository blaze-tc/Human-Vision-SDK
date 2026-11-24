using System;
using System.Collections;
using System.IO;
using HumanVision;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVisionAcceptance
{
    public sealed partial class RuntimePrefabAcceptance
    {
        private readonly RuntimePrefabLiveSkeleton liveSkeleton = new RuntimePrefabLiveSkeleton();
        private bool fullscreenPreview;
        private bool fullscreenCrop;
        private bool photoCaptureActive;

        private HumanVisionRuntimePresentationMode OrdinaryPresentationMode =>
            HumanVisionRuntimePresentationMode.Disabled;

        private void RefreshPresentationMode()
        {
            if (view != null && !photoCaptureActive) view.PresentationMode = OrdinaryPresentationMode;
        }

        private void RefreshLivePreview(HumanVisionSdk sdk)
        {
            input = default;
            hasInput = sdk != null && sdk.InputState == InputSourceState.Streaming &&
                sdk.TryGetInputFrame(-1, out input) && input.Texture != null && input.Width > 0 && input.Height > 0;
            if (startup != null && !startup.PreviewEnabled) hasInput = false;
            live = hasInput ? input.Texture : null;
            if (showSkeleton) liveSkeleton.Sample(sdk);
            else liveSkeleton.Clear();
        }

        private void SetFullscreenPreview(bool enabled)
        {
            fullscreenPreview = enabled;
            RefreshPresentationMode();
        }

        private Material previewColorMaterial;

        private bool CanTakePhoto {
            get {
                return PhotoUnavailableReason() == "可用";
            }
        }

        private IEnumerator TakeExactPhoto()
        {
            Debug.Log($"[HVLivePreviewEval] event=photo_attempt source={input.SourceId} generation={input.Generation} " +
                $"input_frame={input.FrameId} input_available={hasInput} fence={view != null && view.SupportsMatchedFrames} " +
                $"async_readback={SystemInfo.supportsAsyncGPUReadback}");
            if (!CanTakePhoto) {
                status = "没有可拍照的输入";
                Debug.LogWarning("[HVLivePreviewEval] event=photo_failed reason=unavailable");
                yield break;
            }
            photoCaptureActive = true;
            ulong source = input.SourceId;
            ulong generation = input.Generation;
            int width = input.Width, height = input.Height;
            long firstInput = input.FrameId;
            view.PresentationMode = HumanVisionRuntimePresentationMode.MatchedColor;
            status = "正在等待同帧照片…";
            try {
                double deadline = Time.realtimeSinceStartupAsDouble + 10;
                while (Time.realtimeSinceStartupAsDouble < deadline) {
                    if (!hasInput || input.SourceId != source || input.Generation != generation ||
                        input.Width != width || input.Height != height) {
                        status = "输入已变化，请重新拍照";
                        Debug.LogWarning("[HVLivePreviewEval] event=photo_failed reason=input_changed");
                        yield break;
                    }
                    if (view.TryGetFrame(out var photo) && photo.SourceId == source &&
                        photo.SourceGeneration == generation && photo.InputFrameId >= firstInput &&
                        photo.ColorTexture != null && photo.ColorTexture.width == width &&
                        photo.ColorTexture.height == height) {
                        bool finished = false;
                        QueueExactPhoto(in photo, () => finished = true);
                        while (!finished) yield return null;
                        yield break;
                    }
                    yield return null;
                }
                status = "没有及时取得同帧照片";
                Debug.LogWarning("[HVLivePreviewEval] event=photo_failed reason=exact_color_timeout");
            } finally {
                photoCaptureActive = false;
                RefreshPresentationMode();
            }
        }

        private void QueueExactPhoto(in HumanVisionPresentedFrame photo, Action completed)
        {
            // Copy once while the SDK's exact texture is borrowed. The asynchronous
            // readback owns this independent snapshot until its callback finishes.
            int width = photo.ColorTexture.width, height = photo.ColorTexture.height;
            ulong source = photo.SourceId, generation = photo.SourceGeneration;
            long inputId = photo.InputFrameId, resultFrame = photo.SourceFrameId, sequence = photo.ResultSequence;
            var inputTexture = photo.ColorTexture;
            var inputTarget = inputTexture as RenderTexture;
            bool srgb = inputTarget != null && inputTarget.sRGB;
            var snapshot = RenderTexture.GetTemporary(width, height, 0, RenderTextureFormat.ARGB32,
                srgb ? RenderTextureReadWrite.sRGB : RenderTextureReadWrite.Linear);
            try {
                bool copy = inputTarget != null && inputTarget.antiAliasing == 1 &&
                    inputTexture.graphicsFormat == snapshot.graphicsFormat &&
                    (SystemInfo.copyTextureSupport & CopyTextureSupport.Basic) != 0;
                if (copy) Graphics.CopyTexture(inputTexture, snapshot);
                else {
                    bool previousSrgbWrite = GL.sRGBWrite;
                    try {
                        GL.sRGBWrite = snapshot.sRGB && QualitySettings.activeColorSpace == ColorSpace.Linear;
                        Graphics.Blit(inputTexture, snapshot);
                    } finally { GL.sRGBWrite = previousSrgbWrite; }
                }
                Debug.Log($"[HVLivePreviewEval] event=photo_copy copy={(copy ? "texture" : "blit")} source_format={inputTexture.graphicsFormat} " +
                    $"snapshot_format={snapshot.graphicsFormat} source_srgb={srgb} snapshot_srgb={snapshot.sRGB}");
                var readbackFormat = snapshot.sRGB ? UnityEngine.Experimental.Rendering.GraphicsFormat.R8G8B8A8_SRGB :
                    UnityEngine.Experimental.Rendering.GraphicsFormat.R8G8B8A8_UNorm;
                AsyncGPUReadback.Request(snapshot, 0, readbackFormat, request => {
                    Texture2D export = null;
                    try {
                        if (this == null) return;
                        if (request.hasError) {
                            status = "照片回读失败";
                            Debug.LogWarning("[HVLivePreviewEval] event=photo_failed reason=async_readback");
                            return;
                        }
                        export = new Texture2D(width, height, TextureFormat.RGBA32, false);
                        export.LoadRawTextureData(request.GetData<byte>());
                        string folder = Path.Combine(Application.persistentDataPath, "runtime-photos");
                        Directory.CreateDirectory(folder);
                        string file = Path.Combine(folder, $"photo-{DateTime.UtcNow:yyyyMMdd-HHmmss-fffffff}.png");
                        File.WriteAllBytes(file, export.EncodeToPNG());
                        status = $"照片已保存 · {width}×{height} · 输入 #{inputId}";
                        Debug.Log($"[HVLivePreviewEval] event=photo exact=1 image_size={width}x{height} input_frame={inputId} " +
                            $"result_frame={resultFrame} result_sequence={sequence} source={source} generation={generation} file={Path.GetFileName(file)}");
                    } catch (Exception error) {
                        status = "保存照片失败";
                        Debug.LogWarning("Runtime Prefab photo export failed: " + error.GetType().Name);
                    } finally {
                        if (export != null) Destroy(export);
                        RenderTexture.ReleaseTemporary(snapshot);
                        completed();
                    }
                });
            } catch (Exception error) {
                RenderTexture.ReleaseTemporary(snapshot);
                status = "提交照片回读失败";
                Debug.LogWarning("Runtime Prefab photo readback failed: " + error.GetType().Name);
                completed();
            }
        }
    }
}
