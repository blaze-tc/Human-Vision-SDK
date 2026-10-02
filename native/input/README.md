# Independent input native plugin

Consumes compressed H.264 over RTSP TCP. Produces Windows CPU RGBA, or Android
PRIVATE decoded AHB leases for GPU conversion. It has no SDK, Runtime Host,
model, ORT, ncnn or SDK clock dependency. The V1 SDK ABI is separate and stable.

`src/android/mediacodec_source.cpp` selects the actual hardware AVC codec and
queries decoder crop/color metadata; `ahb_capabilities.cpp` qualifies the actual
AHB and successfully created Unity logical device. `unity_input_vulkan.cpp`
accesses Unity textures through AccessTexture and submits only within serialized
AccessQueue callbacks. `ahb_image_cache.cpp` caches by AHB identity, generation
and import contract, retiring removed imports only after real GPU completion.
YCbCr conversion uses queried AHB components/offsets and reliable decoder or H.264
SPS/VUI matrix/range; vendor metadata retains its own domain. The bounded SPS
parser reads compressed syntax only. Unknown transfer/primaries remain unknown.
Startup waits boundedly for an actual keyframe before submitting compressed data
to MediaCodec. Imported images are sampled
read-only. No decoded plane mapping, CPU conversion or production readback exists.

Android Task6 exposes a bounded color diagnostic, not a production frame ring or
RTSP texture publication. Task7 owns the full ring/session and reconnect protocol.
The diagnostic's fixed lease and GPU objects are reused; target recreation occurs
only when changing output dimensions. Diagnostic ReadPixels is confined to the
device probe and is counted separately from the production readback counter.
The input-only diagnostic displays the native-written target after GPU completion;
its terminal readback is labeled STATIC TEST SNAPSHOT / COMPLETE.

Build/test: `pwsh -File tools/package/build_input_native.ps1 -Platform Android
-ApiLevel 26 -RunTests`. Host cache/color tests execute Windows PE targets under
`out/input-native/windows`. The shader builder requires the pinned NDK23.1.7779620
compiler hash. Device proof: `tools/test/collect_android_input_gate.ps1 -Gate Color`;
matrix/range/crop options identify encoded source fixtures and actual pixel checks.
Missing formats/features reject explicitly. A request or entry point is not proof
that Unity enabled a capability; actual device capture and GPU gate establish it.
