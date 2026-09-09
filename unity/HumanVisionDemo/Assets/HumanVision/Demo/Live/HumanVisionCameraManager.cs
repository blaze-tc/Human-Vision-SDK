using System;
using System.Collections;
using System.IO;
using HumanVision.Demo;
using UnityEngine;
using UnityEngine.Networking;

namespace HumanVision
{
    public enum HumanVisionJointType { Nose, LeftEye, RightEye, LeftEar, RightEar, LeftShoulder,
        RightShoulder, LeftElbow, RightElbow, LeftWrist, RightWrist, LeftHip, RightHip,
        LeftKnee, RightKnee, LeftAnkle, RightAnkle, LeftHand, LeftHandtip, LeftThumb, RightHand, RightHandtip, RightThumb }

    [RequireComponent(typeof(HumanVisionManager), typeof(VideoPlayerFrameSource), typeof(HumanVisionLiveSource))]
    public sealed class HumanVisionCameraManager : MonoBehaviour
    {
        public static HumanVisionCameraManager Instance { get; private set; }
        public HumanVisionCameraSettings Settings = new HumanVisionCameraSettings();
        public bool startAutomatically;
        [Tooltip("Disable automatic device acceleration for CPU comparison; restart the app after changing this.")]
        public bool forceCpu;
        public string Status { get; private set; } = "Initializing";
        public bool IsReady => _manager != null && _manager.IsInitialized;
        public string InputStatus => _source != null ? _source.Status : "Stopped";
        public long ResultSequence => _manager != null ? _manager.ResultSequence : 0;
        public event Action<long> SkeletonUpdated;
        private HumanVisionManager _manager;
        private VideoPlayerFrameSource _bridge;
        private HumanVisionLiveSource _source;
        private HumanVisionBody[] _slots = Array.Empty<HumanVisionBody>();
        private int[] _assignments = Array.Empty<int>();
        private long _revision;
        private bool _regionsEnabled;
        private string _detectorPath, _posePath;

