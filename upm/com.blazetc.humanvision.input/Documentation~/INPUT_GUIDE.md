# Standalone input preview

Install `com.blazetc.humanvision.input` without the SDK. Import the **InputPreview**
sample in Package Manager, open its scene and enter a video path, camera device
(empty selects the default) or an RTSP URL. Start/reconnect and Stop remain
available while a camera permission or RTSP connection is pending. H.264 over TCP
is the qualified RTSP path. Request geometry differs from actual texture geometry.
Android requires ARM64, API26 and Vulkan; unsupported hardware reports an error.

The package requires no models, Runtime Host, ONNX Runtime or ncnn. It owns the
locked FFmpeg runtime dependency copies used by its plugin and by the SDK's
legacy RTSP exports. See the native payload manifest for provenance and hashes.

For a combined Git install, add both explicit dependencies to the Unity manifest:

```json
"com.blazetc.humanvision.input": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=upm/com.blazetc.humanvision.input#codex/android-ncnn-vulkan-implementation",
"com.blazetc.humanvision": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=upm/com.blazetc.humanvision#codex/android-ncnn-vulkan-implementation"
```

These URLs require the reviewed branch to be published. For local development use
two `file:` dependencies pointing at each package directory instead. The SDK's
exact input version does not resolve a sibling Git path automatically. For an
input-only project include only the first dependency. No release is implied.

Settings controls use a 1280x720 CanvasScaler, match 0.5, a device safe area and
scrolling content. Hiding controls keeps playback active.
