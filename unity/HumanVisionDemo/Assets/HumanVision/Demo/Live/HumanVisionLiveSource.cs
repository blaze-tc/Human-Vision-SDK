using System;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;
namespace HumanVision {
// Retained scene/API wrapper over independent source components.
[RequireComponent(typeof(VideoPlayerFrameSource))]
public sealed class HumanVisionLiveSource:MonoBehaviour {
    private IHumanVisionFrameSource source;
    private HumanVisionCameraSettings settings;
    private VideoPlayerFrameSource bridge;
    private bool resumeAfterPause;
    public IHumanVisionFrameSource FrameSource=>source;
    public string Status=>source==null?"Stopped":source.State==InputSourceState.Error?source.LastError:source.State.ToString();
    public bool HasRecentFrame=>source!=null&&source.State==InputSourceState.Streaming&&source.TryGetLatestFrame(-1,out var frame)&&
        InputMonotonicClock.NowUs-frame.PublishedTimestampUs<1000000;
    public bool autoRotateScreen=true;
    public bool smoothAndroidPreview=true;
    [Range(320,1920)] public int androidAnalysisWidth=640;
    [Range(240,1080)] public int androidAnalysisHeight=640;
    private void Awake(){bridge=GetComponent<VideoPlayerFrameSource>();}
    public void Open(HumanVisionCameraSettings requested) {
        if(requested==null)throw new ArgumentNullException(nameof(requested));requested.Validate();Close();settings=requested;
        if(Application.platform==RuntimePlatform.Android&&autoRotateScreen){Screen.autorotateToPortrait=true;Screen.autorotateToPortraitUpsideDown=true;Screen.autorotateToLandscapeLeft=true;Screen.autorotateToLandscapeRight=true;Screen.orientation=ScreenOrientation.AutoRotation;}
        if(requested.source==HumanVisionCameraKind.WebCamera){
            var component=GetComponent<WebCameraFrameSource>();if(component==null)component=gameObject.AddComponent<WebCameraFrameSource>();source=component;
            source.Open(new HumanVisionSourceSettings{Kind=InputKind.WebCamera,DeviceName=requested.deviceName,RequestedWidth=requested.width,
                RequestedHeight=requested.height,RequestedFramesPerSecond=requested.framesPerSecond,DisplayMirror=requested.mirror});
        } else {
            var component=GetComponent<RtspFrameSource>();if(component==null)component=gameObject.AddComponent<RtspFrameSource>();source=component;
            source.Open(new RtspSourceSettings{Location=requested.rtspUrl,RequestedWidth=requested.width,RequestedHeight=requested.height,
                RequestedFramesPerSecond=requested.framesPerSecond,DisplayMirror=requested.mirror,Transport=requested.rtspTcp?RtspTransport.Tcp:RtspTransport.Udp});
        }
        bridge.BindUnifiedSource(source);
    }
    public void Close(){if(bridge!=null)bridge.DetachUnifiedSource();if(source!=null)source.Close();source=null;}
    private void OnDisable(){Close();}
    private void OnApplicationPause(bool paused){
        if(paused){resumeAfterPause=source!=null;if(resumeAfterPause)Close();}
        else if(resumeAfterPause&&isActiveAndEnabled){resumeAfterPause=false;Open(settings);}
    }
}
}
