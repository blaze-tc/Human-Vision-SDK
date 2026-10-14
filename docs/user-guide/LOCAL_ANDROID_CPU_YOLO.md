# preview.8 本地 Android CPU / GPU / NPU 部署

preview.8 在恢复的 preview.7 上接入 YOLOv8n-pose CPU，并修复初始化回调的配置提交顺序。
公共 API 不变，Input 仍为 0.1.0-preview.5。完整资源补充包保存在本地，与公共 SDK 主包配套使用。
CPU 界面“固定模型”表示只提供 512×288 模型输入档位，人数仍可设置 1–8；多档 CPU 需要另外匹配并验证模型资源。

| 平台/模式 | 当前模型和输入 | 部署 |
| --- | --- | --- |
| Android CPU | YOLOv8n-pose，ORT FP32，512×288，1–8 人 | 本次完整本地补充包 |
| Android Vulkan | 原 YOLOv8n-pose，低 512×288、中 640×384、高 960×576 | SDK 加完整本地补充包 |
| RK3588 NPU | 原低档 512×288 RKNN、原驱动库和输入内存资格检查 | 完整本地补充包；需要 RK3588 硬件 |
| Windows CPU/DirectML | 恢复版原模型与 Windows 原生库 | SDK 主包 |

先导入 Input 0.1.0-preview.5，再导入 SDK 0.4.0-preview.8，最后导入
`HumanVision-LocalAndroidCpuResources-0.4.0-preview.8.unitypackage`。
不要混装 Assets 和 UPM 两套 SDK。旧版本项目先备份，并用本次对应 SDK 替换主包。
执行 **HumanVision → Enable Local Android CPU Resources**：工具验证 52 个 Runtime 文件，
以及 SDK/Input/ORT/Windows/RKNN 原生库的准确哈希，成功后启用原 CPU/GPU/NPU 构建模式。
不要同时保留第二份 `librknnrt.so`。Android 保持 ARM64、IL2CPP、API 26+、Vulkan 优先。

设置界面仍分别选择计算模式与模型等级。CPU 为固定 512×288，等级控件不可选；
GPU 的低/中/高可选，NPU 仅低档。相机/RTSP 图像尺寸与模型尺寸互相独立。
四档显示尺寸、镜像和来源身份由原输入与坐标变换链路处理；CPU 模型内部采用 114 填充的 letterbox。

资源补充包中保留旧 CPU 的 TopDown/RTMO 文件，作为兼容和同安装包比较夹具。
正常 preview.8 Android CPU 选择只有新 YOLO 配置，旧夹具不会自动替换它。
模型校验拒绝错哈希、重复索引、损坏模型、链接逃逸、错误 CPU 配置和含混模型清单。

使用 `Initialized` 时，成功回调已经提交本次配置；读取 `ActiveConfiguration` 可以获得实际值。
`OpenInputOnInitialize=false` 为 Ready，打开输入成功为 Running，输入失败不发送成功事件。
坐标、数据借用与复制、世界平面映射及生命周期合同仍见 [统一 API](SDK_CONFIGURATION.md)。

本地补充包包含模型和 RKNN 库，仅作为本地评估资料；未上传公共 Release，分发资格标记保持 false。
OnePlus 的 CPU/Vulkan 结果不能替代 RK3588 的 NPU 驱动与帧率验收。
实际本轮构建、测试和设备结果见 [验证报告](../reports/2026-10-14-main-cpu-yolo-api.md)。
