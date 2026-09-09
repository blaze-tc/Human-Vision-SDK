# Human Vision SDK 0.3.0-preview.2

## Git Package Manager

Unity: Window > Package Manager > + > Add package from git URL:

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.3.0-preview.2
```

The repository is private: use an account/SSH key with repository access.
SSH alternative:

```
ssh://git@github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.3.0-preview.2
```

Package includes Windows x64 and Android ARM64 libraries and pinned model files.
On Editor installation and before builds, HumanVisionModelInstaller copies the two
models into Assets/StreamingAssets/HumanVision/Models. An explicit menu is available:
HumanVision > Install Packaged Models. Native libraries remain in the package.
This copy is required because UPM model folders are not APK StreamingAssets.

## Local installation

- Assets import: download HumanVisionSDK-0.3.0-preview.2.unitypackage from Releases,
  then Assets > Import Package > Custom Package.
- UPM offline: download com.blazetc.humanvision-0.3.0-preview.2.tgz, then Package Manager
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
Live skeleton visibility now has a configurable source-age limit (default 3000 ms),
 and the HUD reports actual pose FPS, source age and native/visible body counts.
 This display allowance does not make an old pose current or increase inference FPS.
 The raised-hand example uses its own 1500 ms age limit. MP4 synchronization is unchanged.

Windows/Android libraries and C# are compiled; phone performance and hand accuracy
require user validation. Eight-person 30 fresh complete skeleton FPS is not certified.
Coordinates are RGB image-plane coordinates, not Kinect metric 3D depth.
Third-party license and model provenance records are included in the package.

## 0.3.0-preview.2 手机更新

- 两个场景共用安全区、横竖屏自适应 GUI。手机短边按480个界面单位布局，按钮高50单位；设置面板支持滚动。
- 相机启用自动旋转，修正90/270度纹理采样方向；预览与识别共用校正后的图像。启动时校准GPU回读行顺序。旋转后丢弃旧方向结果。
- 骨骼显示不再受固定350ms门限限制。HUD中的Bodies是原生结果人数，Visible是当前可显示人数，Age是源帧年龄。
- `HumanVisionRaisedHandDetector.cs` 是简单举手示例：regionIndex选择区域，比较有效手腕与肩膀的归一化Y坐标，输出左右手状态；默认忽略超过1500ms的动作结果。
- 现有相机场景的HumanVisionSceneControls会自动挂载举手组件；新建场景也已挂载。可在Inspector调整regionIndex、heightMargin、minimumConfidence和maximumPoseAgeMilliseconds。
- 更新Git依赖到新标签即可；不要同时导入unitypackage。若当前场景经过自行修改并删除了SceneControls，请手动挂载举手组件并指定manager。
- 未执行手机、摄像头、Unity运行测试；仅编译与包内容校验。请实机检查前后摄像头、横竖屏、身体与双手、举手状态，以及关闭Use regions后的全画面识别。
