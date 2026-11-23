using System;
using System.IO;
using System.Reflection;
using HumanVision;
using HumanVision.Input;
using UnityEditor;
using UnityEngine;

// Manual inspection of SDK-owned data; never captures the desktop or drives inference.
public static class RuntimePrefabIndependentInspect
{
    [Serializable] private sealed class Snapshot
    {
        public string utc, backend, profile, inputState, error, execution, configuredSource, configuredQuality, configuredPolicy;
        public bool playing, fresh, hasPreview, bindingConfirmed, nativeStreaming, automated, operating, hasNativeEpochField, hasRuntimeClock, hasNativeProgress, configuredPreview, configuredRegions;
        public int targetFps;
        public long runtimeClock = -1;
        public long resultSequence, resultFrame, previewFrame, renderedFrame;
        public ulong source, generation, epoch, previewSource, previewGeneration, freshFrames, inputFrames, nativeEpoch;
        public int users, maxBodies, managerRawBodies, managerSampledBodies, visibleBodies, visibleJoints, visibleBones, previewWidth, previewHeight;
        public int[] rawJoints, sampledJoints, managerSampledRegions, managerSampledJoints;
        public long[] managerSampledTimes, managerSampledIds;
        public string pixelHash;
        public int minimumLuma, maximumLuma;
    }
    private static object Field(object owner, string name) => owner?.GetType().GetField(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)?.GetValue(owner);
    private static int Number(object owner, string name) => Convert.ToInt32(owner?.GetType().GetProperty(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)?.GetValue(owner) ?? 0);

