# Live-camera and independent SDK implementation plan

User authorization: 2026-09-08. Use writing-plans/executing-plans guidance;
the user's no-test instruction overrides the old milestone test gates.

## Design

WebCamTexture and native FFmpeg RTSP decoding feed a shared Unity frame bridge.
RTSP decode/reconnect runs on a native worker, copying only its latest RGBA
frame into reusable Unity buffers. Unity objects stay on the main thread.
Each source normalizes orientation before submission. Preview and skeleton use
the same frame identity; a bounded history prevents accumulating delayed frames.

Native region configuration masks every pixel outside the union before detector
and pose processing. Each non-overlapping normalized rectangle is an indexed
slot; only one detection per slot is posed. Overlap is rejected explicitly to
avoid ambiguous ownership. Empty slots remain empty. Native snapshots include
region index and configuration revision alongside the unchanged legacy ABI.
Configuration is saved under persistentDataPath, never per-frame JSON.

Unity settings expose source/device/RTSP URL, people count, region toggle,
drag/resize rectangles, save/load. KinectManager-inspired getters expose texture,
counts, tracked IDs, region slots and valid image-space joint positions. They
do not imply metric depth or fabricate missing hand joints.

Package independent runtime/demo scripts, models, Windows x64 libraries and
Android ARM64 libraries, import configuration, permissions, setup menu and guide.
Android first uses generic ONNX CPU; RK3588 NPU/30FPS remains a separate target.

## Implementation order

- [x] Native region mask/selection and versioned region snapshot query.
- [x] Shared live texture bridge and WebCamTexture capture/lifecycle.
- [x] FFmpeg RTSP worker: TCP default, interrupt deadline, reconnect, latest frame.
- [x] Region UI, persistence and KinectManager-style game facade.
- [x] Android CMake/ORT dependencies, asset extraction and camera permission.
- [x] Windows/Android native builds; package generation script added.
- [x] Usage instructions and exact untested delivery status; no test execution.

Files: native core/public headers + native/src/input; Unity Runtime/Live and
Editor package helper; tools/setup dependency preparation and tools/package.
Builds are packaging steps, not acceptance tests. Preserve vendor examples and
the current imported demo; new functionality is delivered as a separate package.
