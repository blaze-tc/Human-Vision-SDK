# Human Vision SDK 0.4.0-preview.4 安装与接入

支持 Windows x64 Editor/Player 和 Android ARM64，Unity 2021.3 或更新版本。
包含独立 Input、SDK、原生依赖、模型和 Camera/Video/RTSP 三个 Demo。
不需要 AzureKinectExamples；测试视频不随包分发。

## Git 安装

Window > Package Manager > + > Add package from git URL，先 Input、后 SDK：

```text
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.4
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.4
```

也可以在 `Packages/manifest.json` 的 `dependencies` 同时添加这两条地址。
SDK 的版本依赖不会自动查找同仓库另一个 Git 子目录，因此两包都需要配置。
版本标签固定本次内容；`#main` 会随后续开发更新。

## 本地安装

[Release 下载](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.4)：

- `HumanVisionSDK-Input-0.4.0-preview.4.unitypackage`：Assets > Import Package > Custom Package，一次导入 SDK 和 Input。
- 两个 `.tgz`：Package Manager > Add package from tarball，先 Input、后 SDK。
- `HumanVisionSDK-Input-0.4.0-preview.4.zip`：两个 UPM tarball、导入包、安装说明和校验信息。

Git、tarball、Assets 导入选择一种，更换方式前移除原 SDK/Input，避免重复脚本、
GUID、原生库。保留游戏和厂商资源，不手工修改 `Library/PackageCache`。
安装器按哈希将模型复制到 `Assets/StreamingAssets/HumanVision/Runtime`，使用独立
GUID；菜单 `HumanVision > Install Packaged Models` 可以重新安装运行数据。

## Demo 与设置

执行 `HumanVision > Create unified demos in dedicated folder`，在
`Assets/HumanVisionUnifiedDemo` 生成三个场景并加入 Build Settings；公共按钮切换。
也可以导入 Package Manager 的 `UnifiedInput` sample。

Camera 选择设备；Video 填写自己的视频路径；RTSP 填写 H.264/TCP 地址。
各模式独立保存采集尺寸、镜像和骨骼样式。RTSP 快捷按钮使用打包电脑的 LAN 地址，
默认端口554、路径 `/videodevice` 和 `/video-1.mp4`；仍需启动自己的推流服务，
手机和电脑在同一局域网，也可修改电脑地址。

公共 Settings 设置人数、编号区域和模型质量：低512×288、中640×384、高960×576。
采集尺寸和模型尺寸独立。选择质量后 Apply，Save all settings 保存。
区域使用推理后 bbox/pelvis assignment，框外结果不对外提供，不执行整图 CPU mask。

Android：Project Settings > Human Vision > Android Runtime 选择 **NCNN Vulkan**；
Player Settings 使用 ARM64、最低 API26、Vulkan Graphics API。
所需 native/profile/model/GPU bridge 或设备能力不满足时明确报错，不自动回退 ORT。
ORT CPU/XNNPACK 的配置接口保留，但当前验收的随包路线是 NCNN 三档与 Windows PC，
兼容模式的独立 ModelPack/Profile 不属于本次验收。

## 游戏接入和本次范围

参考 `HumanVisionInputAdapter` 将独立 Input 绑定到 `HumanVisionManager`。
Input 可以只绑定预览而不创建推理。读取 `Bodies` 和 `ResultUpdated`；区域查询参考
`HumanVisionCameraManager.TryGetBodyByRegionIndex` / `TryGetJointByRegionIndex`。
公开游戏接口不需要引用具体模型类型。

用户已确认当前演示效果，可进入项目接入。**Android 30 个完整新鲜观察帧/秒尚未认证**；
Render/Preview/预测帧不能算新骨骼帧。当前默认人体模型输出 COCO-17 身体关键点；
Canonical API 保留手掌/指尖/拇指字段，但随包 Profile 未启用真实手部识别。
图像平面坐标不包含 Kinect 深度传感器的真实三维深度。
模型保留评估标记与来源记录；具体条款见包内 `Licenses` 和 ModelPack，
不表示已取得商业模型授权。维护入口：`docs/maintenance/START_HERE.md`。

---

## 以下为旧版本安装历史，不适用于 preview.4

# Human Vision SDK 0.4.0-preview.3

Current installation/runtime instructions: [0.4 user guide](SDK_040_USER_GUIDE.md).
Use the Git URL below or the matching 0.4.0-preview.3 unitypackage/tgz release asset.
The installer now copies indexed RuntimeData into
Assets/StreamingAssets/HumanVision/Runtime, with independent metadata GUIDs.

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.3
```

The notes below describe older 0.3 releases and are retained as migration history.

Preview.3 adds explicit CPU, XNNPACK and NNAPI no-hands benchmark selections in
the generated camera settings scene. Apply/Start recreates the Runtime with the
selected profile. Use the HUD's actual backend and raw body metrics when comparing
providers; physical-device performance remains user acceptance.

## Git Package Manager

Unity: Window > Package Manager > + > Add package from git URL:

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.3.0-preview.5
```

The repository is private: use an account/SSH key with repository access.
SSH alternative:

```
ssh://git@github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.3.0-preview.5
```

Package includes Windows x64 and Android ARM64 libraries and pinned model files.
On Editor installation and before builds, HumanVisionModelInstaller copies the two
models into Assets/StreamingAssets/HumanVision/Models. An explicit menu is available:
HumanVision > Install Packaged Models. Native libraries remain in the package.
This copy is required because UPM model folders are not APK StreamingAssets.

## Local installation

- Assets import: download HumanVisionSDK-0.3.0-preview.5.unitypackage from Releases,
  then Assets > Import Package > Custom Package.