    [MenuItem("Human Vision/Acceptance/Inspect Visible Runtime")]
    public static void Inspect()
    {
        string root = Path.GetDirectoryName(Application.dataPath);
        if (Path.GetFileName(root) != "Human-Vision-SDK-Test" || !EditorApplication.isPlaying) throw new InvalidOperationException("The acceptance scene must be playing.");
        MonoBehaviour acceptance = null;
        foreach (var candidate in Resources.FindObjectsOfTypeAll<MonoBehaviour>())
            if (candidate != null && candidate.gameObject.scene.IsValid() && candidate.GetType().FullName == "HumanVisionAcceptance.RuntimePrefabAcceptance") { acceptance = candidate; break; }
        var view = Field(acceptance, "view") as HumanVisionRuntimeView;
        var sdk = view != null ? view.Sdk : null;
        if (sdk == null) { Debug.Log("HV_VISIBLE_RUNTIME_INSPECTION waiting for the acceptance SDK"); return; }
        var configuration = sdk.IsInitialized ? sdk.Configuration : null;
        var geometry = Field(acceptance, "liveSkeleton");
        var manager = typeof(HumanVisionSdk).GetProperty("RuntimeManager", BindingFlags.Instance | BindingFlags.NonPublic)?.GetValue(sdk) as HumanVisionManager;
        var snapshot = new Snapshot {
            utc = DateTime.UtcNow.ToString("O"), playing = true, fresh = sdk.HasFreshResult,
            resultSequence = sdk.ResultSequence, resultFrame = sdk.SourceFrameId, renderedFrame = Time.renderedFrameCount,
            users = sdk.GetUsersCount(), maxBodies = sdk.GetMaxBodies(), inputState = sdk.InputState.ToString(), error = sdk.LastError,
            configuredSource = configuration?.SourceKind.ToString(), configuredQuality = configuration?.InputQuality.ToString(),
            configuredPolicy = configuration?.ExecutionPolicy.ToString(), configuredPreview = configuration != null && configuration.PreviewEnabled,
            configuredRegions = configuration != null && configuration.Recognition.UseRegions, targetFps = Application.targetFrameRate,
            backend = Convert.ToString(Field(acceptance, "actualBackend")), profile = Convert.ToString(Field(acceptance, "diagnosticProfile")),
            visibleBodies = Number(geometry, "VisibleBodies"), visibleJoints = Number(geometry, "VisibleJoints"), visibleBones = Number(geometry, "VisibleBones"),
            bindingConfirmed = Convert.ToBoolean(Field(geometry, "bindingConfirmed") ?? false), nativeEpoch = Convert.ToUInt64(Field(geometry, "nativeEpoch") ?? 0UL),
            hasNativeEpochField = geometry?.GetType().GetField("nativeEpoch", BindingFlags.Instance | BindingFlags.NonPublic) != null,
            automated = Convert.ToBoolean(Field(acceptance, "automatedFixture") ?? false), operating = Convert.ToBoolean(Field(acceptance, "operating") ?? false),
            managerRawBodies = manager != null ? manager.BodyCount : -1, managerSampledBodies = manager != null ? manager.SampledBodyCount : -1
        };
        snapshot.rawJoints = new int[snapshot.maxBodies]; snapshot.sampledJoints = new int[snapshot.maxBodies];
        snapshot.managerSampledRegions = new int[snapshot.managerSampledBodies > 0 ? snapshot.managerSampledBodies : 0];
        snapshot.managerSampledJoints = new int[snapshot.managerSampledRegions.Length];
        snapshot.managerSampledTimes = new long[snapshot.managerSampledRegions.Length]; snapshot.managerSampledIds = new long[snapshot.managerSampledRegions.Length];
        for (int i = 0; i < snapshot.managerSampledRegions.Length; i++) {
            snapshot.managerSampledRegions[i] = manager.SampledBodies[i].RegionIndex;
            snapshot.managerSampledTimes[i] = manager.SampledBodies[i].ObservationTimestampUs; snapshot.managerSampledIds[i] = manager.SampledBodies[i].StableTrackId;
            foreach (var joint in manager.SampledBodies[i].CanonicalJoints) if (joint.Position.Valid) snapshot.managerSampledJoints[i]++;
        }
        var bindingType = typeof(HumanVisionManager).Assembly.GetType("HumanVision.Interop.RuntimeBindings");
        var clockMethod = bindingType?.GetMethod("HV_RuntimeClockUs", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public);
        if (clockMethod != null) { snapshot.runtimeClock = Convert.ToInt64(clockMethod.Invoke(null, null)); snapshot.hasRuntimeClock = snapshot.runtimeClock >= 0; }
        for (int slot = 0; slot < snapshot.maxBodies; slot++) {
            if (sdk.TryGetBodyByIndex(slot, out var raw)) foreach (var joint in raw.CanonicalJoints) if (joint.Position.Valid) snapshot.rawJoints[slot]++;
            if (sdk.TryGetSampledBodyByIndex(slot, out var sampled)) foreach (var joint in sampled.CanonicalJoints) if (joint.Position.Valid) snapshot.sampledJoints[slot]++;
        }
        if (sdk.TryGetRuntimeProgress(out var progress)) {
            snapshot.hasNativeProgress = true;
            snapshot.source = progress.SourceId; snapshot.generation = progress.Generation; snapshot.epoch = progress.ConfigEpoch;
            snapshot.freshFrames = progress.NativeFreshFrames; snapshot.inputFrames = progress.NativeInputFrames;
            snapshot.execution = progress.ExecutionMode; snapshot.nativeStreaming = progress.Streaming;
        }
        snapshot.hasPreview = sdk.TryGetInputFrame(-1, out var preview);
        if (snapshot.hasPreview) {
            snapshot.previewFrame = preview.FrameId; snapshot.previewSource = preview.SourceId; snapshot.previewGeneration = preview.Generation;
            snapshot.previewWidth = preview.Width; snapshot.previewHeight = preview.Height;
            InspectPixels(preview.Texture, snapshot);
        }
        string directory = Path.Combine(root, "Builds/IndependentRuntime-20261123/VisibleRuntime"); Directory.CreateDirectory(directory);
        string path = Path.Combine(directory, DateTime.UtcNow.ToString("yyyyMMddTHHmmssfffZ") + ".json");
        File.WriteAllText(path, JsonUtility.ToJson(snapshot, true));
        Debug.Log("HV_VISIBLE_RUNTIME_INSPECTION " + path);
    }
    private static void InspectPixels(Texture source, Snapshot result)
    {
        var before = RenderTexture.active;
        var target = RenderTexture.GetTemporary(64, 36, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
        var read = new Texture2D(64, 36, TextureFormat.RGBA32, false, true);
        try {
            Graphics.Blit(source, target); RenderTexture.active = target;
            read.ReadPixels(new Rect(0, 0, 64, 36), 0, 0); read.Apply(false, false);
            ulong hash = 14695981039346656037UL; result.minimumLuma = 255;
            foreach (var pixel in read.GetPixels32()) {
                int luma = (pixel.r * 54 + pixel.g * 183 + pixel.b * 19) / 256;
                result.minimumLuma = Math.Min(result.minimumLuma, luma); result.maximumLuma = Math.Max(result.maximumLuma, luma);
                unchecked { hash = (hash ^ pixel.r) * 1099511628211UL; hash = (hash ^ pixel.g) * 1099511628211UL; hash = (hash ^ pixel.b) * 1099511628211UL; }
            }
            result.pixelHash = hash.ToString("x16");
        } finally { RenderTexture.active = before; RenderTexture.ReleaseTemporary(target); UnityEngine.Object.DestroyImmediate(read); }
    }
}