        private void Awake()
        {
            if (Instance != null && Instance != this) { enabled = false; return; }
            Instance = this;
            _manager = GetComponent<HumanVisionManager>();
            _bridge = GetComponent<VideoPlayerFrameSource>();
            _source = GetComponent<HumanVisionLiveSource>();
        }
        private IEnumerator Start()
        {
            try { Settings = HumanVisionCameraSettings.Load(); Settings.ResizeRegions(Settings.people); }
            catch (Exception e) { Status = e.Message; yield break; }
            string root = Path.Combine(Application.persistentDataPath, "HumanVisionModels");
            Directory.CreateDirectory(root);
            string[] files = { "rtmdet_tiny_person_640.onnx", "rtmpose_s_133.onnx" };
            foreach (string file in files) {
                string source = Application.streamingAssetsPath + "/HumanVision/Models/" + file;
                string target = Path.Combine(root, file);
                // UnityWebRequest also reads Android APK StreamingAssets (jar:file).
                using (var request = UnityWebRequest.Get(source.Contains("://") ? source : new Uri(source).AbsoluteUri)) {
                    request.downloadHandler = new DownloadHandlerFile(target + ".tmp");
                    yield return request.SendWebRequest();
                    if (request.result != UnityWebRequest.Result.Success) { Status = "Model extraction failed: " + request.error; yield break; }
                }
                File.Copy(target + ".tmp", target, true); File.Delete(target + ".tmp");
            }
            _detectorPath = Path.Combine(root, files[0]); _posePath = Path.Combine(root, files[1]);
            if (!_manager.TryInitialize(new HumanVisionConfig { MaxBodies = Settings.people,
                DetectorModelPath = _detectorPath, PoseModelPath = _posePath, EnableTracking = true,
                UseHardwareAcceleration = !forceCpu,
                DetectionInterval = Application.platform == RuntimePlatform.Android ? 2 : 1 })) {
                Status = _manager.LastError; yield break;
            }
            if (!ApplySettings()) yield break;
            Status = "Ready. Select camera and press Start.";
            if (startAutomatically) StartCamera();
        }
        private void OnEnable()
        {
            if (_manager != null) _manager.ResultUpdated += OnResult;
        }
        private void OnDisable()
        {
            if (_manager != null) _manager.ResultUpdated -= OnResult;
            StopCamera();
        }
        private void OnDestroy() { if (Instance == this) Instance = null; }
        public bool ApplySettings()
        {
            if (!IsReady) { Status = "SDK is not ready"; return false; }
            try {
                Settings.Validate();
                long revision = ++_revision;
                _bridge.StopFrames();
                Array.Clear(_slots, 0, _slots.Length);
                if (!_manager.TrySetRegions(Array.Empty<Rect>(), revision) ||
                    !_manager.TrySetMaxBodies(Settings.people) ||
                    !_manager.TrySetRegions(Settings.useRegions ? Settings.regions : Array.Empty<Rect>(), revision)) {
                    Status = _manager.LastError; return false;
                }
                _slots = new HumanVisionBody[Settings.people];
                _assignments = new int[Settings.people];
                _regionsEnabled = Settings.useRegions;
                Status = "Settings applied";
                return true;
            } catch (Exception e) { Status = e.Message; return false; }
        }
        public void StartCamera()
        {
            if (!ApplySettings()) return;
            try { _source.Open(Settings); Status = "Camera requested"; }
            catch (Exception e) { Status = e.Message; }
        }
        public void StopCamera() { if (_source != null) _source.Close(); Array.Clear(_slots, 0, _slots.Length); }
        public void SaveSettings()
        {
            if (!ApplySettings()) return;
            try { Settings.Save(); Status = "Saved to " + HumanVisionCameraSettings.FilePath; }
            catch (Exception e) { Status = e.Message; }
        }
        public void LoadSettings()
        {
            try { Settings = HumanVisionCameraSettings.Load(); Settings.ResizeRegions(Settings.people); ApplySettings(); }
            catch (Exception e) { Status = e.Message; }
        }
        private void OnResult(long sequence)
        {
            Array.Clear(_slots, 0, _slots.Length);
            if (!_manager.TryCopyRegionAssignments(_assignments, out long revision) || revision != _revision) return;
            for (int i = 0; i < _manager.BodyCount; i++) {
                int slot = _regionsEnabled ? _assignments[i] : i;
                if (slot >= 0 && slot < _slots.Length) _slots[slot] = _manager.Bodies[i];
            }
            SkeletonUpdated?.Invoke(sequence);
        }
        private bool Fresh => IsReady && _source.HasRecentFrame && _bridge.CanPresentResult(_manager.SourceFrameId);
        public Texture GetColorImageTex() => _bridge != null ? _bridge.PresentationTexture : null;
        public int GetColorImageWidth() => GetColorImageTex() != null ? GetColorImageTex().width : 0;
        public int GetColorImageHeight() => GetColorImageTex() != null ? GetColorImageTex().height : 0;
        public int GetJointCount() => 23;
        public int GetUsersCount() { if (!Fresh) return 0; int count = 0; foreach (var body in _slots) if (body != null) count++; return count; }
        public int GetRegionCount() => _slots.Length;
        public bool TryGetBodyByRegionIndex(int index, out HumanVisionBody body)
        {
            body = Fresh && index >= 0 && index < _slots.Length ? _slots[index] : null;
            return body != null;
        }
        public bool IsUserDetected(int index) => TryGetBodyByRegionIndex(index, out _);
        public ulong GetUserIdByIndex(int index) => TryGetBodyByRegionIndex(index, out var body) ? (ulong)Math.Max(0, body.TrackId) : 0;
        public int GetUserIndexById(ulong id) { if (id == 0) return -1; for (int i = 0; i < _slots.Length; i++) if (GetUserIdByIndex(i) == id) return i; return -1; }
        public bool TryGetJointByRegionIndex(int index, HumanVisionJointType type, out HumanVisionJoint joint)
        {
            joint = default;
            if ((int)type < 0 || (int)type >= 23 || !TryGetBodyByRegionIndex(index, out var body)) return false;
            joint = (int)type < HumanVisionJoint.Count ? body.Joints[(int)type] : body.HandJoints[(int)type - HumanVisionJoint.Count]; return joint.Valid;
        }
        public bool IsJointTracked(ulong userId, HumanVisionJointType joint) => TryGetJointByRegionIndex(GetUserIndexById(userId), joint, out _);
        public Vector2 GetJointPosition2D(ulong userId, HumanVisionJointType joint) =>
            TryGetJointByRegionIndex(GetUserIndexById(userId), joint, out var value) ? new Vector2(value.Normalized.x * GetColorImageWidth(), value.Normalized.y * GetColorImageHeight()) : Vector2.zero;
        // Unit image plane, origin at center, +Y up. This is not metric 3D depth.
        public Vector3 GetJointPosition(ulong userId, HumanVisionJointType joint) =>
            TryGetJointByRegionIndex(GetUserIndexById(userId), joint, out var value) ?
                new Vector3(value.Normalized.x - .5f, .5f - value.Normalized.y, 0) : Vector3.zero;
    }
}
