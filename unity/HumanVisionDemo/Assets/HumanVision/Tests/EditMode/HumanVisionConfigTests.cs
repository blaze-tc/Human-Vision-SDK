using System;
using NUnit.Framework;

namespace HumanVision.Tests
{
    public sealed class HumanVisionConfigTests
    {
        [Test]
        public void DefaultsMatchPublishedDemoContract()
        {
            var config = new HumanVisionConfig();

            Assert.That(config.MaxBodies, Is.EqualTo(4));
            Assert.That(config.DetectionThreshold, Is.EqualTo(0.35f));
            Assert.That(config.PoseThreshold, Is.EqualTo(0.30f));
            Assert.That(config.DetectionInterval, Is.EqualTo(1));
            Assert.That(config.EnableTracking, Is.True);
        }

        [TestCase(0, 0.35f, 0.30f, 1, "MaxBodies")]
        [TestCase(4, -0.01f, 0.30f, 1, "DetectionThreshold")]
        [TestCase(4, 0.35f, 1.01f, 1, "PoseThreshold")]
        [TestCase(4, 0.35f, 0.30f, 0, "DetectionInterval")]
        public void InvalidNumericConfigurationIsRejected(
            int maxBodies,
            float detectionThreshold,
            float poseThreshold,
            int detectionInterval,
            string expectedField)
        {
            var config = ValidConfig();
            config.MaxBodies = maxBodies;
            config.DetectionThreshold = detectionThreshold;
            config.PoseThreshold = poseThreshold;
            config.DetectionInterval = detectionInterval;

            ArgumentException exception = Assert.Throws<ArgumentException>(() => config.Validate());
            StringAssert.Contains(expectedField, exception.Message);
        }

        [TestCase(null, "pose.onnx", "DetectorModelPath")]
        [TestCase("", "pose.onnx", "DetectorModelPath")]
        [TestCase("detector.onnx", null, "PoseModelPath")]
        [TestCase("detector.onnx", "", "PoseModelPath")]
        public void MissingModelPathIsRejected(string detectorPath, string posePath, string expectedField)
        {
            var config = ValidConfig();
            config.DetectorModelPath = detectorPath;
            config.PoseModelPath = posePath;

            ArgumentException exception = Assert.Throws<ArgumentException>(() => config.Validate());
            StringAssert.Contains(expectedField, exception.Message);
        }

        [Test]
        public void RuntimeMaxBodiesCanExceedDemoDefault()
        {
            var config = ValidConfig();
            config.MaxBodies = 8;

            Assert.DoesNotThrow(config.Validate);
        }

        private static HumanVisionConfig ValidConfig()
        {
            return new HumanVisionConfig
            {
                DetectorModelPath = "detector.onnx",
                PoseModelPath = "pose.onnx"
            };
        }
    }
}
