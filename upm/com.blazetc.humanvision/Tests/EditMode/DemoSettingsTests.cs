using System;
using System.IO;
using HumanVision.Demo;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class DemoSettingsTests
    {
        private string root;
        [SetUp] public void Before() { root = Path.Combine(Path.GetTempPath(), "HumanVisionSettings-" + Guid.NewGuid()); }
        [TearDown] public void After() { if (Directory.Exists(root)) Directory.Delete(root, true); }
        [Test] public void SharedRegionsSurviveThreeSceneSwitches()
        {
            var store = new HumanVisionSettingsStore(root);
            var shared = new SharedRecognitionSettings();
            shared.ResizeRegions(2); shared.UseRegions = true;
            shared.Regions[0] = new Rect(.02f, .05f, .35f, .8f);
            store.SaveShared(shared);
            foreach (InputKind mode in Enum.GetValues(typeof(InputKind))) {
                store.SaveMode(mode, new DemoModeSettings());
                var reloaded = new HumanVisionSettingsStore(root).LoadShared();
                Assert.That(reloaded.MaxBodies, Is.EqualTo(2));
                Assert.That(reloaded.Regions[0], Is.EqualTo(shared.Regions[0]));
                Assert.That(reloaded.UseRegions, Is.True);
            }
        }
        [Test] public void ModeSettingsNeverOverwriteAnotherMode()
        {
            var store = new HumanVisionSettingsStore(root);
            store.SaveMode(InputKind.Video, new DemoModeSettings { VideoPath = "video.mp4", Mirror = true, LineWidth = 9, PointDiameter = 27 });
            store.SaveMode(InputKind.WebCamera, new DemoModeSettings { CameraDevice = "camera", Mirror = false, LineWidth = 12, PointDiameter = 30 });
            store.SaveMode(InputKind.Rtsp, new DemoModeSettings { RtspUrl = "rtsp://localhost/live", Mirror = true, LineWidth = 15, PointDiameter = 33 });
            Assert.That(store.LoadMode(InputKind.Video).VideoPath, Is.EqualTo("video.mp4"));
            Assert.That(store.LoadMode(InputKind.Video).LineWidth, Is.EqualTo(9));
            Assert.That(store.LoadMode(InputKind.WebCamera).Mirror, Is.False);
            Assert.That(store.LoadMode(InputKind.Rtsp).PointDiameter, Is.EqualTo(33));
            Assert.That(store.LoadMode(InputKind.Video).PointDiameter, Is.EqualTo(27));
        }
        [Test] public void AnalysisResolutionMustMatchModelPackContract()
        {
            Directory.CreateDirectory(Path.Combine(root, "profiles"));
            Directory.CreateDirectory(Path.Combine(root, "modelpacks", "precision-t-26-ncnn-fp16"));
            File.WriteAllText(Path.Combine(root, "profiles", "android-ncnn-vulkan.json"), "{\"profile\":\"android-ncnn-vulkan\",\"body\":{\"modelPack\":\"precision-t-26-ncnn-fp16\"},\"detector\":{\"cadence_interval_frames\":4}}");
            File.WriteAllText(Path.Combine(root, "modelpacks", "precision-t-26-ncnn-fp16", "manifest.json"), "{\"pack_id\":\"precision-t-26-ncnn-fp16\",\"models\":[{\"role\":\"detector\",\"input_contract\":{\"width\":320,\"height\":320}},{\"role\":\"body\",\"input_contract\":{\"width\":192,\"height\":256}}]}");
            var contract = AnalysisContract.Load(root, "android-ncnn-vulkan", 8);
            var settings = new SharedRecognitionSettings(); contract.ApplyTo(settings); contract.Validate(settings);
            settings.DetectorWidth = 576;
            Assert.Throws<ArgumentException>(() => contract.Validate(settings));
            settings.DetectorWidth = 320; settings.AnalysisProfileId = "unvalidated";
            Assert.Throws<ArgumentException>(() => contract.Validate(settings));
        }

        [Test] public void CpuClockMappingPreservesPublicationAndReadbackAge() {
            Assert.That(InputTimestampMapping.Map(1500000,2000000,1515320000000),Is.EqualTo(1515319500000));
            Assert.That(InputTimestampMapping.Map(500000,2000000,7000000),Is.EqualTo(5500000));
        }
        [Test] public void ReopenedSourceDoesNotReusePreviousEpochOffset() {
            Assert.That(InputTimestampMapping.Map(100,200,10000),Is.EqualTo(9900));
            Assert.That(InputTimestampMapping.Map(5,50,20000),Is.EqualTo(19955));
        }
        [Test] public void InvalidClockPairsAreRejectedInsteadOfClampedFresh() {
            Assert.Throws<ArgumentException>(()=>InputTimestampMapping.Map(201,200,10000));
            Assert.Throws<ArgumentException>(()=>InputTimestampMapping.Map(-1,200,10000));
            Assert.Throws<ArgumentException>(()=>InputTimestampMapping.Map(0,long.MaxValue,100));
            Assert.Throws<ArgumentException>(()=>InputTimestampMapping.Map(0,0,-1));
        }
        [Test] public void UnknownSourceDomainAndFuturePublicationAreRejected() {
            var frame=new HumanVisionTextureFrame {PublishedTimestampUs=100,SourceTimestampUs=100,TimestampKind=FrameTimestampKind.UnityObserved,SourceClockDomain=FrameClockDomain.Unspecified};
            Assert.Throws<ArgumentException>(()=>InputTimestampMapping.ToUnity(in frame,200,1000));
            frame.SourceClockDomain=FrameClockDomain.InputMonotonic;
            Assert.That(InputTimestampMapping.ToUnity(in frame,200,1000),Is.EqualTo(900));
            frame.SourceTimestampUs=50;Assert.That(InputTimestampMapping.ToUnity(in frame,200,1000),Is.EqualTo(850));frame.SourceTimestampUs=100;
            frame.PublishedTimestampUs=201;Assert.Throws<ArgumentException>(()=>InputTimestampMapping.ToUnity(in frame,200,1000));
            frame.PublishedTimestampUs=100;frame.TimestampKind=FrameTimestampKind.LocalDecode;frame.SourceClockDomain=FrameClockDomain.SourceLocalMonotonic;frame.SourceClockId=8;frame.SourceTimestampUs=long.MaxValue;
            Assert.That(InputTimestampMapping.ToUnity(in frame,200,1000),Is.EqualTo(900));
        }

        [Test] public void RegionOutlineUsesDisplayScaleIndependentlyOfSourceTextureResolution() {
            var go=new GameObject("Actual region display",typeof(RectTransform),typeof(Canvas));
            var uiGo=new GameObject("Actual region settings");
            RenderTexture source720=null,source4k=null;
            try {
                var canvas=go.GetComponent<Canvas>();canvas.renderMode=RenderMode.ScreenSpaceOverlay;canvas.scaleFactor=3;
                var ui=uiGo.AddComponent<HumanVisionRegionSettingsUI>();
                var previewField=typeof(HumanVisionRegionSettingsUI).GetField("preview");
                var preview=(Component)go.AddComponent(previewField.FieldType);previewField.SetValue(ui,preview);
                var width=typeof(HumanVisionRegionSettingsUI).GetProperty("RegionLineWidth",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Instance);
                Assert.NotNull(width,"The actual drawn Region border needs the agreed display-pixel width contract.");
                source720=new RenderTexture(1280,720,0);source4k=new RenderTexture(3840,2160,0);
                var texture=previewField.FieldType.GetProperty("texture");texture.SetValue(preview,source720);
                Assert.That((float)width.GetValue(ui),Is.EqualTo(9));
                texture.SetValue(preview,source4k);Assert.That((float)width.GetValue(ui),Is.EqualTo(9));
                canvas.scaleFactor=1;Assert.That((float)width.GetValue(ui),Is.EqualTo(3));
            } finally {if(source720!=null)UnityEngine.Object.DestroyImmediate(source720);if(source4k!=null)UnityEngine.Object.DestroyImmediate(source4k);UnityEngine.Object.DestroyImmediate(go);UnityEngine.Object.DestroyImmediate(uiGo);}
        }

        [Test] public void PublicStyleDiameterUsesExistingRadiusField() {
            var go=new GameObject("Actual existing overlay",typeof(RectTransform),typeof(CanvasRenderer));
            try {var overlay=go.AddComponent<HumanVisionOverlay>();overlay.ConfigureStyle(9,27);
                Assert.That((float)typeof(HumanVisionOverlay).GetField("jointSize",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Instance).GetValue(overlay),Is.EqualTo(13.5f));
                Assert.That((float)typeof(HumanVisionOverlay).GetField("boneThickness",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Instance).GetValue(overlay),Is.EqualTo(9));
            } finally {UnityEngine.Object.DestroyImmediate(go);}
        }
    }
}
