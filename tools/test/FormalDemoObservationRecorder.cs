using System;
using System.IO;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;

// Add this component to each official generated Demo root. It only observes
// production scheduling. Do not disable Update, invoke Tick, or replace rendering.
// This separate test asset intentionally uses only frozen public v13 APIs.
[DisallowMultipleComponent]
[DefaultExecutionOrder(1000)]
public sealed class FormalDemoObservationRecorder : MonoBehaviour
{
    public HumanVisionManager Manager;
    public InputPreviewControls Input;
    public HumanVisionInputAdapter Adapter;
    public VideoPlayerFrameSource Bridge;
    public bool AutoStart = true;
    public string RunId;
    public string OutputFile;
    public int ObservationCapacity = 4096;
    public int SourceCapacity = 8192;
    public int SampleCapacity = 8192;
    public bool IsRecording { get; private set; }
    public bool IsFinished { get; private set; }
    public string ExportError { get; private set; }
    public string ExportPath { get; private set; }

    private const long DurationUs = 60000000;
    private const int JointsPerBody = 32 + 17 + 6;
    private Header header;
    private Footer footer;
    private Observation[] observations;
    private Source[] sources;
    private Sample[] samples;
    private Body[] bodies;
    private Joint[] joints;
    private int[] regionAssignments;
    private int observationCount, sourceCount, sampleCount, bodyCapacity;
    private long lastBodySequence, lastSourceFrame = -1;
    private bool prepared, overflow, copyInvalid, exportPending;
    private HumanVisionManager subscribedManager;
    private IHumanVisionFrameSource measuredSource;

    [Serializable] private struct Header
    {
        public string kind, run_id, profile;
        public int schema, max_bodies;
        public bool gpu, runtime;
        public long start_unity_us, duration_us, warmup_us, window_us;
        public long begin_sequence, begin_source_frame;
    }
    [Serializable] private struct Footer
    {
        public string kind;
        public long elapsed_us, end_sequence, end_source_frame, hand_only_events;
        public int observation_count, source_count, sample_count;
        public bool overflow, interrupted, copy_invalid;
    }
    [Serializable] private struct Observation
    {
        public string kind;
        public long elapsed_us, sequence, source_frame_id, source_timestamp_us;
        public long unity_before_us, unity_after_us, input_now_us;
        public long submitted, processed_sequence, runtime_drops;
        public long region_revision;
        public bool region_assignments_available;
        public float inference_fps, detection_ms, pose_ms, tracking_ms, total_ms;
        public int body_count;
    }
    [Serializable] private struct Source
    {
        public string kind;
        public long elapsed_us, frame_id, published_us, source_us, pts_us;
        public ulong source_id, generation, source_clock_id, resource_token;
        public int source_clock, timestamp_kind, width, height, rotation, row_origin, color_space;
        public bool mirror, baseline;
    }
    [Serializable] private struct Sample
    {
        public string kind, manager_error, source_error, adapter_error;
        public long elapsed_us, sequence, submitted, processed_sequence, runtime_drops;
        public long bridge_drops, bridge_copies, cpu_readbacks, readback_errors;
        public int source_state;
        public bool error;
    }
    [Serializable] private struct Body
    {
        public int track_id, region_index;
        public long stable_track_id, observation_us;
        public float confidence, x, y, width, height;
    }
    [Serializable] private struct Joint
    {
        public float px, py, nx, ny, confidence, prediction_ms;
        public long observation_us;
        public bool valid, derived;
    }

    private static long UnityUs() => (long)(Time.realtimeSinceStartupAsDouble * 1000000.0);

    private void OnEnable()
    {
        if (Manager == null) Manager = GetComponent<HumanVisionManager>();
        if (Input == null) Input = GetComponent<InputPreviewControls>();
        if (Bridge == null) Bridge = GetComponent<VideoPlayerFrameSource>();
        if (Adapter == null) Adapter = GetComponent<HumanVisionInputAdapter>();
        Subscribe();
    }
    private void Subscribe()
    {
        if (subscribedManager == Manager) return;
        if (subscribedManager != null) subscribedManager.ResultUpdated -= OnResult;
        subscribedManager = Manager;
        if (subscribedManager != null) subscribedManager.ResultUpdated += OnResult;
    }
    private bool Ready => Manager != null && Manager.IsInitialized && Input != null &&
        Input.Source != null && Input.Source.State == InputSourceState.Streaming;

