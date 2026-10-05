# Input third-party notices

The Input package uses dynamically linked FFmpeg 7.1 LGPL 2.1-or-later libraries.
License text: Licenses/FFmpeg-LGPL-2.1.txt. Exact binaries/importers and provenance:
native-payload.json. Official source: https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz .
Binary build provenance: https://github.com/bytedeco/javacpp-presets/tree/1.5.11/ffmpeg .
No FFmpeg JNI library, model or inference engine is included. The native code also
uses platform Android/MediaCodec/AHardwareBuffer/Vulkan and Windows system APIs.
These source notices do not constitute redistribution/commercial clearance.
The repository SDK license and model evaluation restrictions remain unchanged.

Unity native Plugin API: native/input/src/android/ahb_capabilities.cpp uses
IUnityInterfaces/UnityPluginLoad. The pinned Unity2021.3.45f1 copyright and
Companion License notice is included byte-exactly in
Licenses/Unity-Plugin-API-LICENSE.md (SHA-256 dc34d09c3c3a5057244ba4e6cbb871105ec42cd1130617aceeddb9219da922d1).
Source release: https://unity.com/releases/editor/whats-new/2021.3.45f1
License URL: https://unity.com/legal/licenses/unity-companion-license
No JSON or PicoSHA2 source dependency is included in the Input native sources.
