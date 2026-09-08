# HumanVision Live Camera SDK 0.2.0-preview

本次按用户要求只交付代码、编译和封装，**没有运行新功能测试**。
Windows 摄像头、RTSP、Android 发布和区域交互等待用户实测。
此前 `4859224-uhd_3840_2160_25fps` 的视频跟随已由用户确认。

## 导入与运行

1. 将 `HumanVisionSDK-0.2.0-preview.unitypackage` 导入 Unity。建议先导入空项目；
   支持目标为 Windows x64 Editor/Player、Android ARM64。
   编译使用 Unity 2021.3.45f1 的程序集；推荐 2021.3/2022.3 LTS。
2. 项目需要 UGUI、Video、ImageConversion、UnityWebRequest、IMGUI、WebCam 模块。
   普通 Unity 3D 项目通常已经启用。使用 .NET Standard 2.1。
3. 菜单 `HumanVision > Create Live Camera Demo` 创建独立的新场景。
   不依赖、不导入 AzureKinectExamples。场景生成器会保留已有场景文件。
4. 进入 Play，等待模型准备完成。选择 WebCamera，用 `Next device` 切换设备；
   或选择 RTSP，填写 `rtsp://用户名:密码@地址:端口/路径`，默认 TCP。
   按 `Start` 获取画面、推理并绘制骨骼。`Stop` 停止采集。
5. 发布时将新场景置于 Build Settings 首位或仅勾选该场景。
   此包不替换你的 ProjectSettings，也不自动改动图形 API/发布设置。

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

当前生产模型仍为 COCO-17；手掌、指尖、大拇指的真实额外输出尚未接入此版本。
显示层的躯干连线不增加模型观测关键点。1–8 人配置不等于8人30FPS达标。

## Android

- IL2CPP、ARM64 only、最低 API24；实机目标 RK3588 Android12。
- 包内 Android 库已交叉编译，使用通用 ONNX CPU，**不包含 RKNN/NPU 加速**。
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
`Assets/StreamingAssets/HumanVision/Models`：检测与17点姿态模型。
不包含 AzureKinectExamples、测试框架或用户摄像头图片/视频。

如果导入已有 HumanVision 项目，先退出 Play；包内同路径文件是此版本的替换文件。
不要保留另一目录下同名的 humanvision/onnxruntime/FFmpeg 插件副本。
Windows 非开发机可能需要 Microsoft Visual C++ x64 Runtime。

请实测后反馈：Unity版本/平台、摄像头型号和来源、是否有画面、各区域是否对应、
骨骼跟随情况、Console 错误原文。由你完成运行验收后再修正实际问题。