    private void Prepare()
    {
        // Allocate before the measured run. Fixed arrays retain every accepted raw
        // snapshot; capacity exhaustion invalidates instead of overwriting history.
        if (ObservationCapacity < 1 || SourceCapacity < 1 || SampleCapacity < 2)
            throw new ArgumentOutOfRangeException("Recorder capacities must be positive.");
        bodyCapacity = Manager.MaxBodies;
        observations = new Observation[ObservationCapacity];
        sources = new Source[SourceCapacity];
        samples = new Sample[SampleCapacity];
        bodies = new Body[checked(ObservationCapacity * bodyCapacity)];
        joints = new Joint[checked(bodies.Length * JointsPerBody)];
        regionAssignments = new int[bodyCapacity];
        if (string.IsNullOrEmpty(RunId)) RunId = Guid.NewGuid().ToString("N");
        ExportPath = string.IsNullOrEmpty(OutputFile)
            ? Path.Combine(Application.persistentDataPath, "formal-demo-" + RunId + ".jsonl")
            : Path.GetFullPath(OutputFile);
        // Resolve initial auto-created production adapter outside measurement.
        if (Adapter == null) Adapter = GetComponent<HumanVisionInputAdapter>();
        prepared = true;
    }

    private void LateUpdate()
    {
        if (exportPending) { Export(); return; }
        if (IsFinished) return;
        Subscribe();
        if (!IsRecording)
        {
            if (!Ready) return;
            if (!prepared) { Prepare(); return; }
            if (AutoStart) BeginRecording();
            return;
        }
        long now = UnityUs();
        CaptureSource(now, false);
        CaptureSample(now - header.start_unity_us);
        if (now - header.start_unity_us >= DurationUs) Finish(now, false);
    }

    public bool BeginRecording()
    {
        if (IsRecording || IsFinished || !Ready) return false;
        if (!prepared) Prepare();
        measuredSource = Input.Source;
        header = new Header { kind = "header", schema = 1, run_id = RunId,
            profile = Manager.ActiveRuntimeProfile, runtime = Manager.UsesRuntimeProfile,
            gpu = Manager.UsesAndroidGpuFrames, max_bodies = bodyCapacity,
            duration_us = DurationUs, warmup_us = 10000000, window_us = 40000000,
            start_unity_us = UnityUs(), begin_sequence = Manager.ResultSequence };
        lastBodySequence = header.begin_sequence;
        IsRecording = true;
        CaptureSource(header.start_unity_us, true);
        header.begin_source_frame = Math.Max(0, lastSourceFrame);
        CaptureSample(0);
        return true;
    }

    private void CaptureSource(long now, bool baseline)
    {
        // Borrow metadata only. No leases, copies, GPU operations or source Tick.
        if (measuredSource == null || !measuredSource.TryGetLatestFrame(lastSourceFrame, out var frame)) return;
        lastSourceFrame = frame.FrameId;
        if (sourceCount == sources.Length) { overflow = true; return; }
        sources[sourceCount++] = new Source { kind = "source", baseline = baseline,
            elapsed_us = now - header.start_unity_us, source_id = frame.SourceId,
            generation = frame.Generation, frame_id = frame.FrameId,
            published_us = frame.PublishedTimestampUs, source_us = frame.SourceTimestampUs,
            source_clock = (int)frame.SourceClockDomain, source_clock_id = frame.SourceClockId,
            timestamp_kind = (int)frame.TimestampKind, pts_us = frame.PresentationTimestampUs,
            width = frame.Width, height = frame.Height, rotation = frame.AppliedRotationDegrees,
            mirror = frame.AppliedMirrorX, row_origin = (int)frame.RowOrigin,
            color_space = (int)frame.ColorSpace, resource_token = frame.ResourceToken };
    }

