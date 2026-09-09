# Human Vision SDK 0.3.0-preview.1

## Git Package Manager

Unity: Window > Package Manager > + > Add package from git URL:

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.3.0-preview.1
```

The repository is private: use an account/SSH key with repository access.
SSH alternative:

```
ssh://git@github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.3.0-preview.1
```

Package includes Windows x64 and Android ARM64 libraries and pinned model files.
On Editor installation and before builds, HumanVisionModelInstaller copies the two
models into Assets/StreamingAssets/HumanVision/Models. An explicit menu is available:
HumanVision > Install Packaged Models. Native libraries remain in the package.
This copy is required because UPM model folders are not APK StreamingAssets.

## Local installation

- Assets import: download HumanVisionSDK-0.3.0-preview.1.unitypackage from Releases,
  then Assets > Import Package > Custom Package.
- UPM offline: download com.blazetc.humanvision-0.3.0-preview.1.tgz, then Package Manager
  > + > Add package from tarball.
- Choose one installation method. Do not install UPM over an existing Assets/HumanVision
  copy or duplicate native plugin DLLs. Back up the project before migrating; remove
  the previous SDK's files through Unity first. Do not remove Azure vendor assets.

## Scenes

HumanVision > Create Live Camera Demo creates a live camera scene plus a separate
settings scene, registers both in Build Settings, and opens the camera scene.
HumanVision > Create Camera Settings Scene opens the settings scene instead.
Use the bottom navigation buttons; settings save under persistentDataPath.
The existing rectangle move/resize behavior is unchanged.

The video RawImage and HumanVisionSkeletonOverlayer are independent. Adjust
lineWidthPixels and jointDiameterPixels, or supply jointPrefab/linePrefab.
Hands: LeftHand/LeftHandtip/LeftThumb and RightHand/RightHandtip/RightThumb.
Palm is derived from actual hand-model landmarks and exposes IsDerived=true;
fingertips/thumbs are model observations. Invalid/old joints are hidden.

## Android preview changes

OnePlus 9 Pro / Snapdragon 888 / Android 14 is the reported slow device.
Live preview now runs separately from inference, with no history texture ring.
Readback is limited to work the native worker can consume; analysis defaults to
640x640 maximum preserving aspect ratio. Android ORT session pools use two threads
and disable spinning. No NNAPI/RKNN acceleration is included in this preview.
HUD separates render FPS from actual inference FPS and source age.
Live skeletons older than 350ms are hidden. MP4 synchronization remains unchanged.

Windows/Android libraries and C# are compiled; phone performance and hand accuracy
require user validation. Eight-person 30 fresh complete skeleton FPS is not certified.
Coordinates are RGB image-plane coordinates, not Kinect metric 3D depth.
Third-party license and model provenance records are included in the package.
