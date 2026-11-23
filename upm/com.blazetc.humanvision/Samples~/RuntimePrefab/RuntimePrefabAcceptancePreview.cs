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

        private void DrawUnifiedPreviewCard(Rect left, Rect right)
        {
            var card = right;
            DrawCard(card, "实时预览 · 骨骼与识别区域", live, inputLine, showRegions: true, drawSkeleton: showSkeleton);
            DrawFullscreenButton(card);
        }

        private void DrawPreviewImage(Rect image, Texture texture, bool showRegions, bool drawSkeleton, float strokeWidth = 2)
        {
            if (texture != null) DrawCameraImage(image, texture);
            if (showRegions && recognition != null && recognition.UseRegions) DrawRegions(image);
            if (drawSkeleton) liveSkeleton.Draw(image, strokeWidth);
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

        private void DrawFullscreenButton(Rect card)
        {
            var matrix = GUI.matrix;
            float right = card.xMax - 17;
            if (Screen.safeArea.xMax > 0 && matrix.m00 > 0)
                right = Mathf.Min(right, (Screen.safeArea.xMax - matrix.m03) / matrix.m00 - 8);
            if (GUI.Button(new Rect(right - 125, card.y + 9, 125, 34), "全屏预览", button))
                SetFullscreenPreview(true);
        }

        private static void GetExpandedCards(float scale, out Rect left, out Rect right, out Rect controls)
        {
            float availableWidth = Screen.safeArea.width > 0 ? Screen.safeArea.width : Screen.width;
            float extraSide = Mathf.Max(0, (availableWidth / scale - 1440f) * .5f);
            left = new Rect(LiveCard.x - extraSide, LiveCard.y, LiveCard.width, LiveCard.height);
            right = new Rect(PoseCard.x - extraSide, PoseCard.y, PoseCard.width + extraSide * 2, PoseCard.height);
            controls = left;
        }

        private void SetFullscreenPreview(bool enabled)
        {
            fullscreenPreview = enabled;
            capacityMenuOpen = false;
        }

        private void DrawFullscreenPreview()
        {
            var savedMatrix = GUI.matrix;
            GUI.matrix = Matrix4x4.identity;
            try {
                var screen = new Rect(0, 0, Screen.width, Screen.height);
                Box(screen, Color.black);
                var safeArea = Screen.safeArea;
                if (safeArea.width <= 0 || safeArea.height <= 0) safeArea = screen;
                float scale = Mathf.Clamp(Mathf.Min(safeArea.width / 470f, safeArea.height / 720f), .55f, 2);
                if (live != null || showSkeleton || recognition != null && recognition.UseRegions) {
                    int width = live != null ? live.width : 1280;
                    int height = live != null ? live.height : 720;
                    Rect image = RuntimePrefabLiveSkeleton.Fit(screen, width, height, fullscreenCrop);
                    DrawPreviewImage(image, live, true, showSkeleton, 2 * scale);
                } else {
                    GUI.Label(screen, "等待当前码流", badge);
                }
                float controlsRight = safeArea.xMax - 13 * scale;
                float controlsTop = Screen.height - safeArea.yMax + 12 * scale;
                var control = new Rect(controlsRight - 122 * scale, controlsTop, 122 * scale, 36 * scale);
                if (GUI.Button(control, "退出全屏", button) ||
                    Event.current.type == EventType.KeyDown && Event.current.keyCode == KeyCode.Escape) {
                    SetFullscreenPreview(false);
                }
                control.x -= 129 * scale;
                if (GUI.Button(control, showSkeleton ? "关闭骨骼" : "开启骨骼", button)) showSkeleton = !showSkeleton;
                control.x -= 129 * scale;
                if (GUI.Button(control, fullscreenCrop ? "恢复等比" : "填满裁切", button)) fullscreenCrop = !fullscreenCrop;
            } finally { GUI.matrix = savedMatrix; }
        }

        private void DrawCameraImage(Rect image, Texture texture, bool drawSkeleton, float strokeWidth = 2)
        {
            DrawCameraImage(image, texture);
            if (drawSkeleton) liveSkeleton.Draw(image, strokeWidth);
        }

        private static Rect CardImageWindow(Rect card) => new Rect(card.x + 16, card.y + 49, card.width - 32, Mathf.Max(1, card.height - 100));

        private Material previewColorMaterial;

        private void DrawCameraImage(Rect image, Texture texture)
        {
            if (Event.current.type != EventType.Repaint) return;
            // Android hardware RTSP delivers display RGB codes in a non-sRGB storage texture.
            if (QualitySettings.activeColorSpace == ColorSpace.Linear && texture == input.Texture &&
                Application.platform == RuntimePlatform.Android && input.TimestampKind == FrameTimestampKind.LocalDecode &&
                input.ColorSpace != FrameColorSpace.Linear &&
                texture is RenderTexture renderTexture && !renderTexture.sRGB) {
                if (previewColorMaterial == null) {
                    var shader = Resources.Load<Shader>("HumanVisionRuntimePreviewColor");
                    if (shader != null && shader.isSupported) previewColorMaterial = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
                }
                if (previewColorMaterial != null) {
                    var matrix = GUI.matrix;
                    var topLeft = (Vector2)matrix.MultiplyPoint3x4(image.min);
                    var bottomRight = (Vector2)matrix.MultiplyPoint3x4(image.max);
                    GUI.matrix = Matrix4x4.identity;
                    Graphics.DrawTexture(new Rect(topLeft, bottomRight - topLeft), texture, previewColorMaterial);
                    GUI.matrix = matrix;
                    return;
                }
            }
            GUI.DrawTexture(image, texture, ScaleMode.StretchToFill, false);
        }

        private bool CanTakePhoto => !photoCaptureActive && hasInput && view != null &&
            view.SupportsMatchedFrames && SystemInfo.supportsAsyncGPUReadback;

        private IEnumerator TakeExactPhoto()
        {
            Debug.Log($"[RuntimePrefabPhoto] request source={input.SourceId} generation={input.Generation} " +
                $"input_frame={input.FrameId} input_available={hasInput} fence={view != null && view.SupportsMatchedFrames} " +
                $"async_readback={SystemInfo.supportsAsyncGPUReadback}");
            if (!CanTakePhoto) {
                status = "没有可拍照的输入";
                Debug.LogWarning("[RuntimePrefabPhoto] unavailable");
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
                        Debug.LogWarning("[RuntimePrefabPhoto] input_changed");
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
                Debug.LogWarning("[RuntimePrefabPhoto] timed_out_waiting_for_exact_color");
            } finally {
                photoCaptureActive = false;
                if (view != null) view.PresentationMode = HumanVisionRuntimePresentationMode.Disabled;
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
                Debug.Log($"[RuntimePrefabPhoto] copy={(copy ? "texture" : "blit")} source_format={inputTexture.graphicsFormat} " +
                    $"snapshot_format={snapshot.graphicsFormat} source_srgb={srgb} snapshot_srgb={snapshot.sRGB}");
                var readbackFormat = snapshot.sRGB ? UnityEngine.Experimental.Rendering.GraphicsFormat.R8G8B8A8_SRGB :
                    UnityEngine.Experimental.Rendering.GraphicsFormat.R8G8B8A8_UNorm;
                AsyncGPUReadback.Request(snapshot, 0, readbackFormat, request => {
                    Texture2D export = null;
                    try {
                        if (this == null) return;
                        if (request.hasError) {
                            status = "照片回读失败";
                            Debug.LogWarning("[RuntimePrefabPhoto] async_readback_failed");
                            return;
                        }
                        export = new Texture2D(width, height, TextureFormat.RGBA32, false);
                        export.LoadRawTextureData(request.GetData<byte>());
                        string folder = Path.Combine(Application.persistentDataPath, "runtime-photos");
                        Directory.CreateDirectory(folder);
                        string file = Path.Combine(folder, $"photo-{DateTime.UtcNow:yyyyMMdd-HHmmss-fffffff}.png");
                        File.WriteAllBytes(file, export.EncodeToPNG());
                        status = $"照片已保存 · {width}×{height} · 输入 #{inputId}";
                        Debug.Log($"[RuntimePrefabPhoto] image_size={width}x{height} input_frame={inputId} " +
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
