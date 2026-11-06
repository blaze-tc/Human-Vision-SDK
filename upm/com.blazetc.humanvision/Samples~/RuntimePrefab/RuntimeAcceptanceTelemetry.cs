using System;
using System.Diagnostics;
using HumanVision;
using UnityEngine;
using UnityEngine.Profiling;

namespace HumanVisionAcceptance
{
    internal sealed class RuntimeAcceptanceTelemetry
    {
        private double windowStart;
        private int updates, pairedUpdates, poseUpdates, newPairs;
        private long lastSequence, lastNative;
        private ulong lastSource, lastGeneration;
        private double updateTicks, maxFrameSeconds, lastPairAt, maxPairInterval;
        public string PerformanceLine { get; private set; } = "正在测量游戏、识别和 Update 耗时…";
        public string ReadLine { get; private set; } = "正在测量 Update 取数…";

        internal void Reset() { windowStart = Time.realtimeSinceStartupAsDouble; }

        internal void Observe(long startTicks, HumanVisionSdk sdk, bool paired, HumanVisionPresentedFrame frame, int joints, int bones)
        {
            double now = Time.realtimeSinceStartupAsDouble;
            ++updates;
            updateTicks += Stopwatch.GetTimestamp() - startTicks;
            maxFrameSeconds = Math.Max(maxFrameSeconds, Time.unscaledDeltaTime);
            bool validTextures = paired && frame.ColorTexture != null && frame.OverlayTexture != null;
            if (validTextures) {
                ++pairedUpdates;
                if (joints > 0) ++poseUpdates;
                if (frame.SourceId != lastSource || frame.SourceGeneration != lastGeneration ||
                    frame.ResultSequence != lastSequence || frame.SourceFrameId != lastNative) {
                    ++newPairs;
                    if (lastPairAt > 0 && frame.SourceId == lastSource && frame.SourceGeneration == lastGeneration)
                        maxPairInterval = Math.Max(maxPairInterval, now - lastPairAt);
                    lastPairAt = now;
                    lastSource = frame.SourceId;
                    lastGeneration = frame.SourceGeneration;
                    lastSequence = frame.ResultSequence;
                    lastNative = frame.SourceFrameId;
                }
            }
            if (sdk == null || !sdk.IsRunning) {
                lastPairAt = 0;
                maxPairInterval = 0;
            } else if (lastPairAt > 0) {
                maxPairInterval = Math.Max(maxPairInterval, now - lastPairAt);
            }
            double seconds = now - windowStart;
            if (seconds < 1) return;

            double fps = updates / seconds, averageFrame = 1000 / fps;
            double updateMs = updateTicks * 1000 / (Stopwatch.Frequency * (double)updates);
            var stats = sdk != null ? sdk.Stats : default;
            double inputFps = sdk?.InputPublicationFps ?? 0, freshFps = sdk?.FreshResultFps ?? 0;
            double ageMs = sdk != null && sdk.HasFreshResult ? sdk.ResultAgeMilliseconds : -1;
            long memoryMiB = Profiler.GetTotalAllocatedMemoryLong() / 1048576;
            double fpsGap = inputFps - freshFps;
            string pairInterval = maxPairInterval > 0 ? $"{maxPairInterval * 1000:F0} ms" : "—";
            PerformanceLine = $"游戏 {fps:F1} FPS / {averageFrame:F1} ms  |  输入 {inputFps:F1} FPS  |  新识别 {freshFps:F1} FPS  |  差 {fpsGap:+0.0;-0.0;0.0}  |  SDK {stats.TotalMs:F1} ms  |  Unity {memoryMiB} MiB";
            ReadLine = $"Update 取帧 {pairedUpdates}/{updates} · 取点 {poseUpdates}/{updates} · 新配对 {newPairs}  |  最长配对间隔 {pairInterval}  |  取数 + 提交绘制 {updateMs:F2} ms";
            if (Application.platform == RuntimePlatform.Android) {
                var device = SystemInfo.graphicsDeviceType;
                string profile = sdk != null && sdk.IsRunning ? sdk.RuntimeProfile : "-";
                string state = sdk?.State.ToString() ?? "-";
                long input = paired ? frame.InputFrameId : -1, native = paired ? frame.SourceFrameId : -1;
                long seq = paired ? frame.ResultSequence : -1;
                int width = validTextures ? frame.ColorTexture.width : 0, height = validTextures ? frame.ColorTexture.height : 0;
                int bodies = paired ? frame.BodyCount : 0;
                UnityEngine.Debug.Log(FormattableString.Invariant($"HV_RT_SAMPLE state={state} profile={profile} gfx={device} game_fps={fps:F2} frame_ms={averageFrame:F2} max_frame_ms={maxFrameSeconds*1000:F2} input_fps={inputFps:F2} fresh_fps={freshFps:F2} fps_gap={fpsGap:F2} max_pair_gap_ms={maxPairInterval*1000:F2} sdk_ms={stats.TotalMs:F2} age_ms={ageMs:F2} update_ms={updateMs:F3} mem_mib={memoryMiB} updates={updates} paired_updates={pairedUpdates} joint_updates={poseUpdates} new_pairs={newPairs} bodies={bodies} joints={joints} bones={bones} size={width}x{height} input={input} native={native} seq={seq} dropped={stats.DroppedFrames}"));
            }

            updates = pairedUpdates = poseUpdates = newPairs = 0;
            updateTicks = maxFrameSeconds = maxPairInterval = 0;
            windowStart = now;
        }
    }
}
