# HumanVision Live Camera SDK 0.3.0-preview.4

本次按用户要求只交付代码、编译和封装，**没有运行新功能测试**。
Windows 摄像头、RTSP、Android 发布和区域交互等待用户实测。
此前 `4859224-uhd_3840_2160_25fps` 的视频跟随已由用户确认。

## 导入与运行

1. 使用修正版 `HumanVisionSDK-0.3.0-preview.4.unitypackage`，通过
   `Assets > Import Package > Custom Package` 导入 Unity。建议先导入空项目；
   支持目标为 Windows x64 Editor/Player、Android ARM64。
   编译使用 Unity 2021.3.45f1 的程序集；推荐 2021.3/2022.3 LTS。
2. 项目需要 UGUI、Video、ImageConversion、UnityWebRequest、IMGUI、WebCam 模块。
   普通 Unity 3D 项目通常已经启用。使用 .NET Standard 2.1。
3. 菜单 `HumanVision > Create Live Camera Demo` 创建摄像头与设置两个独立场景，底部按钮切换。
   `HumanVision > Create Camera Settings Scene` 可直接打开设置场景。
   不依赖、不导入 AzureKinectExamples。场景生成器会保留已有场景文件。
4. 进入 Play，等待模型准备完成。选择 WebCamera，用 `Next device` 切换设备；
   或选择 RTSP，填写 `rtsp://用户名:密码@地址:端口/路径`，默认 TCP。
   按 `Start` 获取画面、推理并绘制骨骼。`Stop` 停止采集。
5. 发布时将新场景置于 Build Settings 首位或仅勾选该场景。
   此包不替换你的 ProjectSettings，也不自动改动图形 API/发布设置。

旧版及 importfix 包的 gzip 内部文件名使用了外层 unitypackage 文件名，
Unity 2021 导入器因此返回零资源。importfix2 改为 Unity 自身导出时使用的
`archtemp.tar`，并保留原资源 GUID，不需要删除 Library 或现有项目资源。
这是 Assets 资源包，导入后查看 `Assets/HumanVision`，不会作为 UPM 包显示在
Package Manager 中。摄像头和骨骼运行验收仍由用户执行。

## 人数与区域

- `People` 输入 1–8，按 `Set`。改变人数会重新生成 n 个等宽初始区域。
- 勾选 `Use regions`；`Edit regions` 开启拖拽，拖框内移动、拖右下角缩放。
- 区域编号为 **0 到 n-1**。每区只识别一个人，空区返回空，不会压缩编号。
- 区域不能重叠，不能越出画面。配置编辑完成后按 `Apply` 或 `Save`。
  编辑过程不改变已经生效的识别参数。区域很小或裁掉人体会降低识别质量。
- 原生推理前将区域外像素置黑；检测与姿态推理只使用屏蔽后的图像。
  画面仍显示完整摄像头图像。候选框中心用于判定区域归属，按检测分数选择。
- 未启用区域时，index 为当前结果数组顺序，不保证永久占位；游戏固定玩家槽位
  应开启区域。人物的 TrackId 与区域 index 是两种不同标识。
- `Save` 保存到 `Application.persistentDataPath/HumanVisionCamera.json`，
  `Load saved` 重新读取，启动时自动加载。RTSP 地址也保存在此本地文件，
  包含密码的配置不要提交到版本库。
- 切换摄像头、镜像方向或安装角度后需要重新确认区域与实际画面的对应关系。

## 游戏代码

```csharp
using HumanVision;
using UnityEngine;

public sealed class PlayerSlotReader : MonoBehaviour
{
    public int regionIndex = 0;
    void Update()
    {
        var sdk = HumanVisionCameraManager.Instance;
        if (sdk == null) return;
        Texture cameraImage = sdk.GetColorImageTex();
        if (!sdk.TryGetBodyByRegionIndex(regionIndex, out var body)) return;
        if (sdk.TryGetJointByRegionIndex(regionIndex,
            HumanVisionJointType.LeftWrist, out var wrist))
        {
            Vector2 pixel = wrist.Pixel;       // 图像左上角原点，Y 向下
            Vector2 normalized = wrist.Normalized;
            // 在这里用关键点驱动游戏；只读取 Valid=true 的关键点。
        }
    }
}
```

类似 KinectManager 的接口：`GetUsersCount()`、`IsUserDetected(index)`、
`GetUserIdByIndex(index)`、`GetUserIndexById(id)`、`IsJointTracked(id, joint)`、
`GetJointPosition2D(id, joint)`、`GetJointPosition(id, joint)`、
`GetColorImageTex()`、`GetColorImageWidth/Height()`。
`GetJointPosition` 是中心原点、Y 向上的单位图像平面，Z=0，**不是 Kinect 米制深度**。
`SkeletonUpdated(sequence)` 用于订阅新结果；返回的 Body/Joint 数组会复用，
需要长期保留时由调用方复制。未识别的 userId 为0，查询前检查有效性。

新版使用全身姿态模型，保留 `body.Joints[17]`，新增 `body.HandJoints[6]`：
左手 Hand/Handtip/Thumb，再右手三点。`HumanVisionJointType.LeftHand` 等可直接查询。
身体和双手同帧推理、同一结果序号。手掌由真实手部根节点和四个 MCP 点求均值，
`IsDerived=true`；指尖使用中指尖、拇指使用拇指尖，低置信度不显示，并非腕点外推。
显示层的躯干连线不增加模型观测关键点。1–8 人配置不等于8人30FPS达标。

