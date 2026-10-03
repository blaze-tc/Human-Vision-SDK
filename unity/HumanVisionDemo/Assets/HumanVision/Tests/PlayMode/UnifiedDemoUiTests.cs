using System.Collections;
using System.IO;
using System.Linq;
using HumanVision.Demo;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.SceneManagement;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace HumanVision.Tests
{
    public sealed class UnifiedDemoUiTests
    {
        private bool previousIgnore;
        [SetUp] public void Before() { previousIgnore = LogAssert.ignoreFailingMessages; LogAssert.ignoreFailingMessages = true; }
        [TearDown] public void After() { LogAssert.ignoreFailingMessages = previousIgnore; }
        private static void Click(Button button) => ExecuteEvents.Execute(button.gameObject,
            new PointerEventData(EventSystem.current) { button = PointerEventData.InputButton.Left }, ExecuteEvents.pointerClickHandler);
        [UnityTest] public IEnumerator ActualVideoDemoShowsPublicSkeletonAndIndependentPreview()
        {
            SceneManager.LoadScene("HumanVisionVideoDemo"); yield return null; yield return null;
            var demo = Object.FindObjectOfType<HumanVisionDemoNavigator>(); Assert.NotNull(demo);
            var facade = demo.GetComponent<HumanVision.HumanVisionCameraManager>(); int actualFacadeEvents = 0;
            facade.SkeletonUpdated += sequence => { if (demo.Manager.BodyCount > 0 && sequence == demo.Manager.ResultSequence) actualFacadeEvents++; };
            Assert.NotNull(demo.Overlay.GetComponent<CanvasRenderer>(), "Actual existing skeleton Graphic needs its CanvasRenderer before source/bridge events.");
            demo.ModePanel.GetComponentsInChildren<InputField>().First(field => field.name == "Video path").text = "E:/Project/Human Vision SDK/video-1.mp4";
            Click(demo.GetComponentsInChildren<Button>(true).First(button => button.name == "Start / reconnect source"));
            float deadline = Time.realtimeSinceStartup + 30;
            while (!demo.Manager.IsInitialized && Time.realtimeSinceStartup < deadline) yield return null;
            deadline = Time.realtimeSinceStartup + 5;
            while (demo.Manager.Stats.SubmittedFrames == 0 && Time.realtimeSinceStartup < deadline) yield return null;
            var decoder = Resources.FindObjectsOfTypeAll<UnityEngine.Video.VideoPlayer>().First(player => player.url == "E:/Project/Human Vision SDK/video-1.mp4");
            deadline = Time.realtimeSinceStartup + 10;
            while (!decoder.isPrepared && Time.realtimeSinceStartup < deadline) yield return null;
            Assert.That(decoder.isPrepared, Is.True);
            long sequenceBeforeSeek = demo.Manager.ResultSequence;
            decoder.time = 37;
            deadline = Time.realtimeSinceStartup + 10;
            while ((!demo.Input.Source.TryGetLatestFrame(-1, out var soughtFrame) || soughtFrame.PresentationTimestampUs < 37000000) && Time.realtimeSinceStartup < deadline) yield return null;
            Assert.That(demo.Input.Source.TryGetLatestFrame(-1, out var dancingFrame) && dancingFrame.PresentationTimestampUs >= 37000000, Is.True, "Actual decoded dancing video frame is required.");
            deadline = Time.realtimeSinceStartup + 30;
            while ((demo.Manager.ResultSequence <= sequenceBeforeSeek + 4 || demo.Manager.BodyCount == 0 || demo.Manager.SampledBodyCount == 0 || demo.Manager.Bodies[0].Joints.Count(joint => joint.Valid) <= 10 || !demo.Bridge.CanPresentResult(demo.Manager.SourceFrameId)) && Time.realtimeSinceStartup < deadline) yield return null;
            Directory.CreateDirectory("Screenshots");
            var adapter = demo.GetComponent<HumanVisionInputAdapter>();
            string boundaries = "sourceCurrent=" + demo.Input.Source.TryGetLatestFrame(-1, out var sourceFrame) + "; sourceFrame=" + sourceFrame.FrameId + "; sourceId=" + sourceFrame.SourceId + "; generation=" + sourceFrame.Generation + "; dimensions=" + sourceFrame.Width + "x" + sourceFrame.Height + "; origin=" + sourceFrame.RowOrigin + "; published=" + sourceFrame.PublishedTimestampUs + "; adapterEnabled=" + adapter.isActiveAndEnabled + "; adapterPreview=" + adapter.LatestPreviewFrameId + "; adapterSourceId=" + adapter.SourceId + "; adapterGeneration=" + adapter.SourceGeneration + "; pendingCopies=" + adapter.PendingSourceCopies + "; retirement=" + adapter.RetirementPending + "; readbacks=" + demo.Bridge.FullFrameReadbackRequests + "; readbackErrors=" + demo.Bridge.ReadbackErrors + "; supportsAsync=" + SystemInfo.supportsAsyncGPUReadback;
            bool acquired = demo.Input.Source.TryAcquireSourceCopyLease(in sourceFrame, out var diagnosticLease); if (acquired) diagnosticLease.Dispose();
            boundaries += "; diagnosticLeaseAcquired=" + acquired + "; adapterManagerSame=" + ReferenceEquals(adapter.manager, demo.Manager) + "; adapterGpu=" + adapter.manager.UsesAndroidGpuFrames;
            foreach (string field in new[] { "cpuBridge", "source", "latest", "closing" }) boundaries += "; adapter." + field + "=" + typeof(HumanVisionInputAdapter).GetField(field, System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).GetValue(adapter);
            foreach (string field in new[] { "_rowOrderReady", "_rowProbePending", "_acceptReadbacks", "_externalInput", "_nextLiveSubmitTime", "_renderTexture" }) boundaries += "; " + field + "=" + typeof(VideoPlayerFrameSource).GetField(field, System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).GetValue(demo.Bridge);
            File.WriteAllText("Screenshots/sdk-runtime-diagnostic.txt", boundaries + "; status=" + demo.Status + "; source=" + demo.Input.Source.State + "; bodies=" + demo.Manager.BodyCount + "; submitted=" + demo.Manager.Stats.SubmittedFrames + "; processed=" + demo.Manager.Stats.ProcessedFrames + "; resultSequence=" + demo.Manager.ResultSequence + "; frame=" + demo.Manager.SourceFrameId + "; adapterSubmitted=" + adapter.LatestSubmittedFrameId + "; adapterError=" + adapter.LastError + "; bridgeError=" + demo.Bridge.LastError + "; managerError=" + demo.Manager.LastError + "; diagnostics=" + demo.Manager.RuntimeDiagnostics);
            Assert.That(demo.Input.Source.State, Is.EqualTo(InputSourceState.Streaming)); Assert.NotNull(demo.Input.Preview.texture);
            Assert.That(demo.Manager.IsInitialized, Is.True, demo.Status + " / " + demo.Manager.LastError);
            Assert.That(demo.Manager.Stats.SubmittedFrames, Is.GreaterThan(0), boundaries);
            Assert.That(demo.Manager.BodyCount, Is.GreaterThan(0), demo.Status + " / " + demo.Manager.LastError);
            Assert.That(demo.Bridge.CanPresentResult(demo.Manager.SourceFrameId), Is.True);
            Assert.That(demo.Manager.SampledBodyCount, Is.GreaterThan(0), demo.Manager.RuntimeDiagnostics);
            Assert.That(demo.Bridge.ResultAgeMilliseconds, Is.InRange(0d, 3000d));
            File.WriteAllText("Screenshots/sdk-facade-api.txt", "raw="+demo.Manager.BodyCount+"; sampled="+demo.Manager.SampledBodyCount+"; nativeCapacity="+demo.Manager.MaxBodies+"; facadeReady="+facade.IsReady+"; facadeRegions="+facade.GetRegionCount()+"; facadeUsers="+facade.GetUsersCount()+"; facadeEvents="+actualFacadeEvents+"; facadeTexture="+(facade.GetColorImageTex()!=null)+"; facadeProfile="+facade.ActiveRuntimeProfile+"; resultFrame="+demo.Manager.SourceFrameId+"; sourceFrame="+sourceFrame.FrameId);
            Assert.That(facade.GetRegionCount(), Is.EqualTo(demo.Manager.MaxBodies));
            Assert.That(facade.GetUsersCount(), Is.GreaterThan(0)); Assert.That(actualFacadeEvents, Is.GreaterThan(0));
            bool actualJoint=false, actualSample=false;
            for(int region=0;region<facade.GetRegionCount();region++) {
                actualSample |= facade.TryGetSampledBodyByRegionIndex(region,out var sampledBody);
                for(int joint=0;joint<17;joint++) actualJoint |= facade.TryGetJointByRegionIndex(region,(HumanVision.HumanVisionJointType)joint,out var position);
            }
            Assert.That(actualJoint, Is.True); Assert.That(actualSample, Is.True);
            Assert.That(facade.GetColorImageWidth(),Is.EqualTo(1024));Assert.That(facade.GetColorImageHeight(),Is.EqualTo(576));
            Assert.NotNull(demo.Overlay); Canvas.ForceUpdateCanvases();
            using (var vertices = new VertexHelper()) { typeof(HumanVisionOverlay).GetMethod("OnPopulateMesh", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.DeclaredOnly, null, new[] { typeof(VertexHelper) }, null).Invoke(demo.Overlay, new object[] { vertices }); Assert.That(vertices.currentVertCount, Is.GreaterThan(0)); }
            Assert.That(demo.Manager.Bodies[0].Joints.Count(joint => joint.Valid), Is.GreaterThan(10));
            Click(demo.GetComponentsInChildren<Button>(true).First(button => button.name == "Settings: show / hide")); yield return null;
            Assert.That(demo.Input.Status.gameObject.activeInHierarchy, Is.True); Assert.NotNull(demo.Input.Preview.texture);
            Directory.CreateDirectory("Screenshots");
            File.WriteAllText("Screenshots/sdk-video.txt", "screen=" + Screen.width + "x" + Screen.height + "; source=" + demo.Bridge.SourceWidth + "x" + demo.Bridge.SourceHeight + "; bodies=" + demo.Manager.BodyCount + "; resultFrame=" + demo.Manager.SourceFrameId + "; previewFrame=" + demo.Bridge.PresentationFrameId + "; sampledBodies=" + demo.Manager.SampledBodyCount + "; resultAgeMs=" + demo.Bridge.ResultAgeMilliseconds + "; sourcePts=" + dancingFrame.PresentationTimestampUs + "; profile=" + demo.Manager.ActiveRuntimeProfile);
            string screenshot = "Screenshots/sdk-video-" + System.Guid.NewGuid().ToString("N") + ".png";
            if (!Application.isBatchMode) {
                ScreenCapture.CaptureScreenshot(screenshot); deadline = Time.realtimeSinceStartup + 10;
                while (!File.Exists(screenshot) && Time.realtimeSinceStartup < deadline) yield return null;
                Assert.That(File.Exists(screenshot), Is.True);
                File.WriteAllText(screenshot + ".txt", File.ReadAllText("Screenshots/sdk-video.txt") + "; completion result=" + demo.Manager.SourceFrameId + "; completion sequence=" + demo.Manager.ResultSequence + "; canvasRenderer=" + demo.Overlay.GetComponent<CanvasRenderer>().GetInstanceID() + "; overlay=" + demo.Overlay.GetInstanceID());
            } else File.WriteAllText("Screenshots/sdk-batch-visual-unavailable.txt", "Functional assertions only: batch GameView screenshot is unavailable. Actual user GameView visual gate remains separate.");
            Click(demo.GetComponentsInChildren<Button>(true).First(button => button.name == "Settings: show / hide"));
            Assert.That(demo.Input.Status.gameObject.activeInHierarchy, Is.True); Assert.NotNull(demo.Input.Preview.texture);
            demo.Bridge.DetachUnifiedSource(); yield return null; Assert.NotNull(demo.Input.Preview.texture);
            Assert.That(facade.GetUsersCount(),Is.Zero); Assert.That(facade.TryGetBodyByRegionIndex(0,out var detachedBody),Is.False);
        }
        [UnityTest] public IEnumerator SharedRegionsAndModeParametersSurviveActualSceneNavigation()
        {
            SceneManager.LoadScene("HumanVisionVideoDemo"); yield return null; yield return null;
            var demo = Object.FindObjectOfType<HumanVisionDemoNavigator>();
            demo.SharedPanel.GetComponentsInChildren<InputField>().First().text = "2";
            demo.Shared.ResizeRegions(2); demo.Shared.Regions[0] = new Rect(.02f, .04f, .35f, .8f);
            Assert.NotNull(demo.Overlay.GetComponent<CanvasRenderer>(), "Actual existing skeleton Graphic needs its CanvasRenderer before source/bridge events.");
            demo.ModePanel.GetComponentsInChildren<InputField>().First(field => field.name == "Skeleton line width (px)").text = "9";
            Assert.NotNull(demo.Overlay.GetComponent<CanvasRenderer>(), "Actual existing skeleton Graphic needs its CanvasRenderer before source/bridge events.");
            demo.ModePanel.GetComponentsInChildren<InputField>().First(field => field.name == "Joint diameter (px)").text = "27";
            Click(demo.GetComponentsInChildren<Button>(true).First(button => button.name == "WebCamera")); yield return null; yield return null;
            demo = Object.FindObjectOfType<HumanVisionDemoNavigator>(); Assert.That(demo.Kind, Is.EqualTo(InputKind.WebCamera));
            Assert.That(demo.Shared.MaxBodies, Is.EqualTo(2)); Assert.That(demo.Shared.Regions[0], Is.EqualTo(new Rect(.02f, .04f, .35f, .8f)));
            Click(demo.GetComponentsInChildren<Button>(true).First(button => button.name == "Rtsp")); yield return null; yield return null;
            demo = Object.FindObjectOfType<HumanVisionDemoNavigator>(); Assert.That(demo.Kind, Is.EqualTo(InputKind.Rtsp)); Assert.That(demo.Shared.MaxBodies, Is.EqualTo(2));
            Click(demo.GetComponentsInChildren<Button>(true).First(button => button.name == "Video")); yield return null; yield return null;
            demo = Object.FindObjectOfType<HumanVisionDemoNavigator>(); Assert.That(demo.Mode.LineWidth, Is.EqualTo(9)); Assert.That(demo.Mode.PointDiameter, Is.EqualTo(27));
            Assert.That(demo.Shared.Regions[0], Is.EqualTo(new Rect(.02f, .04f, .35f, .8f)));
        }
    }
}
