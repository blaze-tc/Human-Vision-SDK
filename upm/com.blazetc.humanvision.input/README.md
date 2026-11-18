# Human Vision Input 0.1.0-preview.9

Independent Camera, Video and H.264/TCP RTSP frame sources and preview. This package
contains no models and has no SDK inference dependency. Unity 2021.3+, Windows x64
and Android ARM64/API 26+ are supported by the bundled runtime. Android RTSP uses
MediaCodec/AHardwareBuffer/Vulkan and waits for complete native/copy retirement
before deferred reopening. Errors remain actionable; no silent CPU fallback.

RTSP requested width/height bound the local published image while preserving the
stream aspect ratio; smaller streams are never upscaled. The GPU color converter produces the scaled
output directly, without CPU image readback. This does not reconfigure the camera
or reduce its encoded-stream/decode workload. The optional `RtspStreamSelector`
probes supported Uniview channel paths for H.264 stream sizes before opening the
selected input. Diagnostics distinguish decoded geometry, requested maximum and
published output.

The Settings Demo labels RTSP resolution as an output image size limit. The Android
Vulkan bridge supports an optional separate `512×288` analysis target for the SDK.
Camera mode requests a capture size, while local video plays at its source size.
The converter keeps the latest pending image and protects Unity texture leases until outstanding copies complete.
Per-second native decode/GPU cadence logs accompany Unity publication counters;
changing this scheduling does not guarantee a camera or device frame rate.

Install Input first using:

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.16
```

For body skeletons explicitly add the SDK URL:

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.16
```

For offline preview, import HumanVisionInput-0.1.0-preview.9.unitypackage into a
clean project. Wait for compilation, then import
HumanVisionSDK-0.4.0-preview.16.unitypackage if you need body skeletons.
Choose Git UPM or the offline unitypackages; do not install both.
For Git UPM, import the InputPreview sample in Package Manager first,
then build preview with HumanVision > Input > Create standalone preview. Choose your own
camera, local video or RTSP URL. No media or user camera address is bundled.
The SDK's official builder creates Camera, Video and RTSP demos from
HumanVision > Create unified demos in dedicated folder. See its UPM_INSTALLATION.md
for saved quality settings, semantic joint queries and evaluation limitations.

FFmpeg notices and native provenance remain in Licenses/FFmpeg-LGPL-2.1.txt and
native-payload.json; see THIRD_PARTY_NOTICES.md. No device FPS certification.
