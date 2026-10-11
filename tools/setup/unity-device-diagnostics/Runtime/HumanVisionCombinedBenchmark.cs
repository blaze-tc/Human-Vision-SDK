using System;
using System.Collections;
using System.Collections.Generic;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision.TestProject.Diagnostics
{
    /// <summary>One installation, one sequential comparison. Never writes saved settings.
    /// The existing diagnostics session captures per-person observations and stage timing.
    /// RKNN modes are skipped explicitly on devices without an RK3588 identity.</summary>
    [DisallowMultipleComponent]
    public sealed class HumanVisionCombinedBenchmark : MonoBehaviour
    {
        [SerializeField] private HumanVisionSettingsController controller;
        [SerializeField] private HumanVisionSettingsDeviceDiagnostics diagnostics;
        [SerializeField] private Button runButton;
        [SerializeField,Min(5)] private float warmupSeconds=15;
        [SerializeField,Min(30)] private float sampleSeconds=60;
        [SerializeField,Min(60)] private float sustainedSeconds=300;
        private bool running,cancelled,failed,pixelsPassed;
        private string failure,oldInput,oldResize,oldCopy;
        private HumanVisionSettingsData savedDraft;
        private HumanVisionSdkOptions previousRuntime;
        private CanvasGroup controls;
        private bool oldInteractable,controlsLocked;
        private RtspFrameSource previewSource;
        private FramePreview preview;
        public bool Running=>running;
        public string Status {get;private set;}="";
        public void Configure(HumanVisionSettingsController owner,HumanVisionSettingsDeviceDiagnostics log,Button button)
        {controller=owner;diagnostics=log;runButton=button;}
        private void OnEnable(){if(runButton!=null)runButton.onClick.AddListener(Toggle);}
        private void OnDisable()
        {
            if(runButton!=null)runButton.onClick.RemoveListener(Toggle);
            cancelled=true;RestoreEnvironment();ClosePreview();ReleaseControls();
            // Disabling this component must not leave a live comparison or a locked view.
            StopAllCoroutines();running=false;RefreshButton();
        }
        public void Toggle()
        {
            if(running){cancelled=true;Status="正在安全停止对照…";return;}
            if(controller==null || diagnostics==null || controller.Busy)return;
            StartCoroutine(Execute());
        }
        private IEnumerator Execute()
        {
            running=true;cancelled=failed=false;failure="";
            savedDraft=controller.View.ReadDraft();previousRuntime=controller.Sdk.IsRunning?controller.Sdk.ActiveConfiguration:null;
            oldInput=Environment.GetEnvironmentVariable("HV_RKNN_INPUT_MEMORY_MODE");
            oldResize=Environment.GetEnvironmentVariable("HV_MODEL_READBACK_MODE");
            oldCopy=Environment.GetEnvironmentVariable("HV_SOURCE_COPY_MODE");
            controls=controller.View.GetComponent<CanvasGroup>()??controller.View.gameObject.AddComponent<CanvasGroup>();
            oldInteractable=controls.interactable;controlsLocked=true;controls.interactable=false;
            controller.OperationNotice="自动对照运行中，设置暂时锁定；点击右下「停止对照 / 解锁设置」恢复手动操作";
            RefreshButton();
            // The diagnostic panel ignores this group so log export and cancellation stay usable.
            try {
                yield return Drive(Suite());
                RestoreEnvironment();ClosePreview();
                yield return controller.Sdk.StopSdk();
                controller.Edit(()=>savedDraft);
                if(previousRuntime!=null)yield return controller.Sdk.Initialize(previousRuntime);
                diagnostics.RecordBenchmark("interactive",cancelled?"cancelled":failed?"FAILED "+failure:"comparison completed; physical accuracy/FPS acceptance requires these recorded results");
                Status=cancelled?"对照已取消":failed?"对照失败，查看日志":"对照结束，日志已保存";
            } finally {RestoreEnvironment();ClosePreview();ReleaseControls();running=false;RefreshButton();}
        }
        private void ReleaseControls()
        {
            if(controlsLocked && controls!=null)controls.interactable=oldInteractable;
            controlsLocked=false;
            if(controller!=null)controller.OperationNotice="";
        }
        // Flatten nested iterators so a failed initialization cannot silently terminate
        // the suite before environment/settings restoration. No per-frame task allocation.
        private IEnumerator Drive(IEnumerator first)
        {
            var stack=new Stack<IEnumerator>();stack.Push(first);
            try {
                while(stack.Count>0 && !cancelled) {
                    bool more=false;object current=null;
                    try {more=stack.Peek().MoveNext();if(more)current=stack.Peek().Current;}
                    catch(Exception error){failed=true;failure=error.ToString();break;}
                    if(!more){(stack.Pop() as IDisposable)?.Dispose();continue;}
                    if(current is IEnumerator nested){stack.Push(nested);continue;}
                    yield return current;
                }
            } finally {while(stack.Count>0)(stack.Pop() as IDisposable)?.Dispose();}
        }
        private IEnumerator Suite()
        {
            yield return controller.Sdk.StopSdk();
            pixelsPassed=true;
            yield return ModelReadbackDeviceProbe.Run((detail,passed)=>{pixelsPassed&=passed;diagnostics.RecordBenchmark("gpu-pixel-fixture",(passed?"PASS ":"FAIL ")+detail);});
            var catalogText=Resources.Load<TextAsset>(HumanVisionSettingsBundledVideos.ResourceName);
            var catalog=catalogText!=null?JsonUtility.FromJson<HumanVisionSettingsBundledVideos.Catalog>(catalogText.text):null;
            if(catalog?.videos==null || catalog.videos.Length==0)throw new InvalidOperationException("No bundled comparison video exists.");
            string video=HumanVisionSettingsBundledVideos.ResolvePath(Application.streamingAssetsPath,catalog.videos[0].relativePath);
            diagnostics.RecordBenchmark("suite","video SHA256="+catalog.videos[0].sha256+" warmup="+warmupSeconds+" measure="+sampleSeconds+" sustained="+sustainedSeconds+"; all rates are fresh observations, not rendering");
            bool neural=IsRk3588();
            if(!neural)diagnostics.RecordBenchmark("npu","SKIPPED: connected hardware is not identified as RK3588; CPU/Vulkan do not validate RKNN");
            if(!string.IsNullOrWhiteSpace(savedDraft.Rtsp.RtspUrl))yield return PreviewOnly();
            else diagnostics.RecordBenchmark("rtsp","SKIPPED: no configured RTSP address");
            foreach(var kind in new[]{InputKind.Video,InputKind.Rtsp}) {
                if(kind==InputKind.Rtsp && string.IsNullOrWhiteSpace(savedDraft.Rtsp.RtspUrl))continue;
                if(neural) {
                    yield return Measure(kind,video,HumanVisionAccelerationMode.Neural,true,sampleSeconds);
                    yield return Measure(kind,video,HumanVisionAccelerationMode.Neural,false,sampleSeconds);
                    // A repeated baseline exposes temperature/order drift in the same installation.
                    yield return Measure(kind,video,HumanVisionAccelerationMode.Neural,true,sampleSeconds);
                } else {
                    yield return Measure(kind,video,HumanVisionAccelerationMode.Cpu,false,sampleSeconds);
                    yield return Measure(kind,video,HumanVisionAccelerationMode.Graphics,false,sampleSeconds);
                }
            }
            yield return Measure(InputKind.Video,video,neural?HumanVisionAccelerationMode.Neural:HumanVisionAccelerationMode.Graphics,false,sustainedSeconds);
        }
        private IEnumerator Measure(InputKind kind,string video,HumanVisionAccelerationMode mode,bool baseline,float seconds)
        {
            yield return controller.Sdk.StopSdk();
            SetEnvironment(baseline);
            var settings=savedDraft.Clone();settings.SourceKind=kind;settings.SelectAcceleration(mode);
            settings.InputQuality=ModelInputQuality.Low;settings.Video.VideoPath=video;
            var options=settings.ToOptions();
            string phase=kind+"/"+mode+"/"+(baseline?"baseline":"candidate")+"/"+seconds+"s";
            Status=phase;RefreshButton();controller.Edit(()=>settings);
            diagnostics.RecordBenchmark(phase,"opening; saved preferences remain unchanged");
            yield return controller.Sdk.Initialize(options);
            if(!controller.Sdk.IsRunning) {failed=true;failure=controller.Sdk.LastError;diagnostics.RecordBenchmark(phase,"FAILED open: "+failure);yield break;}
            // Direct SDK initialization intentionally avoids saving preferences;
            // apply the same display style as the regular settings Apply action.
            controller.View.Overlay.ConfigureStyle(settings.Mode.LineWidth,settings.Mode.PointDiameter);
            yield return Wait(warmupSeconds);
            if(mode==HumanVisionAccelerationMode.Neural) {
                bool actualLegacy=controller.Sdk.RuntimeDiagnostics.Contains("input_path=legacy-requested");
                if(actualLegacy!=baseline) {
                    failed=true;failure="Native input mode did not match this comparison phase";
                    diagnostics.RecordBenchmark(phase,"INVALID COMPARISON: "+failure+"; "+controller.Sdk.RuntimeDiagnostics);yield break;
                }
            }
            diagnostics.RecordBenchmark(phase,"MEASURE BEGIN; "+controller.Sdk.RuntimeDiagnostics);
            double start=Time.realtimeSinceStartupAsDouble;
            yield return Wait(seconds);
            diagnostics.RecordBenchmark(phase,"MEASURE END seconds="+(Time.realtimeSinceStartupAsDouble-start)+"; "+controller.Sdk.RuntimeDiagnostics);
        }
        private IEnumerator PreviewOnly()
        {
            yield return controller.Sdk.StopSdk();
            previewSource=gameObject.AddComponent<RtspFrameSource>();
            preview=controller.View.Preview.gameObject.GetComponent<FramePreview>()??controller.View.Preview.gameObject.AddComponent<FramePreview>();
            preview.enabled=true;preview.Bind(previewSource);
            var settings=savedDraft.Rtsp.ToSourceSettings(InputKind.Rtsp);
            if(settings is RtspSourceSettings rtsp)rtsp.Transport=savedDraft.RtspTcp?RtspTransport.Tcp:RtspTransport.Udp;
            previewSource.Open(settings);
            Status="RTSP / 仅预览";RefreshButton();diagnostics.RecordBenchmark("rtsp/preview-only","opening; SDK stopped");
            double deadline=Time.realtimeSinceStartupAsDouble+20;
            while(previewSource.State!=InputSourceState.Streaming && previewSource.State!=InputSourceState.Error && Time.realtimeSinceStartupAsDouble<deadline)yield return null;
            if(previewSource.State!=InputSourceState.Streaming){failed=true;diagnostics.RecordBenchmark("rtsp/preview-only","FAILED "+previewSource.LastError);ClosePreview();yield break;}
            yield return Wait(warmupSeconds);
            double started=Time.realtimeSinceStartupAsDouble;long after=-1,count=0;
            while(Time.realtimeSinceStartupAsDouble-started<sampleSeconds) {
                if(previewSource.TryGetLatestFrame(after,out var frame)){after=frame.FrameId;++count;}
                yield return null;
            }
            diagnostics.RecordBenchmark("rtsp/preview-only","observed publications="+count+" seconds="+(Time.realtimeSinceStartupAsDouble-started)+"; no model running");ClosePreview();
        }
        private IEnumerator Wait(float seconds){double until=Time.realtimeSinceStartupAsDouble+seconds;while(Time.realtimeSinceStartupAsDouble<until)yield return null;}
        private static bool IsRk3588()
        {
            string hardware=SystemInfo.deviceModel+" "+SystemInfo.processorType;
#if UNITY_ANDROID && !UNITY_EDITOR
            try {using(var build=new AndroidJavaClass("android.os.Build"))hardware+=" "+build.GetStatic<string>("HARDWARE")+" "+build.GetStatic<string>("BOARD");}catch(Exception){}
#endif
            return Application.platform==RuntimePlatform.Android && hardware.IndexOf("rk3588",StringComparison.OrdinalIgnoreCase)>=0;
        }
        private void SetEnvironment(bool baseline) {
            Environment.SetEnvironmentVariable("HV_RKNN_INPUT_MEMORY_MODE",baseline?"legacy":"auto");
            Environment.SetEnvironmentVariable("HV_MODEL_READBACK_MODE",baseline||!pixelsPassed?"legacy":"auto");
            Environment.SetEnvironmentVariable("HV_SOURCE_COPY_MODE",baseline?"legacy":"auto");
        }
        private void RestoreEnvironment() {
            if(!running)return;
            Environment.SetEnvironmentVariable("HV_RKNN_INPUT_MEMORY_MODE",oldInput);
            Environment.SetEnvironmentVariable("HV_MODEL_READBACK_MODE",oldResize);
            Environment.SetEnvironmentVariable("HV_SOURCE_COPY_MODE",oldCopy);
        }
        private void ClosePreview() {
            if(preview!=null){preview.Bind(null);preview.enabled=false;preview=null;}
            if(previewSource!=null){previewSource.Close();Destroy(previewSource);previewSource=null;}
        }
        private void RefreshButton(){if(runButton!=null)runButton.GetComponentInChildren<Text>().text=running?"停止对照 / 解锁设置":"一键自动对照";}
    }
}
