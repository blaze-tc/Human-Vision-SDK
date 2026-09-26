using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Networking;

namespace HumanVision.Demo
{
    // Present only in a development gate player. The camera image goes through
    // the production bridge and B5 cached ncnn import/conversion; no body is emitted.
    public sealed class HumanVisionAndroidGpuGate : MonoBehaviour
    {
        public TextAsset inputContract;
        public bool preparedDetectorGate;
        private static Texture2D CreatePreparedFixture()
        {
            var fixture = new Texture2D(320, 320, TextureFormat.RGBA32, false, true);
            fixture.filterMode = FilterMode.Point;
            fixture.wrapMode = TextureWrapMode.Clamp;
            return fixture;
        }
#if HUMANVISION_GPU_GATE && UNITY_ANDROID && !UNITY_EDITOR
        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        private struct Submission
        {
            public uint size, version;
            public IntPtr texture;
            public int width, height;
            public long frameId, timestampUs;
            public uint rotation, mirrored;
        }
        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        private unsafe struct Status
        {
            public uint size, version, path, format;
            public ulong usage, features, submitted, imported, noSlot, generationDrops;
            public fixed byte unityDevice[16], ncnnDevice[16], unityDriver[16], ncnnDriver[16];
        }
        [DllImport("humanvision")] private static extern int HV_AndroidGpuGateBegin(IntPtr texture, string contract);
        [DllImport("humanvision")] private static extern void HV_AndroidGpuGateEnd();
        [DllImport("humanvision")] private static extern int HV_AndroidGpuGateSubmit(ref Submission submission, out IntPtr eventData, out int eventId);
        [DllImport("humanvision")] private static extern void HV_AndroidGpuGateStatus(ref Status status, out ulong converted, StringBuilder error, uint capacity);
        [DllImport("humanvision")] private static extern uint HV_AndroidGpuGateProbe(StringBuilder probe, uint capacity);
        [DllImport("humanvision")] private static extern uint HV_AndroidGpuGateRestartReady();
        [DllImport("humanvision")] private static extern uint HV_AndroidGpuGateRestartVerified();
        [DllImport("humanvision")] private static extern uint HV_AndroidGpuGateMustRetainSource();
        [DllImport("humanvision")] private static extern IntPtr HV_GetAndroidGpuRenderEventAndDataFunction();
        private WebCamTexture _camera;
        private RenderTexture _source;
        private Texture2D _preparedFixture;
        private string _preparedManifest;
        private string _preparedModelHashes;
        private int _preparedPasses;
        private CommandBuffer _commands;
        private IntPtr _renderEvent;
        private long _frameId;
        private ulong _sourceGeneration;
        private float _nextLog;
        private bool _begun;
        private int _rotation = -1;
        private bool _mirror;
        private string _pendingRecovery;
        private string _lastStatus = "Starting camera";
        private string _lastProbe;
        private bool _terminalGpuFault;
        private static readonly List<Texture> RetainedTerminalTextures = new List<Texture>();
        private static readonly List<CommandBuffer> RetainedTerminalCommands = new List<CommandBuffer>();
        private void Start()
        {
            if (HV_AndroidGpuGateMustRetainSource() != 0)
            {
                _terminalGpuFault = true;
                _lastStatus = "Terminal GPU ownership unknown; restart app before using camera";
                Debug.LogError("HV_GPU_GATE " + _lastStatus);
                return;
            }
            if (inputContract == null) throw new InvalidOperationException("Gate input contract is missing");
            if (SystemInfo.graphicsDeviceType != GraphicsDeviceType.Vulkan) throw new InvalidOperationException("Gate requires Vulkan");
            _renderEvent = HV_GetAndroidGpuRenderEventAndDataFunction();
            if (_renderEvent == IntPtr.Zero) throw new InvalidOperationException("Gate render event unavailable");
            _commands = new CommandBuffer { name = "HumanVision GPU gate" };
            if (preparedDetectorGate) { StartCoroutine(StartPrepared()); return; }
            _camera = new WebCamTexture();
            _camera.Play();
            Debug.Log("HV_GPU_GATE camera started gpu=" + SystemInfo.graphicsDeviceName +
                " driver=" + SystemInfo.graphicsDeviceVersion + " no skeleton is expected");
        }
        private void Update()
        {
            if (_terminalGpuFault) return;
            if (HV_AndroidGpuGateMustRetainSource() != 0) { EndSource(); return; }
            if (preparedDetectorGate) { UpdatePrepared(); return; }
            if (_camera == null || !_camera.didUpdateThisFrame || _camera.width < 32 || _camera.height < 32) return;
            int rotation = ((_camera.videoRotationAngle % 360) + 360) % 360;
            bool mirror = _camera.videoVerticallyMirrored;
            int width = rotation % 180 == 0 ? _camera.width : _camera.height;
            int height = rotation % 180 == 0 ? _camera.height : _camera.width;
            if (_source == null || _source.width != width || _source.height != height ||
                _rotation != rotation || _mirror != mirror)
            {
                ++_sourceGeneration;
                Debug.Log("HV_GPU_GATE source rebuilding generation=" + _sourceGeneration +
                    " width=" + width + " height=" + height +
                    " rotation=" + rotation + " mirror=" + mirror);
                EndSource();
                if (_terminalGpuFault) return;
                _rotation = rotation;
                _mirror = mirror;
                _source = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32);
                _source.Create();
                int beginResult = HV_AndroidGpuGateBegin(_source.GetNativeTexturePtr(), inputContract.text);
                if (beginResult != 0) FailGate("source lease", beginResult);
                _begun = true;
                if (_pendingRecovery != null)
                {
                    Debug.Log("HV_GPU_GATE source resumed after=" + _pendingRecovery);
                    _pendingRecovery = null;
                }
            }
            Graphics.Blit(_camera, _source);
            var submission = new Submission {
                size = 48, version = 1, texture = _source.GetNativeTexturePtr(),
                width = width, height = height, frameId = ++_frameId,
                timestampUs = (long)(Time.realtimeSinceStartupAsDouble * 1000000.0),
                rotation = (uint)rotation, mirrored = mirror ? 1u : 0u
            };
            int result = HV_AndroidGpuGateSubmit(ref submission, out IntPtr data, out int eventId);
            if (data != IntPtr.Zero)
            {
                _commands.Clear();
                _commands.IssuePluginEventAndData(_renderEvent, eventId, data);
                Graphics.ExecuteCommandBuffer(_commands);
            }
            if (result != 0 && result != 1) FailGate("submit", result);
            if (Time.realtimeSinceStartup >= _nextLog)
            {
                _nextLog = Time.realtimeSinceStartup + 0.25f;
                var status = new Status { size = 128, version = 1 };
                var error = new StringBuilder(2048);
                HV_AndroidGpuGateStatus(ref status, out ulong converted, error, 2048);
                LogProbe();
                string statusError = error.Length == 0 ? "<none>" :
                    error.ToString().Replace("\\", "\\\\").Replace("\r", "\\r").Replace("\n", "\\n");
                unsafe
                {
                    byte* ud = status.unityDevice;
                    byte* nd = status.ncnnDevice;
                    byte* ur = status.unityDriver;
                    byte* nr = status.ncnnDriver;
                        _lastStatus = "HV_GPU_GATE frame=" + _frameId + " generation=" + _sourceGeneration + " result=" + result +
                    " orientation=" + Screen.orientation + " rotation=" + rotation + " mirror=" + mirror +
                    " path=" + status.path + " ahbFormat=" + status.format +
                    " ahbUsage=0x" + status.usage.ToString("X") +
                    " formatFeatures=0x" + status.features.ToString("X") +
                    " submitted=" + status.submitted + " imported=" + status.imported +
                    " converted=" + converted + " noSlot=" + status.noSlot +
                    " generationDrops=" + status.generationDrops +
                    " unityDeviceUUID=" + Hex(ud) + " ncnnDeviceUUID=" + Hex(nd) +
                    " unityDriverUUID=" + Hex(ur) + " ncnnDriverUUID=" + Hex(nr) +
                    " error=" + statusError;
                }
                Debug.Log(_lastStatus);
            }
        }
        private IEnumerator StartPrepared()
        {
            if (_terminalGpuFault || HV_AndroidGpuGateMustRetainSource() != 0)
            {
                EndSource(); yield break;
            }
            string root = Path.Combine(Application.persistentDataPath, "humanvision-prepared-gate");
            string detector = Path.Combine(root, "detector");
            Directory.CreateDirectory(detector);
            var modelHashes = new StringBuilder();
            foreach (string name in new[] { "model.param", "model.bin" })
            {
                string uri = Application.streamingAssetsPath + "/HumanVisionPreparedGate/detector/" + name;
                using (var request = UnityWebRequest.Get(uri))
                {
                    yield return request.SendWebRequest();
                    if (_terminalGpuFault || HV_AndroidGpuGateMustRetainSource() != 0)
                    {
                        EndSource(); yield break;
                    }
                    if (request.result != UnityWebRequest.Result.Success)
                        throw new InvalidOperationException("Prepared gate model extraction failed: " + request.error);
                    byte[] modelBytes = request.downloadHandler.data;
                    File.WriteAllBytes(Path.Combine(detector, name), modelBytes);
                    modelHashes.Append(name).Append("=").Append(Sha256(modelBytes)).Append(" ");
                }
            }
            _preparedModelHashes = modelHashes.ToString();
            _preparedManifest = inputContract.text.Replace("__ASSET_ROOT__", root.Replace('\\', '/'));
            _preparedFixture = CreatePreparedFixture();
            var pixels = new Color32[320 * 320];
            var colors = new[] { new Color32(128, 64, 32, 255),
                new Color32(48, 160, 210, 255), new Color32(220, 118, 44, 255),
                new Color32(22, 35, 235, 255) };
            for (int y = 0; y < 320; ++y)
                for (int x = 0; x < 320; ++x)
                    pixels[y * 320 + x] = colors[(y >= 160 ? 2 : 0) + (x >= 160 ? 1 : 0)];
            var fixtureBytes = new byte[pixels.Length * 4];
            for (int i = 0; i < pixels.Length; ++i)
            {
                fixtureBytes[i * 4] = pixels[i].r;
                fixtureBytes[i * 4 + 1] = pixels[i].g;
                fixtureBytes[i * 4 + 2] = pixels[i].b;
                fixtureBytes[i * 4 + 3] = pixels[i].a;
            }
            string fixtureHash = Sha256(fixtureBytes);
            if (fixtureHash != "3e1edfd04bd3abd1b260cc67b07b781e365a85559365032185cd8ec72545ce74")
                throw new InvalidOperationException("Prepared gate pinned fixture bytes changed: " + fixtureHash);
            Debug.Log("HV_PREPARED_GATE pinned_fixture_rgba_sha256=" + fixtureHash +
                " manifest_sha256=" + Sha256(Encoding.UTF8.GetBytes(_preparedManifest)) +
                " " + _preparedModelHashes);
            _preparedFixture.SetPixels32(pixels);
            _preparedFixture.Apply(false, true);
            if (_terminalGpuFault || HV_AndroidGpuGateMustRetainSource() != 0)
            {
                EndSource(); yield break;
            }
            CreatePreparedSource();
        }
        private static string Sha256(byte[] bytes)
        {
            using (var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
        }
        private void CreatePreparedSource()
        {
            if (_terminalGpuFault || HV_AndroidGpuGateMustRetainSource() != 0)
            {
                EndSource(); return;
            }
            ++_sourceGeneration;
            _source = new RenderTexture(320, 320, 0, RenderTextureFormat.ARGB32,
                RenderTextureReadWrite.Linear);
            _source.Create();
            string manifest = _preparedManifest;
            if (_sourceGeneration == 2)
            {
                manifest = manifest.TrimEnd();
                if (!manifest.EndsWith("}", StringComparison.Ordinal))
                    throw new InvalidOperationException("Prepared gate manifest root is invalid");
                manifest = manifest.Substring(0, manifest.Length - 1) +
                    ",\"prepared_gate_restart_during_job\":true}";
            }
            int result = HV_AndroidGpuGateBegin(_source.GetNativeTexturePtr(), manifest);
            if (result != 0) FailGate("prepared source lease", result);
            _begun = true;
            Debug.Log("HV_PREPARED_GATE source generation=" + _sourceGeneration +
                " fixture=320x320 four-quadrant RGBA32 model=detector");
        }
        private void UpdatePrepared()
        {
            if (_terminalGpuFault) return;
            if (HV_AndroidGpuGateMustRetainSource() != 0) { EndSource(); return; }
            if (!_begun || _preparedFixture == null || _preparedPasses >= 3) return;
            Graphics.Blit(_preparedFixture, _source);
            var submission = new Submission {
                size = 48, version = 1, texture = _source.GetNativeTexturePtr(),
                width = 320, height = 320, frameId = ++_frameId,
                timestampUs = (long)(Time.realtimeSinceStartupAsDouble * 1000000.0),
                rotation = 0, mirrored = 0
            };
            int result = HV_AndroidGpuGateSubmit(ref submission, out IntPtr data, out int eventId);
            if (data != IntPtr.Zero)
            {
                _commands.Clear();
                _commands.IssuePluginEventAndData(_renderEvent, eventId, data);
                Graphics.ExecuteCommandBuffer(_commands);
            }
            if (result != 0 && result != 1) FailGate("prepared submit", result);
            if (Time.realtimeSinceStartup < _nextLog) return;
            _nextLog = Time.realtimeSinceStartup + 0.25f;
            var status = new Status { size = 128, version = 1 };
            var error = new StringBuilder(2048);
            HV_AndroidGpuGateStatus(ref status, out ulong converted, error, 2048);
            LogProbe();
            if (error.Length != 0) FailGate("prepared worker: " + error, -1);
            if (_sourceGeneration == 2)
            {
                if (HV_AndroidGpuGateRestartReady() == 0) return;
                // Development gate only: request restart while the prepared
                // token is outstanding; End drains the job before rebuilding.
                EndSource();
                if (_terminalGpuFault) return;
                if (HV_AndroidGpuGateRestartVerified() == 0)
                    FailGate("prepared job drain across source restart", -1);
                Debug.Log("HV_PREPARED_GATE restart requested with outstanding token; " +
                    "source retired before detector run; drain verified");
                ++_preparedPasses;
                CreatePreparedSource();
                return;
            }
            if (converted == 0) return;
            Debug.Log("HV_PREPARED_GATE PASS generation=" + _sourceGeneration +
                " source submitted=" + status.submitted + " imported=" + status.imported +
                " actualAHBFormat=" + status.format + " actualAHBUsage=0x" +
                status.usage.ToString("X") + " formatFeatures=0x" + status.features.ToString("X") +
                " path=" + status.path + " noSlot=" + status.noSlot);
            ++_preparedPasses;
            EndSource();
            if (_terminalGpuFault) return;
            if (_preparedPasses < 3) CreatePreparedSource();
            else _lastStatus = "Prepared detector parity twice and pending-job restart PASS";
        }
        private static unsafe string Hex(byte* bytes)
        {
            var text = new StringBuilder(32);
            for (int i = 0; i < 16; ++i) text.Append(bytes[i].ToString("x2"));
            return text.ToString();
        }
        private void LogProbe()
        {
            uint required = HV_AndroidGpuGateProbe(null, 0);
            if (required <= 1 || required > 65536) return;
            var probe = new StringBuilder((int)required);
            if (HV_AndroidGpuGateProbe(probe, required) > required) return;
            string measured = probe.ToString();
            if (measured == _lastProbe || !measured.Contains("candidate=")) return;
            _lastProbe = measured;
            foreach (string line in measured.Split('\n'))
            {
                string entry = line.TrimEnd('\r');
                if (entry.Length != 0) Debug.Log("HV_GPU_GATE probe generation=" + _sourceGeneration + " " + entry);
            }
        }
        private void FailGate(string operation, int result)
        {
            var status = new Status { size = 128, version = 1 };
            var error = new StringBuilder(2048);
            HV_AndroidGpuGateStatus(ref status, out ulong converted, error, 2048);
            LogProbe();
            string diagnostic = error.Length == 0 ? "<none>" :
                error.ToString().Replace("\\", "\\\\").Replace("\r", "\\r").Replace("\n", "\\n");
            enabled = false;
            _lastStatus = "Gate " + operation + " failed result=" + result +
                " path=" + status.path + " submitted=" + status.submitted +
                " imported=" + status.imported + " converted=" + converted +
                " diagnostic=" + diagnostic;
            Debug.LogError("HV_GPU_GATE " + _lastStatus);
            EndSource();
            throw new InvalidOperationException(_lastStatus);
        }
        private void OnGUI()
        {
            GUI.Label(new Rect(20, 20, Screen.width - 40, 90), _lastStatus);
            if (!_terminalGpuFault && HV_AndroidGpuGateMustRetainSource() == 0 &&
                GUI.Button(new Rect(20, 115, 260, 80), "Restart camera"))
            {
                EndSource();
                if (_camera != null) { _camera.Stop(); _camera.Play(); }
                _pendingRecovery = "restart";
                Debug.Log("HV_GPU_GATE camera restart requested");
            }
        }
        private void OnApplicationPause(bool pause)
        {
            if (_terminalGpuFault) return;
            if (HV_AndroidGpuGateMustRetainSource() != 0) { EndSource(); return; }
            Debug.Log("HV_GPU_GATE pause=" + pause);
            if (pause) EndSource();
            else
            {
                _pendingRecovery = "pause";
                if (_camera != null && !_camera.isPlaying) _camera.Play();
            }
        }
        private void OnApplicationFocus(bool focus)
        {
            Debug.Log("HV_GPU_GATE focus=" + focus);
        }
        private void EndSource()
        {
            // Query both sides of the worker join: a fault can occur while
            // draining an outstanding prepared detector job. This diagnostic
            // player must retain Unity-owned source textures until process
            // exit when completion is unknown; no same-process restart.
            bool retainSource = HV_AndroidGpuGateMustRetainSource() != 0;
            if (_begun) { HV_AndroidGpuGateEnd(); _begun = false; }
            retainSource |= HV_AndroidGpuGateMustRetainSource() != 0;
            if (retainSource)
            {
                _terminalGpuFault = true;
                RetainTerminalTexture(_source);
                RetainTerminalTexture(_preparedFixture);
                RetainTerminalTexture(_camera);
                _source = null;
                _lastStatus = "Terminal GPU ownership unknown; restart app before using camera";
                Debug.LogError("HV_GPU_GATE " + _lastStatus);
            }
            else if (_source != null)
            {
                if (RenderTexture.active == _source) RenderTexture.active = null;
                _source.Release();
                Destroy(_source);
                _source = null;
            }
            _lastProbe = null;
        }
        private static void RetainTerminalTexture(Texture texture)
        {
            if (texture == null || RetainedTerminalTextures.Contains(texture)) return;
            texture.hideFlags |= HideFlags.DontUnloadUnusedAsset;
            RetainedTerminalTextures.Add(texture);
        }
        private void OnDestroy()
        {
            EndSource();
            if (!_terminalGpuFault)
            {
                if (_preparedFixture != null) Destroy(_preparedFixture);
                if (_camera != null) { _camera.Stop(); Destroy(_camera); }
            }
            if (_commands != null)
            {
                if (_terminalGpuFault) RetainedTerminalCommands.Add(_commands);
                else _commands.Release();
            }
        }
#endif
    }
}
