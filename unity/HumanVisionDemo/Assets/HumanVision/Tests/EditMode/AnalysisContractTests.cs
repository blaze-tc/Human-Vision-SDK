using System;
using System.IO;
using HumanVision.Demo;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class AnalysisContractTests
    {
        private string root;
        // Accepted Android rectangle640 profile and pack are copied unchanged as fixtures
        // by the isolated runner. The embedded literals keep normal SDK tests self-contained.
        private const string YoloProfile = @"{""schema_version"":1,""profile"":""android-ncnn-vulkan"",""local_evaluation_only"":true,""frame_policy"":""every_frame"",""body"":{""pipeline"":""pipeline.yolo.pose"",""modelPack"":""yolov8n-pose-rectangle640x384-fp32-local""},""hands"":{""enabled"":false},""backend"":{""preference"":[""backend.ncnn.vulkan""],""allow_fallback"":false},""body_fps"":30,""output"":{""hz"":60}}";
        private const string YoloPack = @"{""schema_version"":2,""pack_id"":""yolov8n-pose-rectangle640x384-fp32-local"",""pipeline_id"":""pipeline.yolo.pose"",""max_people"":8,""local_evaluation_only"":true,""execution_contract"":""raw_tensor_fp32_v1"",""models"":[{""role"":""body"",""format"":""ncnn"",""decoder_id"":""yolov8_pose_dfl17_v1"",""input_contract"":{""image_format"":""rgba8-unorm"",""color_order"":""rgb"",""crop"":""letterbox"",""resize_interpolation"":""bilinear"",""pad_rgb"":[114,114,114],""normalization"":{""mean"":[0,0,0],""norm"":[0.00392156862745098,0.00392156862745098,0.00392156862745098]},""width"":640,""height"":384,""tensor_dtype"":""fp32"",""elempack"":1,""input_blob"":""in0""},""output_contract"":{""decoder"":""yolov8_pose_dfl17_v1"",""output_blobs"":[""out0"",""out1""],""max_output_bytes"":{""out0"":1310400,""out1"":1028160}}}]}";
        [SetUp] public void SetUp() { root = Path.Combine(Path.GetTempPath(), "HumanVisionAnalysis-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(Path.Combine(root, "profiles")); }
        [TearDown] public void TearDown() { if (Directory.Exists(root)) Directory.Delete(root, true); }
        private string Write(string relative, string text) { string path = Path.Combine(root, relative); Directory.CreateDirectory(Path.GetDirectoryName(path)); File.WriteAllText(path, text); return path; }
        private string WriteYolo()
        {
            string fixture = Path.Combine(Application.dataPath, "Accepted640Fixtures");
            Write("profiles/android-ncnn-vulkan.json", File.Exists(Path.Combine(fixture, "profile.json")) ? File.ReadAllText(Path.Combine(fixture, "profile.json")) : YoloProfile);
            return Write("modelpacks/yolov8n-pose-rectangle640x384-fp32-local/modelpack.json", File.Exists(Path.Combine(fixture, "modelpack.json")) ? File.ReadAllText(Path.Combine(fixture, "modelpack.json")) : YoloPack);
        }
        [TestCase(1)] [TestCase(4)] [TestCase(8)]
        public void AcceptedSchema2IntegratedPoseAdmits640EveryFrameAndPreservesPeopleRegions(int people)
        {
            WriteYolo();
            var contract = AnalysisContract.Load(root, "android-ncnn-vulkan", people);
            var regions = new[] { new Rect(0, 0, .5f, 1), new Rect(.5f, 0, .5f, 1) };
            var settings = new SharedRecognitionSettings { MaxBodies = people, Regions = regions };
            contract.ApplyTo(settings); contract.Validate(settings);
            Assert.That(settings.ModelPackId, Is.EqualTo("yolov8n-pose-rectangle640x384-fp32-local"));
            Assert.That(settings.DetectorWidth, Is.Zero); Assert.That(settings.DetectorHeight, Is.Zero);
            Assert.That(settings.PoseWidth, Is.EqualTo(640)); Assert.That(settings.PoseHeight, Is.EqualTo(384));
            Assert.That(settings.DetectionCadence, Is.EqualTo(1)); Assert.That(settings.MaxBodies, Is.EqualTo(people));
            Assert.That(settings.Regions, Is.SameAs(regions));
            settings.PoseWidth = 576;
            Assert.Throws<ArgumentException>(() => contract.Validate(settings), "576 candidate must not pass as accepted640.");
        }
        [TestCase("precision-t-26", 192, 256, 320)]
        [TestCase("rtmo-t-416", 416, 416, 0)]
        public void ExistingSchema1ManifestRemainsReadable(string pack, int width, int height, int detector)
        {
            Write("profiles/cpu.json", "{\"profile\":\"cpu\",\"body\":{\"modelPack\":\"" + pack + "\"}}");
            string det = detector == 0 ? "" : "{\"role\":\"detector\",\"input_contract\":{\"width\":320,\"height\":320}},";
            Write("modelpacks/" + pack + "/manifest.json", "{\"schema_version\":1,\"pack_id\":\"" + pack + "\",\"models\":[" + det + "{\"role\":\"body\",\"input_contract\":{\"width\":" + width + ",\"height\":" + height + "}}]}");
            var contract = AnalysisContract.Load(root, "cpu", 4);
            Assert.That(contract.PoseWidth, Is.EqualTo(width)); Assert.That(contract.PoseHeight, Is.EqualTo(height));
            Assert.That(contract.DetectorWidth, Is.EqualTo(detector)); Assert.That(contract.DetectionCadence, Is.EqualTo(1));
        }
        [Test] public void ExistingSchema2SeparateDetectorPreservesCadenceAndDimensions()
        {
            Write("profiles/android-ncnn-vulkan.json", @"{""profile"":""android-ncnn-vulkan"",""detector"":{""cadence_interval_frames"":4},""body"":{""modelPack"":""precision-t-26-ncnn-fp16""}}");
            Write("modelpacks/precision-t-26-ncnn-fp16/modelpack.json", @"{""schema_version"":2,""pack_id"":""precision-t-26-ncnn-fp16"",""models"":[{""role"":""detector"",""input_contract"":{""width"":320,""height"":320}},{""role"":""body"",""input_contract"":{""width"":192,""height"":256}}]}");
            var contract = AnalysisContract.Load(root, "android-ncnn-vulkan", 8);
            Assert.That(contract.DetectorWidth, Is.EqualTo(320)); Assert.That(contract.DetectorHeight, Is.EqualTo(320));
            Assert.That(contract.PoseWidth, Is.EqualTo(192)); Assert.That(contract.PoseHeight, Is.EqualTo(256));
            Assert.That(contract.DetectionCadence, Is.EqualTo(4));
        }
        [Test] public void BothManifestNamesFailInsteadOfChoosingAnUnintendedPack()
        {
            string selected = WriteYolo();
            File.Copy(selected, Path.Combine(Path.GetDirectoryName(selected), "manifest.json"));
            var error = Assert.Throws<InvalidDataException>(() => AnalysisContract.Load(root, "android-ncnn-vulkan", 4));
            StringAssert.Contains("both", error.Message); StringAssert.Contains("manifest.json", error.Message); StringAssert.Contains("modelpack.json", error.Message);
        }
        [Test] public void MissingManifestNamesFailWithSelectedPackAndAction()
        {
            File.Delete(WriteYolo());
            var error = Assert.Throws<InvalidDataException>(() => AnalysisContract.Load(root, "android-ncnn-vulkan", 4));
            StringAssert.Contains("yolov8n-pose-rectangle640x384-fp32-local", error.Message);
            StringAssert.Contains("manifest.json", error.Message); StringAssert.Contains("modelpack.json", error.Message);
        }
    }
}