## Android

- IL2CPP、ARM64 only、最低 API24。当前反馈设备为 OnePlus 9 Pro/骁龙888/Android14，RK3588仍为目标。
- Android 默认独立实时预览，推理不控制画面播放速度；默认分析尺寸上限640×640（保持比例），
  不再为实时输入分配历史画面缓存。可在 `HumanVisionLiveSource` 调整分析尺寸。
- Android 推理会话各限制2线程并关闭空转，减少和 Unity/摄像头竞争。尚未实机测速，
  不能据此宣称30FPS。侧栏显示 Render FPS、真实 Inference FPS、耗时与结果年龄。
  实时骨骼显示时限改为可配置 `maxLiveResultAgeMilliseconds`（默认3000ms）；HUD显示真实结果年龄，超时仍隐藏。放宽显示时限不会提高识别帧率，也不代表骨骼与当前相机帧同步。
- 包内 Android 库已交叉编译，Android检测固定使用CPU，姿态AUTO模式请求NNAPI设备加速并在失败时回退CPU；不包含RKNN。实际设备分配及性能需要实测。
- 包含 CAMERA/INTERNET 权限清单合并库；首次 WebCamera 请求摄像头权限。
  摄像头必须能被 Android Camera API 枚举；不保证所有厂商 USB UVC 固件自动支持。
- 模型从 APK 的 StreamingAssets 提取到 persistentDataPath，再交给原生库。
- 输入桥需要 `SystemInfo.supportsAsyncGPUReadback`。Android 优先选择支持此功能的
  Vulkan 配置；不支持的图形设备会报告明确错误，需要反馈设备/图形 API 信息。
- 首次模型加载和首帧包含初始化开销。RTSP 使用软件解码；断线后台重连，
  不承诺未实测的延迟、解码帧率或板端性能。

## 包内容与边界

`Assets/HumanVision`：运行时、输入、区域 UI、演示生成器、shader、文档。
`Assets/Plugins/x86_64`：Windows SDK、私有 ORT/DirectML、FFmpeg共享库。
`Assets/Plugins/Android`：ARM64 SDK/ORT/FFmpeg与权限合并库。
`Assets/StreamingAssets/HumanVision/Models`：检测与全身姿态模型。
不包含 AzureKinectExamples、测试框架或用户摄像头图片/视频。

如果导入已有 HumanVision 项目，先退出 Play；包内同路径文件是此版本的替换文件。
不要保留另一目录下同名的 humanvision/onnxruntime/FFmpeg 插件副本。
Windows 非开发机可能需要 Microsoft Visual C++ x64 Runtime。

请实测后反馈：Unity版本/平台、摄像头型号和来源、是否有画面、各区域是否对应、
骨骼跟随情况、Console 错误原文。由你完成运行验收后再修正实际问题。

## 独立绘制与设置

`Camera Image` 是独立 RawImage，可隐藏而不停止识别。`Skeleton Objects` 使用
`HumanVisionSkeletonOverlayer` 的球体和 LineRenderer，独立于画面绘制。
通过 `lineWidthPixels`、`jointDiameterPixels` 控制粗细，可分别关闭点和线；
可指定 jointPrefab/linePrefab。位置映射到 preview 矩形和 foregroundCamera 的图像平面。
设置在独立场景中，继续使用原有拖框/缩放逻辑和 Save/Load，矩形外不参与检测。
Git UPM 包与 unitypackage 为两种安装方式，不要同时安装。见 UPM_INSTALLATION.md。

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

## 0.3.0-preview.4 Detector bottleneck correction

User measurements from preview.3: detector1073–1086ms, pose60–86ms, pipeline0.9FPS,
source age1811–2311ms. The detector is the measured dominant stage. Whether NNAPI
partitioning or another device-specific cost caused all of the regression is not established.
The old500ms source-time detector deadline also retriggered immediately after each
one-second detector pass, so skipping every second pass did not help.

Android now uses a dedicated CPU detector thread and a separate pose thread (pose
AUTO still requests NNAPI with CPU fallback). The pose thread refreshes the detector's
single pending image up to5Hz; inference never holds the queue mutex. Region masks
are applied before both branches. Detector snapshots carry source timestamp, image
size and region revision; wrong-coordinate or older-than1200ms crops are rejected.
The pose thread always runs the model on its current input frame; it does not reuse
or fabricate joints. Tracker crop prediction is non-mutating and capped at250ms;
unmatched detections are not resurrected. A valid zero-person detector result clears
old tracks. Stop joins both workers before destroying their models.

Detector/Pose milliseconds are parallel stage costs on Android and must not be added
to interpret pose-frame latency. HUD Pose FPS is now a recent half-second processed
rate, not the lifetime average. The core cumulative counters retain their original API.
No30FPS claim: the provided single-body60–86ms pose timings alone exceed a33ms budget.
This release addresses the long detector stall; phone validation is still required.

Verification: Windows x64 and Android ARM64 native builds; managed Runtime/Demo/Editor
and Android conditional compilation. No unit/integration/Unity runtime/phone tests run
per user instruction. Rebuild the APK after updating the Git package; scenes and models
need not be deleted or recreated.
