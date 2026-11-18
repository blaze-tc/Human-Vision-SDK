using System.Collections;
using System.IO;
using System.Reflection;
using HumanVision.Demo;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class HumanVisionRuntimeViewTests
    {
        [UnityTest]
        public IEnumerator PackagePrefabIsExplicitlyStartedAndSurvivesSceneUnload()
        {
            var prefab = Resources.Load<GameObject>("HumanVisionRuntime");
            Assert.NotNull(prefab, "The package must contain its runtime prefab.");
            var previousScene = SceneManager.GetActiveScene();
            var scene = SceneManager.CreateScene("Runtime view bootstrap");
            Assert.True(SceneManager.SetActiveScene(scene));
            var view = HumanVisionRuntimeView.Create();
            var owner = view.gameObject;
            try {
                var sdk = owner.GetComponent<HumanVisionSdk>();
                Assert.NotNull(sdk);
                Assert.False(sdk.InitializeOnStart);
                Assert.False(sdk.IsInitialized);
                Assert.That(view.PresentationMode, Is.EqualTo(HumanVisionRuntimePresentationMode.MatchedOverlay));
                Assert.AreNotEqual(scene, owner.scene);
                var cameras = owner.GetComponentsInChildren<Camera>(true);
                Assert.That(cameras.Length, Is.EqualTo(1));
                Assert.False(cameras[0].enabled);
                Assert.That(cameras[0].cullingMask, Is.Zero);
                var duplicate = Object.Instantiate(prefab);
                Assert.AreSame(view, HumanVisionRuntimeView.Create());
                Assert.True(SceneManager.SetActiveScene(previousScene));
                yield return SceneManager.UnloadSceneAsync(scene);
                Assert.True(owner != null);
                Assert.True(duplicate == null);
            } finally {
                if (owner != null) Object.Destroy(owner);
                if (scene.isLoaded) SceneManager.UnloadSceneAsync(scene);
            }
        }

        [UnityTest]
        public IEnumerator RtspProducesMatchedImageJointAndOffscreenPixelsAndCanRestart()
        {
            string url = System.Environment.GetEnvironmentVariable("HV_RUNTIME_VIEW_RTSP");
            if (string.IsNullOrEmpty(url)) Assert.Ignore("Set HV_RUNTIME_VIEW_RTSP to an authorized local stream with a visible person.");
            var view = HumanVisionRuntimeView.Create();
            var sdk = view.Sdk;
            int pairs = 0, accepted = 0;
            long previousInputFrame = -1;
            bool pixelVerified = false;
            var probe = new Texture2D(1, 1, TextureFormat.RGBA32, false);
            try {
                Assert.True(view.SupportsMatchedFrames, "Run this test with graphics fences enabled.");
                var options = sdk.Configuration;
                options.SourceKind = InputKind.Rtsp;
                options.Input.RtspUrl = url;
                options.UseWindowsCpu = true;
                options.Recognition.MaxBodies = 4;
                options.Recognition.UseRegions = false;
                options.Recognition.MaximumResultAgeMilliseconds = 1500;
                options.RuntimeRoot = System.Environment.GetEnvironmentVariable("HV_RUNTIME_VIEW_DATA") ?? "";
                Assert.True(string.IsNullOrEmpty(options.RuntimeRoot) || Directory.Exists(options.RuntimeRoot));
                sdk.ResultUpdated += _ => accepted++;
                System.Action<HumanVisionPresentedFrame> checkOverlay = pair => {
                    Assert.True(view.TryGetFrame(out var fromQuery));
                    Assert.That(fromQuery.SourceFrameId, Is.EqualTo(sdk.SourceFrameId));
                    Assert.That(fromQuery.ResultSequence, Is.EqualTo(sdk.ResultSequence));
                    Assert.That(fromQuery.InputFrameId, Is.GreaterThan(previousInputFrame));
                    Assert.AreSame(pair.OverlayTexture, view.OffscreenCamera.targetTexture);
                    Assert.False(view.OffscreenCamera.enabled);
                    Assert.AreNotSame(view.GetLiveTexture(), pair.ColorTexture);
                    Assert.That(pair.OverlayTexture.width, Is.EqualTo(pair.ColorTexture.width));
                    previousInputFrame = pair.InputFrameId;
                    pairs++;
                    for (int slot = 0; !pixelVerified && slot < sdk.GetMaxBodies(); slot++) {
                        var id = HumanVisionCanonicalJointId.Nose;
                        if (!view.TryGetJointTransform(slot, id, out var joint)) continue;
                        Assert.True(view.TryGetJointPosition(slot, id, out var position));
                        Assert.AreEqual(joint.position, position);
                        Assert.True(view.TryGetBodyId(slot, out var track));
                        Assert.That(track, Is.EqualTo(sdk.GetUserIdByIndex(slot)));
                        Assert.True(sdk.TryGetJointNormalizedPosition(slot, id, out var normalized));
                        var viewport = view.OffscreenCamera.WorldToViewportPoint(position);
                        Assert.That(viewport.x, Is.EqualTo(normalized.x).Within(.001f));
                        Assert.That(viewport.y, Is.EqualTo(1 - normalized.y).Within(.001f));
                        if (viewport.x <= .02f || viewport.y <= .02f || viewport.x >= .98f || viewport.y >= .98f) continue;
                        int x = (int)(viewport.x * pair.OverlayTexture.width);
                        int y = (int)(viewport.y * pair.OverlayTexture.height);
                        var pixel = Pixel(pair.OverlayTexture, probe, x, y);
                        Assert.That(pixel.g - Mathf.Max(pixel.r, pixel.b), Is.GreaterThan(.2f),
                            $"Expected green at ({x},{y}); pixel={pixel}.");
                        Assert.That(ColorDistance(Pixel((RenderTexture)pair.ColorTexture, probe, 1, 1),
                            Pixel(pair.OverlayTexture, probe, 1, 1)), Is.LessThan(.08f), "Background color should be preserved.");
                        pixelVerified = true;
                        string screenshot = System.Environment.GetEnvironmentVariable("HV_RUNTIME_VIEW_SCREENSHOT");
                        if (!string.IsNullOrEmpty(screenshot)) SaveOverlay(pair.OverlayTexture, screenshot);
                    }
                };
                view.FramePresented += checkOverlay;
                yield return sdk.Initialize(options);
                Assert.True(sdk.IsRunning, sdk.LastError);
                Assert.That(sdk.RuntimeProfile, Is.EqualTo("windows-yolo-cpu-medium"));
                double deadline = Time.realtimeSinceStartupAsDouble + 35;
                while ((!pixelVerified || pairs < 25) && Time.realtimeSinceStartupAsDouble < deadline) yield return null;
                Assert.True(pixelVerified, $"No drawn person was validated ({pairs} pairs, {sdk.FreshResultFps:F1} result FPS, {sdk.LastError}).");
                Assert.GreaterOrEqual(pairs, 25);
                Assert.True(view.TryGetFrame(out var beforeRestart));
                Debug.Log($"HV_RUNTIME_VIEW_RTSP verified {pairs}/{accepted} matched/accepted results; resolution={beforeRestart.ColorTexture.width}x{beforeRestart.ColorTexture.height}, last input={beforeRestart.InputFrameId}, native={beforeRestart.SourceFrameId}.");
                yield return VerifySettingsOverlay(view);
                yield return VerifyOverlayRetention(view, probe);
                view.FramePresented -= checkOverlay;
                yield return VerifyPresentationModes(view);
                view.PresentationMode = HumanVisionRuntimePresentationMode.MatchedOverlay;
                view.FramePresented += checkOverlay;

                yield return sdk.StopSdk();
                Assert.False(view.TryGetFrame(out _));
                Assert.False(view.TryGetDisplayedFrame(out _));
                Assert.False(view.TryGetJointTransform(0, HumanVisionCanonicalJointId.Nose, out _));
                Assert.IsNull(view.GetLiveTexture());
                pairs = 0;
                previousInputFrame = -1;
                yield return sdk.RestartSdk();
                Assert.True(sdk.IsRunning, sdk.LastError);
                deadline = Time.realtimeSinceStartupAsDouble + 35;
                while (pairs == 0 && Time.realtimeSinceStartupAsDouble < deadline) yield return null;
                Assert.True(view.TryGetFrame(out var afterRestart));
                Assert.That(afterRestart.SourceId, Is.Not.EqualTo(beforeRestart.SourceId));
                yield return view.Unload();
                Assert.True(view == null);
                Assert.True(HumanVisionRuntimeView.Instance == null);
            } finally {
                Object.Destroy(probe);
                if (view != null) Object.Destroy(view.gameObject);
            }
        }

        private static IEnumerator VerifyPresentationModes(HumanVisionRuntimeView view)
        {
            var sdk = view.Sdk;
            Assert.True(sdk.TryGetInputFrame(-1, out var initial));
            long sequence = sdk.ResultSequence;
            const BindingFlags members = BindingFlags.Instance | BindingFlags.NonPublic;
            var bridge = typeof(HumanVisionSdk).GetProperty("FrameBridge", members).GetValue(sdk);
            var exactHistory = bridge.GetType().GetField("_runtimeViewHistory", members);
            var delayedHistory = bridge.GetType().GetField("_displayHistory", members);

            view.PresentationMode = HumanVisionRuntimePresentationMode.Disabled;
            Assert.IsNull(view.OffscreenCamera.targetTexture);
            Assert.False(view.TryGetFrame(out _));
            double deadline = Time.realtimeSinceStartupAsDouble + 8;
            while (sdk.ResultSequence <= sequence && Time.realtimeSinceStartupAsDouble < deadline) yield return null;
            Assert.That(sdk.ResultSequence, Is.GreaterThan(sequence), "Live-only mode must keep inference running.");
            Assert.True(sdk.TryGetInputFrame(initial.FrameId, out var live));
            Assert.AreSame(live.Texture, view.GetLiveTexture());
            Assert.IsNull(exactHistory.GetValue(bridge), "Live-only mode must not retain exact snapshots.");
            Assert.IsNull(delayedHistory.GetValue(bridge), "Live-only mode must not switch to delayed presentation.");
            Assert.IsNull(view.OffscreenCamera.targetTexture);
            Assert.False(view.TryGetFrame(out _));
            Assert.False(view.TryGetDisplayedFrame(out _));

            view.PresentationMode = HumanVisionRuntimePresentationMode.MatchedColor;
            deadline = Time.realtimeSinceStartupAsDouble + 12;
            HumanVisionPresentedFrame color = default;
            while ((!view.TryGetFrame(out color) || color.BodyCount == 0) && Time.realtimeSinceStartupAsDouble < deadline)
                yield return null;
            Assert.That(color.BodyCount, Is.GreaterThan(0), "Color-only mode must restore exact camera pairs with a detected person.");
            Assert.AreEqual(sdk.SourceFrameId, color.SourceFrameId);
            Assert.AreEqual(sdk.ResultSequence, color.ResultSequence);
            Assert.AreEqual(live.Width, color.ColorTexture.width);
            Assert.AreEqual(live.Height, color.ColorTexture.height);
            Assert.IsNull(color.OverlayTexture);
            Assert.IsNull(view.OffscreenCamera.targetTexture);
            Assert.False(view.TryGetDisplayedFrame(out _));
            for (int slot = 0; slot < sdk.GetMaxBodies(); ++slot) {
                if (!view.TryGetJointPosition(slot, HumanVisionCanonicalJointId.Nose, out var joint)) continue;
                Assert.True(sdk.TryGetJointNormalizedPosition(slot, HumanVisionCanonicalJointId.Nose, out var normalized));
                var viewport = view.OffscreenCamera.WorldToViewportPoint(joint);
                Assert.That(viewport.x, Is.EqualTo(normalized.x).Within(.001f));
                Assert.That(viewport.y, Is.EqualTo(1 - normalized.y).Within(.001f));
                yield break;
            }
            Assert.Fail("Color-only mode must expose matched joint geometry.");
        }

        private static IEnumerator VerifyOverlayRetention(HumanVisionRuntimeView view, Texture2D probe)
        {
            var sdk = view.Sdk;
            double deadline = Time.realtimeSinceStartupAsDouble + 5;
            while (!view.TryGetFrame(out _) && Time.realtimeSinceStartupAsDouble < deadline) yield return null;
            Assert.True(view.TryGetFrame(out var original), "A real paired result is required before forcing a preview gap.");
            Assert.False(view.RetainLastOverlayDuringPairGaps);

            const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic;
            var bridge = typeof(HumanVisionSdk).GetProperty("FrameBridge", flags).GetValue(sdk);
            var resetHistory = typeof(VideoPlayerFrameSource).GetMethod("ResetRuntimeViewHistory", flags);
            var updateView = typeof(HumanVisionRuntimeView).GetMethod("LateUpdate", flags);
            var before = Pixel(original.OverlayTexture, probe, 1, 1);
            Assert.Greater(before.a, .5f, "The current composition must contain the camera image.");

            view.RetainLastOverlayDuringPairGaps = true;
            try {
                // Drop the image history for one tick while the live input and accepted result remain available.
                resetHistory.Invoke(bridge, null);
                updateView.Invoke(view, null);
                Assert.True(sdk.HasFreshResult);
                Assert.False(view.TryGetFrame(out _));
                Assert.False(view.TryGetJointTransform(0, HumanVisionCanonicalJointId.Nose, out _));
                Assert.True(view.TryGetDisplayedFrame(out var held));
                Assert.AreSame(original.OverlayTexture, held.OverlayTexture);
                Assert.AreEqual(original.InputFrameId, held.InputFrameId);
                Assert.AreEqual(original.ResultSequence, held.ResultSequence);
                Assert.IsNull(held.ColorTexture);
                var duringGap = Pixel(held.OverlayTexture, probe, 1, 1);
                Assert.That(ColorDistance(before, duringGap), Is.LessThan(.01f));
                Assert.That(duringGap.a, Is.EqualTo(before.a).Within(.01f));

                var presentedAt = typeof(HumanVisionRuntimeView).GetField("lastComposedAt", flags);
                Assert.NotNull(presentedAt);
                presentedAt.SetValue(view, Time.realtimeSinceStartupAsDouble - 1d);
                Assert.True(view.DisplayedFrameExpired);
                Assert.False(view.TryGetDisplayedFrame(out _), "Expired composition must leave the display.");

                deadline = Time.realtimeSinceStartupAsDouble + 5;
                while (!view.TryGetFrame(out _) && Time.realtimeSinceStartupAsDouble < deadline) yield return null;
                Assert.True(view.TryGetFrame(out var next), "The live input must resume exact pairing.");
                Assert.Greater(next.ResultSequence, held.ResultSequence);
                Assert.True(view.TryGetDisplayedFrame(out var displayed));
                Assert.AreEqual(next.ResultSequence, displayed.ResultSequence);
                Assert.False(view.DisplayedFrameExpired);
            } finally {
                view.RetainLastOverlayDuringPairGaps = false;
            }
        }

        private static IEnumerator VerifySettingsOverlay(HumanVisionRuntimeView view)
        {
            var sdk = view.Sdk;
            var canvas = new GameObject("Settings preview test canvas", typeof(Canvas));
            var previewObject = new GameObject("Settings video", typeof(RawImage), typeof(AspectRatioFitter));
            previewObject.transform.SetParent(canvas.transform, false);
            var preview = previewObject.GetComponent<RawImage>();
            var fitter = previewObject.GetComponent<AspectRatioFitter>();
            var overlayObject = new GameObject("Settings skeleton overlay", typeof(RectTransform), typeof(CanvasRenderer), typeof(HumanVisionOverlay));
            overlayObject.transform.SetParent(previewObject.transform, false);
            var overlay = overlayObject.GetComponent<HumanVisionOverlay>();

            System.Action<HumanVisionPresentedFrame> observePair = null;
            VideoPlayerFrameSource bridge = null;
            try {
                // Borrow the already running SDK using the same bindings as Settings.
                var bridgeProperty = typeof(HumanVisionSdk).GetProperty("FrameBridge", BindingFlags.Instance | BindingFlags.NonPublic);
                Assert.NotNull(bridgeProperty);
                bridge = (VideoPlayerFrameSource)bridgeProperty.GetValue(sdk);
                Assert.NotNull(bridge);
                var manager = bridge.GetComponent<HumanVisionManager>();
                Assert.NotNull(manager);
                bridge.Configure(manager, preview, fitter);
                overlay.Configure(manager, bridge);
                overlay.ConfigureStyle(9, 27);

                var populateOverlay = typeof(HumanVisionOverlay).GetMethod("OnPopulateMesh",
                    BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly);
                Assert.NotNull(populateOverlay);
                int observedPairs = 0;
                int allPairs = 0;
                string mismatch = null;
                observePair = pair => {
                    allPairs++;
                    if (pair.BodyCount == 0 || observedPairs >= 3 || mismatch != null) return;
                    if (!overlay.enabled) {
                        if (preview.texture != view.GetLiveTexture())
                            mismatch = "The hidden skeleton overlay did not restore the live image.";
                    } else if (preview.texture != pair.ColorTexture) {
                        mismatch = "The enabled Settings skeleton overlay must display the same source texture as its pose.";
                    } else if (manager.SourceFrameId != pair.SourceFrameId || manager.ResultSequence != pair.ResultSequence) {
                        mismatch = "The Settings overlay's current body data must match the presented result identity.";
                    } else if (bridge.PresentationFrameId != pair.InputFrameId ||
                               bridge.PresentationStats.poseBeforeFrame != pair.SourceFrameId) {
                        mismatch = "Settings diagnostics must identify the displayed image and its exact pose.";
                    } else {
                        using (var vertices = new VertexHelper()) {
                            populateOverlay.Invoke(overlay, new object[] { vertices });
                            if (vertices.currentVertCount == 0)
                                mismatch = "A matched visible body must produce Settings overlay geometry.";
                        }
                    }
                    observedPairs++;
                };
                view.FramePresented += observePair;

                foreach (bool showSkeleton in new[] { true, false, true }) {
                    overlay.enabled = showSkeleton;
                    observedPairs = allPairs = 0;
                    double deadline = Time.realtimeSinceStartupAsDouble + 35;
                    while (observedPairs < 3 && mismatch == null && Time.realtimeSinceStartupAsDouble < deadline) {
                        // Settings writes the live image during Update before the bridge selects its final presentation.
                        preview.texture = view.GetLiveTexture();
                        yield return null;
                    }

                    Assert.IsNull(mismatch);
                    Assert.AreEqual(3, observedPairs,
                        $"Expected visible RTSP pairs with skeleton enabled={showSkeleton}. All pairs: {allPairs}, " +
                        $"input {sdk.InputPublicationFps:F1}, results {sdk.FreshResultFps:F1}, bodies {manager.BodyCount}. {sdk.LastError} {bridge.LastError}");
                }
                Debug.Log("HV_RTSP_SETTINGS_OVERLAY verified 6 matched overlays with geometry and 3 live previews with skeleton hidden.");
            } finally {
                view.FramePresented -= observePair;
                overlay.Configure(null, null);
                if (bridge != null) bridge.Configure(bridge.GetComponent<HumanVisionManager>(), null, null);
                Object.Destroy(canvas);
            }
        }

        private static Color Pixel(RenderTexture target, Texture2D probe, int x, int y)
        {
            var previous = RenderTexture.active;
            try {
                RenderTexture.active = target;
                probe.ReadPixels(new Rect(x, y, 1, 1), 0, 0, false);
                return probe.GetPixel(0, 0);
            } finally { RenderTexture.active = previous; }
        }

        private static float ColorDistance(Color a, Color b) =>
            Mathf.Max(Mathf.Abs(a.r - b.r), Mathf.Abs(a.g - b.g), Mathf.Abs(a.b - b.b));

        private static void SaveOverlay(RenderTexture target, string path)
        {
            var previous = RenderTexture.active;
            var copy = new Texture2D(target.width, target.height, TextureFormat.RGBA32, false);
            try {
                RenderTexture.active = target;
                copy.ReadPixels(new Rect(0, 0, target.width, target.height), 0, 0, false);
                File.WriteAllBytes(path, copy.EncodeToPNG());
            } finally {
                RenderTexture.active = previous;
                Object.Destroy(copy);
            }
        }
    }
}
