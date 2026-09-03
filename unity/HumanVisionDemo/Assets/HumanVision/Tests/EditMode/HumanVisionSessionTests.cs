using System;
using HumanVision.Interop;
using NUnit.Framework;

namespace HumanVision.Tests
{
    public sealed class HumanVisionSessionTests
    {
        [Test]
        public void SubmitFrameReturnsImmediatelyWithNativeAcceptanceResult()
        {
            var api = new FakeNativeApi();
            using (var session = new HumanVisionSession(ValidConfig(2), api))
            {
                bool accepted = session.SubmitFrame(
                    new IntPtr(1234),
                    64,
                    32,
                    256,
                    HumanVisionPixelFormat.Rgba32,
                    41,
                    9000,
                    8192);

                Assert.That(accepted, Is.True);
                Assert.That(api.SubmitCalls, Is.EqualTo(1));
                Assert.That(api.LastFrame.Data, Is.EqualTo(new IntPtr(1234)));
                Assert.That(api.LastFrame.FrameId, Is.EqualTo(41));
                Assert.That(api.LastFrame.TimestampUs, Is.EqualTo(9000));
            }
        }

        [Test]
        public void PollPublishesStableSnapshotOnlyOnce()
        {
            var api = new FakeNativeApi
            {
                Metadata = Metadata(7, 101, 2000, 1),
                Body = NativeBody(33)
            };

            using (var session = new HumanVisionSession(ValidConfig(2), api))
            {
                Assert.That(session.PollLatestResult(), Is.True);
                Assert.That(session.ResultSequence, Is.EqualTo(7));
                Assert.That(session.BodyCount, Is.EqualTo(1));
                Assert.That(session.Bodies[0].TrackId, Is.EqualTo(33));

                Assert.That(session.PollLatestResult(), Is.False);
                Assert.That(api.GetBodiesCalls, Is.EqualTo(1));
            }
        }

        [Test]
        public void PollDoesNotMislabelBodiesWhenSnapshotChangesDuringCopy()
        {
            var api = new FakeNativeApi
            {
                Metadata = Metadata(4, 40, 4000, 1),
                Body = NativeBody(4),
                ChangeSequenceAfterFirstBodyCopy = true
            };

            using (var session = new HumanVisionSession(ValidConfig(2), api))
            {
                Assert.That(session.PollLatestResult(), Is.True,
                    "A second stable read should publish the newer snapshot.");
                Assert.That(session.ResultSequence, Is.EqualTo(5));
                Assert.That(session.SourceFrameId, Is.EqualTo(50));
                Assert.That(session.Bodies[0].TrackId, Is.EqualTo(5));
            }
        }

        [Test]
        public void RuntimeReconfigureGrowsReusableCapacityWithoutRecreatingHandle()
        {
            var api = new FakeNativeApi();
            using (var session = new HumanVisionSession(ValidConfig(1), api))
            {
                IntPtr originalHandle = session.Handle;

                session.ReconfigureMaxBodies(6);

                Assert.That(session.Handle, Is.EqualTo(originalHandle));
                Assert.That(session.MaxBodies, Is.EqualTo(6));
                Assert.That(session.Capacity, Is.EqualTo(6));
                Assert.That(api.CreateCalls, Is.EqualTo(1));
                Assert.That(api.ReconfigureCalls, Is.EqualTo(1));
                Assert.That(api.LastConfig.MaxBodies, Is.EqualTo(6));
            }
        }

        [Test]
        public void NativeFailureIncludesActionableLastErrorAndPreservesPreviousResult()
        {
            var api = new FakeNativeApi
            {
                Metadata = Metadata(1, 10, 1000, 1),
                Body = NativeBody(9)
            };

            using (var session = new HumanVisionSession(ValidConfig(2), api))
            {
                Assert.That(session.PollLatestResult(), Is.True);
                api.GetBodiesResult = HVResult.Internal;
                api.Metadata = Metadata(2, 20, 2000, 1);
                api.LastError = "simulated native inference failure";

                HumanVisionException exception = Assert.Throws<HumanVisionException>(
                    () => session.PollLatestResult());

                StringAssert.Contains("simulated native inference failure", exception.Message);
                Assert.That(session.ResultSequence, Is.EqualTo(1));
                Assert.That(session.Bodies[0].TrackId, Is.EqualTo(9));
            }
        }