    private void CaptureSample(long elapsed)
    {
        if (sampleCount == samples.Length) { overflow = true; return; }
        var stats = Manager.Stats;
        string managerError = Manager.LastError, sourceError = measuredSource?.LastError;
        string adapterError = Adapter != null ? Adapter.LastError : null;
        samples[sampleCount++] = new Sample { kind = "sample", elapsed_us = elapsed,
            sequence = Manager.ResultSequence, submitted = stats.SubmittedFrames,
            // Frozen runtime Stats.ProcessedFrames is native BodySequence, not FreshBodyFrames.
            processed_sequence = stats.ProcessedFrames, runtime_drops = stats.DroppedFrames,
            bridge_drops = Adapter != null ? Adapter.DroppedUnsubmittedFrames : 0,
            bridge_copies = Adapter != null ? Adapter.CopiedFrames : 0,
            cpu_readbacks = Bridge != null ? Bridge.FullFrameReadbackRequests : 0,
            readback_errors = Bridge != null ? Bridge.ReadbackErrors : 0,
            source_state = measuredSource != null ? (int)measuredSource.State : -1,
            error = !string.IsNullOrEmpty(managerError) || !string.IsNullOrEmpty(sourceError) ||
                !string.IsNullOrEmpty(adapterError) || (Bridge != null && Bridge.ReadbackErrors != 0) ||
                Input.Source != measuredSource || Manager.MaxBodies != bodyCapacity ||
                Manager.UsesAndroidGpuFrames != header.gpu || Manager.ActiveRuntimeProfile != header.profile,
            manager_error = managerError, source_error = sourceError, adapter_error = adapterError };
    }

    private void OnResult(long sequence)
    {
        if (!IsRecording) return;
        if (sequence == lastBodySequence) { footer.hand_only_events++; return; }
        lastBodySequence = sequence;
        // These reads are adjacent: the interval contains only the Input clock read,
        // never production Tick, source polling, native inference or body copying.
        long before = UnityUs();
        long inputNow = InputMonotonicClock.NowUs;
        long after = UnityUs();
        if (observationCount == observations.Length) { overflow = true; return; }
        int count = Manager.BodyCount;
        var raw = Manager.Bodies;
        if (raw == null || count < 0 || count > bodyCapacity || raw.Length < count)
        { copyInvalid = true; return; }
        int index = observationCount;
        // ResultUpdated runs inside Manager.Update immediately after PollLatestResult
        // copies raw Bodies. Deep-copy every public raw field now; never SampledBodies.
        for (int i = 0; i < count; i++)
        {
            var body = raw[i];
            if (body == null || body.CanonicalJoints.Length != 32 || body.Joints.Length != 17 || body.HandJoints.Length != 6)
            { copyInvalid = true; return; }
            int bodyIndex = index * bodyCapacity + i;
            bodies[bodyIndex] = new Body { track_id = body.TrackId, stable_track_id = body.StableTrackId,
                region_index = body.RegionIndex, observation_us = body.ObservationTimestampUs,
                confidence = body.DetectionConfidence, x = body.BoundingBoxPixels.x,
                y = body.BoundingBoxPixels.y, width = body.BoundingBoxPixels.width, height = body.BoundingBoxPixels.height };
            int jointBase = bodyIndex * JointsPerBody;
            for (int j = 0; j < 32; j++)
            {
                var canonical = body.CanonicalJoints[j];
                joints[jointBase+j] = CopyJoint(canonical.Position, canonical.ObservationTimestampUs, canonical.PredictionMilliseconds);
            }
            for (int j = 0; j < 17; j++) joints[jointBase+32+j] = CopyJoint(body.Joints[j], 0, 0);
            for (int j = 0; j < 6; j++) joints[jointBase+49+j] = CopyJoint(body.HandJoints[j], 0, 0);
        }
        bool regionsAvailable = Manager.TryCopyRegionAssignments(regionAssignments, out long regionRevision);
        if (regionsAvailable)
            for (int i = 0; i < count; i++)
                if (bodies[index*bodyCapacity+i].region_index != regionAssignments[i]) copyInvalid = true;
        CaptureSource(after, false);
        var stats = Manager.Stats;
        observations[observationCount++] = new Observation { kind = "observation",
            elapsed_us = before-header.start_unity_us, sequence = sequence,
            source_frame_id = Manager.SourceFrameId, source_timestamp_us = Manager.SourceTimestampUs,
            unity_before_us = before, unity_after_us = after, input_now_us = inputNow,
            body_count = count, submitted = stats.SubmittedFrames,
            processed_sequence = stats.ProcessedFrames, runtime_drops = stats.DroppedFrames,
            region_revision = regionRevision, region_assignments_available = regionsAvailable,
            inference_fps = stats.InferenceFps, detection_ms = stats.DetectionMs, pose_ms = stats.PoseMs,
            tracking_ms = stats.TrackingMs, total_ms = stats.TotalMs };
    }

