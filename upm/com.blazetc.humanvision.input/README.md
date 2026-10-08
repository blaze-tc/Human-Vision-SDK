# Human Vision Input 0.1.0-preview.2

Independent Camera, Video and H.264/TCP RTSP frame sources and preview. This package
contains no models and has no SDK inference dependency. Unity 2021.3+, Windows x64
and Android ARM64/API 26+ are supported by the bundled runtime. Android RTSP uses
MediaCodec/AHardwareBuffer/Vulkan and waits for complete native/copy retirement
before deferred reopening. Errors remain actionable; no silent CPU fallback.

RTSP requested width/height bound the local published image while preserving the
stream aspect ratio; smaller streams are never upscaled. The Android output-bound
fix is staged in the local RK3588-Fix2 retest build (published preview.6/Fix1 still
use the decoded dimensions). The existing GPU color converter produces the scaled
output directly, without CPU image readback. This does not reconfigure the camera
or reduce its encoded-stream/decode workload. Choose a camera substream separately
when those costs also need to be reduced. New-generation diagnostics distinguish
decoded geometry, requested maximum and published output.

Install Input first using:

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=upm/com.blazetc.humanvision.input#v0.4.0-preview.4
```

For body skeletons explicitly add the SDK URL:

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=upm/com.blazetc.humanvision#v0.4.0-preview.4
```

For offline preview, import HumanVisionInput-0.1.0-preview.2.unitypackage into a
clean project. Wait for compilation, then import
HumanVisionSDK-0.4.0-preview.4.unitypackage if you need body skeletons.
Choose Git UPM or the offline unitypackages; do not install both.
For Git UPM, import the InputPreview sample in Package Manager first,
then build preview with HumanVision > Input > Create standalone preview. Choose your own
camera, local video or RTSP URL. No media or user camera address is bundled.
The SDK's official builder creates Camera, Video and RTSP demos from
HumanVision > Create unified demos in dedicated folder. See its UPM_INSTALLATION.md
for saved quality settings, semantic joint queries and evaluation limitations.

FFmpeg notices and native provenance remain in Licenses/FFmpeg-LGPL-2.1.txt and
native-payload.json; see THIRD_PARTY_NOTICES.md. No device FPS certification.