        [Test]
        public void MetadataFailureThrowsActionableErrorInsteadOfLookingLikeNoNewResult()
        {
            var api = new FakeNativeApi
            {
                MetadataResult = HVResult.Internal,
                LastError = "simulated metadata channel failure"
            };

            using (var session = new HumanVisionSession(ValidConfig(2), api))
            {
                HumanVisionException exception = Assert.Throws<HumanVisionException>(
                    () => session.PollLatestResult());

                StringAssert.Contains("simulated metadata channel failure", exception.Message);
                Assert.That(api.GetBodiesCalls, Is.Zero);
                Assert.That(session.ResultSequence, Is.Zero);
            }
        }

        private static HumanVisionConfig ValidConfig(int maxBodies)
        {
            return new HumanVisionConfig
            {
                MaxBodies = maxBodies,
                DetectorModelPath = "detector.onnx",
                PoseModelPath = "pose.onnx"
            };
        }

        private static HVResultMetaNative Metadata(long sequence, long frameId, long timestamp, int bodyCount)
        {
            return new HVResultMetaNative
            {
                StructSize = NativeBindings.ResultMetaSize,
                ResultSequence = sequence,
                SourceFrameId = frameId,
                SourceTimestampUs = timestamp,
                BodyCount = bodyCount
            };
        }

        private static HVBodyNative NativeBody(int trackId)
        {
            return new HVBodyNative
            {
                StructSize = NativeBindings.BodySize,
                TrackId = trackId,
                BoundingBox = new HVRectNative { X = 1f, Y = 2f, Width = 3f, Height = 4f },
                DetectionConfidence = 0.9f,
                Joint0 = new HVJointNative { X = 5f, Y = 6f, Confidence = 0.8f, Valid = 1 }
            };
        }

        private sealed class FakeNativeApi : IHumanVisionNativeApi
        {
            internal HVResultMetaNative Metadata;
            internal HVBodyNative Body;
            internal HVVideoFrameNative LastFrame;
            internal HVConfigNative LastConfig;
            internal HVResult GetBodiesResult = HVResult.Ok;
            internal HVResult MetadataResult = HVResult.Ok;
            internal string LastError = "fake error";
            internal bool ChangeSequenceAfterFirstBodyCopy;
            internal int CreateCalls;
            internal int ReconfigureCalls;
            internal int SubmitCalls;
            internal int GetBodiesCalls;

            public HVResult Create(ref HVConfigNative config, out IntPtr handle)
            {
                CreateCalls++;
                LastConfig = config;
                handle = new IntPtr(77);
                return HVResult.Ok;
            }

            public HVResult Reconfigure(IntPtr handle, ref HVConfigNative config)
            {
                ReconfigureCalls++;
                LastConfig = config;
                return HVResult.Ok;
            }

            public HVResult SubmitFrame(IntPtr handle, ref HVVideoFrameNative frame)
            {
                SubmitCalls++;
                LastFrame = frame;
                return HVResult.Ok;
            }

            public HVResult GetLatestResultMeta(IntPtr handle, ref HVResultMetaNative metadata)
            {
                metadata = Metadata;
                if (MetadataResult != HVResult.Ok)
                {
                    return MetadataResult;
                }

                return Metadata.ResultSequence > 0 ? HVResult.Ok : HVResult.NoNewResult;
            }

            public HVResult GetBodies(IntPtr handle, IntPtr bodies, int capacity, out int written)
            {
                GetBodiesCalls++;
                written = Metadata.BodyCount;
                if (GetBodiesResult != HVResult.Ok)
                {
                    return GetBodiesResult;
                }

                unsafe
                {
                    *(HVBodyNative*)bodies.ToPointer() = Body;
                }

                if (ChangeSequenceAfterFirstBodyCopy && GetBodiesCalls == 1)
                {
                    Metadata = HumanVisionSessionTests.Metadata(5, 50, 5000, 1);
                    Body = NativeBody(5);
                }

                return HVResult.Ok;
            }

            public HVResult GetStats(IntPtr handle, ref HVStatsNative stats)
            {
                stats = new HVStatsNative
                {
                    StructSize = NativeBindings.StatsSize,
                    InputFps = 30f,
                    InferenceFps = 5f,
                    DroppedFrames = 3
                };
                return HVResult.Ok;
            }

            public string GetLastError(IntPtr handle)
            {
                return LastError;
            }

            public void Destroy(IntPtr handle)
            {
            }
        }
    }
}
