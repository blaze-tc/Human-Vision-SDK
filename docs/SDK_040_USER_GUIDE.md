# Human Vision SDK 0.4.0-preview.3

独立 Unity SDK，目标为 Windows x64 和 Android ARM64，不依赖 AzureKinectExamples。
本版使用语义 Runtime Host、可替换流水线/后端、ModelPack 和 Profile。
实际手机帧率、延迟、1–8 人完整骨骼和双手质量仍由用户进行设备验收。

## 安装

Package Manager → Add package from git URL：

```
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.3
```

也可从 Human-Vision-SDK Releases 下载 unitypackage 或 UPM tgz。私有仓库需要
Git 访问权限；只选择一种导入方式，避免重复脚本和原生库。
编辑器按包内索引复制数据到 Assets/StreamingAssets/HumanVision/Runtime；Android
首次启动按哈希提取到 persistentDataPath，后续复用相同文件。无需清除 Library。
原有脚本 GUID、17+6 点接口和 V1 C ABI 保留。

## 场景与设置

使用 HumanVision > Create Live Camera Demo 创建摄像头和独立设置场景。
支持 WebCamera/RTSP，选择设备或填写地址后 Start。侧边箭头收起面板，
设置面板可滚动；Edit regions 收起遮挡后拖动区域。每个区域对应 RegionIndex。
区域外像素在推理前屏蔽，相机预览保持完整；保存后重启仍使用该配置。

默认骨骼为独立于 RawImage 的批量 UGUI 网格。调整 HumanVisionSkeletonOverlayer
的 lineWidthPixels/jointDiameterPixels。显式指定旧 jointPrefab/linePrefab 时
保留旧逐对象绘制。点是图像平面坐标，并非深度传感器的米制3D坐标。

## 获取数据

原有 GetColorImageTex、TryGetBodyByRegionIndex、TryGetJointByRegionIndex 等继续可用。
body.CanonicalJoints 提供32个语义关节点，使用 HumanVisionCanonicalJointId 枚举。
双手输出 Hand、Handtip（食指指尖）、Thumb（拇指指尖）；手掌由实际识别点求中心，
标记 IsDerived。看不清的手可以无效，不用手腕伪造手指。
每个关节点带独立 ObservationTimestampUs，手部与人体任务异步进行。

HumanVisionManager.Bodies 是原始观察，SampledBodies 是平滑/有限预测后的显示数据。
Demo 的 targetDisplayFrameRate 默认60，实际显示速率仍取决于设备。
预测最多25ms，随后在按实测观察周期计算的300–800ms显示窗口内保持最后的
滤波骨骼；超过窗口后才隐藏。原始观察时间戳不变，显示保持不计作新识别帧。
数组循环复用，保留历史时自行复制。
ResultUpdated 表示新的原生观察，显示采样不能计算为新的识别帧。
HumanVisionRaisedHandDetector 是举手示例，按区域比较有效肩膀与手腕高度。

## 性能诊断

HUD 区分 Render、Raw body、Hand jobs、结果年龄、预处理和推理时间，并显示
Pipeline、Profile、Requested/Actual backend、原始/跟踪/采样人数和两级丢帧。
实际后端诊断来自已创建的会话，不根据请求选项推测 GPU/NPU 已启用。
auto 配置在1–2人使用轻量 TopDown，3–8人使用多人流水线；手部共享轮转任务。
设置场景可选择 android-cpu-nohands、android-xnnpack-nohands 和
android-nnapi-nohands；点击 Apply/Start 后 Runtime 会重新初始化。三种配置用于
同条件基准对比，不能把 Requested backend 当成 Actual backend。
forceCpu 仅在 override 为空时用于 CPU 对照。30FPS/15Hz是调度上限，不是实测保证。
QNN 为可选构建路径，默认包不宣称 QNN 已验证。

真机只需按 [Android preview.3 device benchmark](https://github.com/blaze-tc/Human-Vision-SDK/blob/main/docs/diagnostics/ANDROID_0403_DEVICE_BENCHMARK.md)
的六组矩阵测试，
每组至少30秒。若最佳 RTMO 仍低于15 Raw body FPS 或 Result age 高于180ms，
下一版本进入 QNN HTP 专项；本版不声称手机性能已通过。

维护入口：docs/maintenance/START_HERE.md。原生构建使用 NDK23、Android API24；
Unity 托管兼容目标2021.3/2022.3。自动化结果以 DEVELOPMENT_STATUS 为准。
