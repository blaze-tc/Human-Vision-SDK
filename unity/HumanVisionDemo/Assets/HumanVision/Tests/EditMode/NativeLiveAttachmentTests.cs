using HumanVision.Demo;
using HumanVision.Input;
using HumanVision.Interop;
using NUnit.Framework;
using UnityEngine;
namespace HumanVision.Tests
{
    public sealed class NativeLiveAttachmentTests
    {
        private sealed class Provider : INativeLiveAttachmentProvider, INativeLiveConsumerApi {
            internal int Calls, BusyUntil = 2, Creates, Failure;
            internal uint Width, Height;
            public int Attach(ref RuntimeLiveConfigNative config, out RuntimeLiveBindingNative binding) {
                binding = new RuntimeLiveBindingNative { SourceId = 77 }; Width = config.MaxWidth; Height = config.MaxHeight;
                return ++Calls <= BusyUntil ? RuntimeControlBindings.Busy : Failure;
            }
            public INativeLiveConsumerApi CreateConsumer(RuntimeLiveBindingNative binding) { ++Creates; return this; }
            public bool TryGetSourceProgress(out HumanVisionSourceProgress progress) { progress = new HumanVisionSourceProgress { SourceId = 77, Generation = 1, Streaming = true, State = 2, DecodedFrames = 100 }; return true; }
            public InputSourceState State => InputSourceState.Streaming;
            public string LastError => "";
            public bool TryGetGeometry(out NativeLiveGeometry geometry) { geometry = default; return false; }
            public int BindTargets(System.IntPtr[] pointers,uint width,uint height,ulong generation) => 0;
            public bool TryPoll(ulong after,out NativeLiveTextureFrame frame) { frame=default;return false; }
            public int ReleaseSlot(uint slot,ulong generation,ulong sequence) => 0;
            public int RetireTargets() => 0;
            public bool TargetsRetired => true;
            public int Close() => 0;
            public bool TryRelease() => true;
            public void PumpRenderEvent() { }
        }
        [Test] public void TransientAttachBusyRetriesWithoutOpeningAnotherSourceOrRecordingPermanentError() {
            var go = new GameObject("live attach retry"); var source = go.AddComponent<HumanVisionNativeFrameSource>(); var provider = new Provider();
            try {
                source.ConfigureLive(provider,false,false,1920,1080);source.Tick();source.Tick();source.Tick();
                Assert.That(provider.Calls,Is.EqualTo(3));Assert.That(provider.Creates,Is.EqualTo(1));
                Assert.That(source.PreviewError,Is.Empty);Assert.That(source.State,Is.EqualTo(InputSourceState.Streaming));
            } finally { source.Close();source.Tick();Object.DestroyImmediate(go); }
        }
        [Test] public void OriginalLiveZeroBoundsReachTheExistingDecoderUnchanged() {
            var go = new GameObject("original live attach"); var source = go.AddComponent<HumanVisionNativeFrameSource>(); var provider = new Provider { BusyUntil = 0 };
            try {
                source.ConfigureLive(provider, false, false, 0, 0);
                Assert.That(provider.Width, Is.Zero); Assert.That(provider.Height, Is.Zero); Assert.That(provider.Creates, Is.EqualTo(1));
            } finally { source.Close(); source.Tick(); Object.DestroyImmediate(go); }
        }
        [Test] public void UnsupportedOriginalPreviewReportsErrorWithoutStoppingHealthyAnalysis() {
            var go = new GameObject("unsupported original preview"); var source = go.AddComponent<HumanVisionNativeFrameSource>();
            var provider = new Provider { BusyUntil = 0, Failure = RuntimeControlBindings.Unsupported };
            try {
                source.ConfigureLive(provider, true, false, 0, 0); source.Tick();
                Assert.That(source.PreviewError, Does.Contain("Original-resolution")); Assert.That(source.PreviewError, Does.Contain("analysis remain active"));
                Assert.That(source.State, Is.EqualTo(InputSourceState.Streaming)); Assert.That(provider.Calls, Is.EqualTo(1)); Assert.That(source.CurrentTexture, Is.Null);
            } finally { source.Close(); source.Tick(); Object.DestroyImmediate(go); }
        }
        [Test] public void CloseCancelsPendingAttachWithoutReopeningTheSource() {
            var go = new GameObject("live attach cancel"); var source = go.AddComponent<HumanVisionNativeFrameSource>(); var provider = new Provider { BusyUntil = int.MaxValue };
            try {
                source.ConfigureLive(provider,false,false,1920,1080);source.Tick();int calls=provider.Calls;source.Close();source.Tick();source.Tick();
                Assert.That(provider.Calls,Is.EqualTo(calls));Assert.That(provider.Creates,Is.Zero);Assert.That(source.State,Is.EqualTo(InputSourceState.Stopped));
            } finally { Object.DestroyImmediate(go); }
        }
    }
}
