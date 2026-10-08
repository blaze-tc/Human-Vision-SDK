新版单组件总控：Hierarchy → Human Vision → Create SDK。UGUI 设置示例：HumanVision → Create SDK settings demo assets。详见 Documentation/user-guide/UNITY_SDK.md 和 SETTINGS_DEMO.md。

# Unity installation — 0.4.0-preview.6

Requires Unity 2021.3 or later. Install into a clean project, or remove a previous
Assets/HumanVision, Assets/HumanVisionInput and HumanVision native plugin installation
before changing installation methods. Back up project settings and saved presets first.
Choose Git UPM **or** the two offline unitypackages; installing both creates duplicate
scripts, GUIDs and native libraries. No Library cleanup is required.

For Git UPM, add Input first, then SDK, using these two tag-pinned dependencies
in Packages/manifest.json:

```json
"com.blazetc.humanvision.input": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=upm/com.blazetc.humanvision.input#v0.4.0-preview.6",
"com.blazetc.humanvision": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=upm/com.blazetc.humanvision#v0.4.0-preview.6"
```

The SDK's exact Input version is 0.1.0-preview.4. Unity cannot discover a second
Git subdirectory from the SDK's version dependency; explicitly add both URLs.
Input alone provides camera/video/RTSP preview without models or inference.
For offline import into a clean project, import
**HumanVisionInput-0.1.0-preview.4.unitypackage first**, then
**HumanVisionSDK-0.4.0-preview.6.unitypackage**. Wait for Input compilation before
importing SDK. Input alone supports standalone preview; both packages provide
the complete SDK installation. The two release tgz files are the alternative
local UPM archives, also installed Input first, then SDK.

Create the official Camera, Video and RTSP demos with
**HumanVision > Examples > Create Camera, Video and RTSP demos**. The other official
menu **Tools > Human Vision > Legacy Examples > Create demos in Assets Scenes** creates the same
three modes. For Git UPM, import the InputPreview sample in Package Manager first.
Input-only preview uses **HumanVision > Examples > Create standalone Input preview**.
Select a camera, your own local video, or an H.264/TCP RTSP URL. No video or user
camera address is bundled. Computer camera/video RTSP presets use a transient
build-computer address; that computer must run the corresponding publisher.

Windows PC uses the bundled Windows x64 runtime. Android requires ARM64, API 26+
and a compatible Vulkan GPU. Choose the NCNN Vulkan runtime mode for the bundled
YOLO pose qualities: Low 512×288, Medium 640×384 and High 960×576. The selected
Profile/ModelPack controls inference dimensions; camera capture dimensions are
separate. The Android route fails with an actionable error when its requirements
are unavailable and does not silently fall back to CPU. The Editor validates
native/importer/model hashes and stages RuntimeData into StreamingAssets.

Shared settings save people capacity, regions and quality; each input mode saves
its own source, mirror and skeleton style. Apply performs validation before source
retirement. Saved styles are retained; new defaults use line width 4.5 and point
diameter 13.5. TryGetBodyByRegionIndex/TryGetJointByRegionIndex and
body.CanonicalJoints expose SDK semantics via HumanVisionCanonicalJointId; tracking
ID differs from region/display index. These points are image-plane observations,
not depth-sensor metric 3D coordinates. A semantic hand API does not imply observed
Handtip/Thumb: the bundled pose profiles disable genuine hand inference.

This is an evaluation preview. Included YOLO assets retain local_evaluation_only
and distribution_qualified=false; upstream license texts and factual provenance
are in THIRD_PARTY_NOTICES.md and Licenses. Including these texts does not grant
commercial or model redistribution approval. 30 fresh complete skeleton FPS,
continuous motion quality and genuine hand endpoints are not certified here.
