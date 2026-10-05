# Human Vision Input 0.1.0-preview.2

Independent Camera, Video and H.264/TCP RTSP frame sources and preview. This package
contains no models and has no SDK inference dependency. Unity 2021.3+, Windows x64
and Android ARM64/API 26+ are supported by the bundled runtime. Android RTSP uses
MediaCodec/AHardwareBuffer/Vulkan and waits for complete native/copy retirement
before deferred reopening. Errors remain actionable; no silent CPU fallback.

Install Input first using:

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=upm/com.blazetc.humanvision.input#v0.4.0-preview.4
```

For body skeletons explicitly add the SDK URL:

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=upm/com.blazetc.humanvision#v0.4.0-preview.4
```

Use a clean Git UPM import or the combined SDK/Input unitypackage; do not install
both. For Git UPM, import the InputPreview sample in Package Manager first,
then build preview with HumanVision > Input > Create standalone preview. Choose your own
camera, local video or RTSP URL. No media or user camera address is bundled.
The SDK's official builder creates Camera, Video and RTSP demos from
HumanVision > Create unified demos in dedicated folder. See its UPM_INSTALLATION.md
for saved quality settings, semantic joint queries and evaluation limitations.

FFmpeg notices and native provenance remain in Licenses/FFmpeg-LGPL-2.1.txt and
native-payload.json; see THIRD_PARTY_NOTICES.md. No device FPS certification.