    private static Joint CopyJoint(HumanVisionJoint joint, long timestamp, float prediction)
        => new Joint { px = joint.Pixel.x, py = joint.Pixel.y, nx = joint.Normalized.x,
            ny = joint.Normalized.y, confidence = joint.Confidence, valid = joint.Valid,
            derived = joint.IsDerived, observation_us = timestamp, prediction_ms = prediction };

    private void Finish(long now, bool interrupted)
    {
        IsRecording = false;
        IsFinished = true;
        footer.kind = "footer"; footer.elapsed_us = now-header.start_unity_us;
        footer.end_sequence = Manager != null ? Manager.ResultSequence : lastBodySequence;
        footer.end_source_frame = Math.Max(0, lastSourceFrame);
        footer.observation_count = observationCount; footer.source_count = sourceCount;
        footer.sample_count = sampleCount; footer.overflow = overflow;
        footer.copy_invalid = copyInvalid; footer.interrupted = interrupted;
        exportPending = true;
    }

    private void OnDisable()
    {
        if (subscribedManager != null) subscribedManager.ResultUpdated -= OnResult;
        subscribedManager = null;
        if (IsRecording) Finish(UnityUs(), true);
        if (exportPending) Export();
    }
    private void OnApplicationQuit()
    {
        if (IsRecording) Finish(UnityUs(), true);
        if (exportPending) Export();
    }

    private void Export()
    {
        exportPending = false;
        // JSON and disk IO occur only after measurement ends. No per-frame JSON,
        // logging, list growth or heap buffers are created by the recording path.
        try
        {
            var directory = Path.GetDirectoryName(ExportPath);
            if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
            using (var stream = new FileStream(ExportPath, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
            using (var writer = new StreamWriter(stream))
            {
                writer.WriteLine(JsonUtility.ToJson(header));
                for (int i = 0; i < sourceCount; i++) writer.WriteLine(JsonUtility.ToJson(sources[i]));
                for (int i = 0; i < observationCount; i++)
                {
                    string row = JsonUtility.ToJson(observations[i]);
                    writer.Write(row.Substring(0, row.Length-1)); writer.Write(",\"bodies\":[");
                    for (int b = 0; b < observations[i].body_count; b++)
                    {
                        if (b != 0) writer.Write(',');
                        int bodyIndex = i*bodyCapacity+b;
                        string body = JsonUtility.ToJson(bodies[bodyIndex]);
                        writer.Write(body.Substring(0, body.Length-1));
                        WriteJoints(writer, "canonical_joints", bodyIndex*JointsPerBody, 32);
                        WriteJoints(writer, "joints", bodyIndex*JointsPerBody+32, 17);
                        WriteJoints(writer, "hand_joints", bodyIndex*JointsPerBody+49, 6);
                        writer.Write('}');
                    }
                    writer.WriteLine("]}");
                }
                for (int i = 0; i < sampleCount; i++) writer.WriteLine(JsonUtility.ToJson(samples[i]));
                writer.WriteLine(JsonUtility.ToJson(footer));
            }
            Debug.Log("Formal Demo observation artifact: " + ExportPath);
        }
        catch (Exception error) { ExportError = error.ToString(); Debug.LogError("Formal Demo recording export failed: " + ExportError); }
    }
    private void WriteJoints(StreamWriter writer, string field, int offset, int count)
    {
        writer.Write(",\""); writer.Write(field); writer.Write("\":[");
        for (int i = 0; i < count; i++) { if (i != 0) writer.Write(','); writer.Write(JsonUtility.ToJson(joints[offset+i])); }
        writer.Write(']');
    }
}
