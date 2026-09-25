using System;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVision.Demo
{
    // Present only in a development gate player. The camera image goes through
    // the production bridge and B5 cached ncnn import/conversion; no body is emitted.
    public sealed class HumanVisionAndroidGpuGate : MonoBehaviour
    {
        public TextAsset inputContract;
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
        [DllImport("humanvision")] private static extern IntPtr HV_GetAndroidGpuRenderEventAndDataFunction();
        private WebCamTexture _camera;
        private RenderTexture _source;
        private CommandBuffer _commands;
        private IntPtr _renderEvent;
        private long _frameId;
        private float _nextLog;
        private bool _begun;
        private int _rotation = -1;
        private bool _mirror;
        private string _pendingRecovery;
        private string _lastStatus = "Starting camera";
        private void Start()
        {
            if (inputContract == null) throw new InvalidOperationException("Gate input contract is missing");
            if (SystemInfo.graphicsDeviceType != GraphicsDeviceType.Vulkan) throw new InvalidOperationException("Gate requires Vulkan");
            _renderEvent = HV_GetAndroidGpuRenderEventAndDataFunction();
            if (_renderEvent == IntPtr.Zero) throw new InvalidOperationException("Gate render event unavailable");
            _commands = new CommandBuffer { name = "HumanVision GPU gate" };
            _camera = new WebCamTexture();
            _camera.Play();
            Debug.Log("HV_GPU_GATE camera started gpu=" + SystemInfo.graphicsDeviceName +
                " driver=" + SystemInfo.graphicsDeviceVersion + " no skeleton is expected");
        }
        private void Update()
        {
            if (_camera == null || !_camera.didUpdateThisFrame || _camera.width < 32 || _camera.height < 32) return;
            int rotation = ((_camera.videoRotationAngle % 360) + 360) % 360;
            bool mirror = _camera.videoVerticallyMirrored;
            int width = rotation % 180 == 0 ? _camera.width : _camera.height;
            int height = rotation % 180 == 0 ? _camera.height : _camera.width;
            if (_source == null || _source.width != width || _source.height != height ||
                _rotation != rotation || _mirror != mirror)
            {
                EndSource();
                _rotation = rotation;
                _mirror = mirror;
                _source = new RenderTexture(width, height, 0, RenderTextureFormat.ARGB32);
                _source.Create();
                if (HV_AndroidGpuGateBegin(_source.GetNativeTexturePtr(), inputContract.text) != 0)
                    throw new InvalidOperationException("Gate source lease failed");
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
            if (result != 0 && result != 1) throw new InvalidOperationException("Gate submit failed: " + result);
            if (Time.realtimeSinceStartup >= _nextLog)
            {
                _nextLog = Time.realtimeSinceStartup + 0.25f;
                var status = new Status { size = 128, version = 1 };
                var error = new StringBuilder(2048);
                HV_AndroidGpuGateStatus(ref status, out ulong converted, error, 2048);
                string statusError = error.Length == 0 ? "<none>" :
                    error.ToString().Replace("\\", "\\\\").Replace("\r", "\\r").Replace("\n", "\\n");
                unsafe
                {
                    byte* ud = status.unityDevice;
                    byte* nd = status.ncnnDevice;
                    byte* ur = status.unityDriver;
                    byte* nr = status.ncnnDriver;
                        _lastStatus = "HV_GPU_GATE frame=" + _frameId + " result=" + result +
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
        private static unsafe string Hex(byte* bytes)
        {
            var text = new StringBuilder(32);
            for (int i = 0; i < 16; ++i) text.Append(bytes[i].ToString("x2"));
            return text.ToString();
        }
        private void OnGUI()
        {
            GUI.Label(new Rect(20, 20, Screen.width - 40, 90), _lastStatus);
            if (GUI.Button(new Rect(20, 115, 260, 80), "Restart camera"))
            {
                EndSource();
                if (_camera != null) { _camera.Stop(); _camera.Play(); }
                _pendingRecovery = "restart";
                Debug.Log("HV_GPU_GATE camera restart requested");
            }
        }
        private void OnApplicationPause(bool pause)
        {
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
            if (_begun) { HV_AndroidGpuGateEnd(); _begun = false; }
            if (_source != null) { _source.Release(); Destroy(_source); _source = null; }
        }
        private void OnDestroy()
        {
            EndSource();
            if (_camera != null) { _camera.Stop(); Destroy(_camera); }
            if (_commands != null) _commands.Release();
        }
#endif
    }
}
