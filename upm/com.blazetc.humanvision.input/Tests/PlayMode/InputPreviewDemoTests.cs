using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.TestTools;
using UnityEngine.UI;

namespace HumanVision.Input.Tests
{
    public sealed class InputPreviewDemoTests
    {
        private GameObject root;
        private bool previousIgnoreLogs;
        [SetUp] public void Before() { previousIgnoreLogs = LogAssert.ignoreFailingMessages; LogAssert.ignoreFailingMessages = true; }
        [TearDown] public void After() { if (root != null) UnityEngine.Object.DestroyImmediate(root); LogAssert.ignoreFailingMessages = previousIgnoreLogs; }
        private IEnumerator Create()
        {
            var type = AppDomain.CurrentDomain.GetAssemblies().Select(assembly => assembly.GetType("InputPreviewController")).FirstOrDefault(value => value != null);
            Assert.NotNull(type, "The actual standalone sample must be imported.");
            root = new GameObject("Actual imported preview sample"); root.AddComponent(type);
            yield return null; yield return null;
        }
        private Button Find(string name) => root.GetComponentsInChildren<Button>(true).First(button => button.name == name);
        private static void Click(Button button)
        {
            Assert.That(button.IsInteractable(), Is.True);
            ExecuteEvents.Execute(button.gameObject, new PointerEventData(EventSystem.current) { button = PointerEventData.InputButton.Left }, ExecuteEvents.pointerClickHandler);
        }
        [UnityTest] public IEnumerator StandaloneSamplePlaysRealVideoWithoutInference()
        {
            yield return Create();
            var input = root.GetComponent<InputPreviewControls>();
            root.GetComponentsInChildren<InputField>(true).First(field => field.name == "Video path / RTSP URL / camera device").text = "E:/Project/Human Vision SDK/video-1.mp4";
            Click(Find("Video")); Click(Find("Start / reconnect"));
            float deadline = Time.realtimeSinceStartup + 20;
            while (input.Preview.texture == null && Time.realtimeSinceStartup < deadline) yield return null;
            Assert.That(input.Source.State, Is.EqualTo(InputSourceState.Streaming));
            Assert.NotNull(input.Preview.texture);
            Assert.That(input.Source.TryGetLatestFrame(-1, out var frame), Is.True);
            Assert.That(frame.FrameId, Is.GreaterThanOrEqualTo(0)); Assert.That(frame.Width, Is.GreaterThan(0));
            long firstFrame = frame.FrameId; deadline = Time.realtimeSinceStartup + 10;
            while (!input.Source.TryGetLatestFrame(firstFrame, out frame) && Time.realtimeSinceStartup < deadline) yield return null;
            Assert.That(frame.FrameId, Is.GreaterThan(firstFrame), "Real playback must advance source frames.");
            Assert.That(root.GetComponents<Component>().Any(component => component.GetType().FullName == "HumanVision.HumanVisionManager"), Is.False);
            Directory.CreateDirectory("Screenshots");
            File.WriteAllText("Screenshots/input-preview-frame.txt", "actual screen=" + Screen.width + "x" + Screen.height + "; source=" + frame.Width + "x" + frame.Height + "; frame=" + frame.FrameId);
            string screenshot = "Screenshots/input-preview-" + Guid.NewGuid().ToString("N") + ".png";
            ScreenCapture.CaptureScreenshot(screenshot);
            deadline = Time.realtimeSinceStartup + 10;
            while (!File.Exists(screenshot) && Time.realtimeSinceStartup < deadline) yield return null;
            Assert.That(File.Exists(screenshot), Is.True, "Actual rendered PNG must be saved.");
            File.WriteAllText(screenshot + ".txt", "screen=" + Screen.width + "x" + Screen.height + "; source=" + frame.Width + "x" + frame.Height + "; frame=" + frame.FrameId + "; status=" + input.Status.text + "; texture=" + input.Preview.texture.name);
            Click(Find("Settings: show / hide")); yield return null;
            Assert.NotNull(input.Preview.texture); Assert.That(input.Status.gameObject.activeInHierarchy, Is.True);
            input.Close(); Assert.That(input.Preview.texture, Is.Null);
        }
        [UnityTest] public IEnumerator FailureKeepsNavigationStatusAndScrollingReachable()
        {
            yield return Create();
            Click(Find("Rtsp")); Click(Find("Start / reconnect")); yield return new WaitForSecondsRealtime(.4f);
            var input = root.GetComponent<InputPreviewControls>();
            Assert.That(input.Status.text, Does.Contain("Error").Or.Contain("Stopped"));
            Assert.That(string.IsNullOrWhiteSpace(input.Status.text), Is.False);
            var scaler = root.GetComponentInChildren<CanvasScaler>(); Assert.That(scaler.referenceResolution, Is.EqualTo(new Vector2(1280, 720))); Assert.That(scaler.matchWidthOrHeight, Is.EqualTo(.5f));
            var scroll = root.GetComponentInChildren<ScrollRect>(); Canvas.ForceUpdateCanvases();
            Assert.That(scroll.vertical, Is.True); Assert.That(scroll.viewport.rect.height, Is.GreaterThan(0));
            scroll.verticalNormalizedPosition = 0; Canvas.ForceUpdateCanvases();
            Assert.That(Find("Stop").gameObject.activeInHierarchy, Is.True);
            var nav = Find("Video"); var center = RectTransformUtility.WorldToScreenPoint(null, ((RectTransform)nav.transform).TransformPoint(((RectTransform)nav.transform).rect.center));
            var hits = new List<RaycastResult>(); EventSystem.current.RaycastAll(new PointerEventData(EventSystem.current) { position = center }, hits);
            Assert.That(hits.Any(hit => hit.gameObject == nav.gameObject), Is.True, "Actual navigation center must be raycast reachable.");
            Click(Find("Settings: show / hide")); Assert.That(input.Status.gameObject.activeInHierarchy, Is.True);
        }
    }
}