- UPM offline: download com.blazetc.humanvision-0.3.0-preview.5.tgz, then Package Manager
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
One GPU readback is outstanding at a time; the latest-frame slot is refreshed up to30FPS. Analysis defaults to
640x640 maximum preserving aspect ratio. Android ORT session pools use two threads
and disable spinning. Android detection uses CPU; pose AUTO requests NNAPI with CPU fallback. RKNN is not included.
HUD separates render FPS from actual inference FPS and source age.
Live skeleton visibility now has a configurable source-age limit (default 3000 ms),
 and the HUD reports actual pose FPS, source age and native/visible body counts.
 This display allowance does not make an old pose current or increase inference FPS.
 The raised-hand example uses its own 1500 ms age limit. MP4 synchronization is unchanged.

Windows/Android libraries and C# are compiled; phone performance and hand accuracy
require user validation. Eight-person 30 fresh complete skeleton FPS is not certified.
Coordinates are RGB image-plane coordinates, not Kinect metric 3D depth.
Third-party license and model provenance records are included in the package.

## 0.3.0-preview.2 手机更新（历史）

- 两个场景共用安全区、横竖屏自适应 GUI。手机短边按480个界面单位布局，按钮高50单位；设置面板支持滚动。
- 相机启用自动旋转，修正90/270度纹理采样方向；预览与识别共用校正后的图像。启动时校准GPU回读行顺序。旋转后丢弃旧方向结果。
- 骨骼显示不再受固定350ms门限限制。HUD中的Bodies是原生结果人数，Visible是当前可显示人数，Age是源帧年龄。
- `HumanVisionRaisedHandDetector.cs` 是简单举手示例：regionIndex选择区域，比较有效手腕与肩膀的归一化Y坐标，输出左右手状态；默认忽略超过1500ms的动作结果。
- 现有相机场景的HumanVisionSceneControls会自动挂载举手组件；新建场景也已挂载。可在Inspector调整regionIndex、heightMargin、minimumConfidence和maximumPoseAgeMilliseconds。
- 更新Git依赖到新标签即可；不要同时导入unitypackage。若当前场景经过自行修改并删除了SceneControls，请手动挂载举手组件并指定manager。
- 未执行手机、摄像头、Unity运行测试；仅编译与包内容校验。请实机检查前后摄像头、横竖屏、身体与双手、举手状态，以及关闭Use regions后的全画面识别。

## 0.3.0-preview.3 Android follow-up (historical)

- All controls, settings, gesture status and diagnostics are now in one scrollable sliding drawer. The edge arrow opens/closes it; Edit regions collapses it automatically. No top/bottom panels remain over the image when collapsed. Region input excludes only the actual drawer and edge tab.
- Android AUTO now requests NNAPI acceleration without FP16 relaxation; unsupported operators remain on ORT CPU. NNAPI initialization or inference exceptions fall back to CPU. This is a requested acceleration path, not proof that all model nodes ran on a GPU/NPU. `forceCpu` on HumanVisionCameraManager allows a CPU-only comparison after restarting the app.
- CPU graph optimization is enabled. The live input keeps the native latest-frame slot current (at most one GPU readback outstanding, up to30 submissions/sec). Android detector interval is2; tracked crops are reused for at most500ms before redetection. Pose and both hands are inferred from each processed frame. Region masking remains applied to every frame. No interpolated joints are advertised as fresh inference.
- The drawer reports detector and pose milliseconds separately alongside actual pose FPS and source age. User screenshots of the previous version showed2.2 pose FPS and599–731ms age; there is no new phone measurement yet, and eight-person30FPS is not claimed.
- UPM model metadata now uses a separate GUID namespace from the StreamingAssets unitypackage copies. Existing StreamingAssets models and GUIDs are preserved.
- Verification: Windows/Android native builds and managed conditional compilation; package and metadata/hash checks. No runtime, camera or phone tests executed per user instruction.

NNAPI behavior reference: https://onnxruntime.ai/docs/execution-providers/NNAPI-ExecutionProvider.html

## 0.3.0-preview.5 Android tracking continuity

The supplied phone recordings show 535-892 ms detector passes and repeated
zero-body snapshots while people remain visible. Preview.4 reset all tracks when
the detector source exceeded 1200 ms, even when current-image pose could continue.
Its pending detector queue also kept the CPU detector continuously busy.

Android now refreshes each tracked crop from successful current-image body joints.
Delayed detections cannot overwrite a newer pose crop. The 1200 ms detector seed
limit no longer clears pose-validated tracks. A crop can be retried for up to 3 s
across a slow frame, but every published skeleton still requires new model inference;
no old joints are republished. Fewer than five valid body joints or two torso joints
rejects the pose and retires that crop. Real palm, fingertip and thumb outputs remain.

The detector accepts the newest image only when idle. While tracks exist it pauses
500 ms after completion, or 1000 ms when all requested places are occupied. With no
tracks it searches immediately. This reduces CPU contention; it does not make the
underlying model a 30 FPS model. New-person discovery may take the pause plus one
full detector pass. Region masking and orientation revision rejection remain active.

HUD explicitly distinguishes an empty result from a valid skeleton; Pose FPS counts
only frames containing valid poses. Detector and pose timings are parallel costs.
Windows retains its existing sequential path.

Verification: native Windows/Android and managed compilation; archive/hash checks.
No unit/integration/Unity runtime/phone tests were run, per the user's instruction.
Actual device continuity, latency, reacquisition and fresh pose FPS require retesting.
Update the Git version and rebuild the APK; no scene recreation is required.
