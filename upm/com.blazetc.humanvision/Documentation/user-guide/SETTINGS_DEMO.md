# UGUI 设置场景

执行 **HumanVision → Create SDK settings demo assets**，在 `Assets/HumanVisionSettingsDemo/` 创建普通 `.unity` 场景和可编辑 Prefab；重复执行保留已有布局。也可通过 Hierarchy **Human Vision → Create SDK Settings UI** 放入当前场景，支持 Undo。Package Manager 的 **SDK Settings** Sample 提供预生成资产。

布局按 Sensory 项目的 Setting 界面改编：1600×900、左侧 72% 视频/骨骼、右侧 28% 滚动设置、顶部 Camera/Video/RTSP，底部应用、应用并保存、停止和返回。无需 GameData/GlobalManager、动作识别或业务场景依赖。

## 设置流程

1. 选择输入模式，填写视频路径或 RTSP 地址；摄像头列表可刷新。
2. 选择人数 1–8、镜像和**采集分辨率下拉框**（640×480、1280×720、1920×1080、3840×2160）。FPS 在高级设置单独调整，切换分辨率不会重置 FPS。人数变化会均分重建草稿区域。
3. 可开启区域绑定并点击拉框编辑；框内移动，右下角缩放。也可逐项填写左上归一化 X/Y/W/H。重叠或越界整组应用会拒绝。
4. 点击“应用 / 重连”，等待 Input 实际 Streaming。可以修改线宽和点大小。
5. “应用并保存”在本次输入成功启动后保存；“仅保存草稿”只写配置，不改变运行会话。“重读保存配置”只加载为草稿。

草稿、成功应用配置和磁盘保存配置分别保留；状态条显示 SDK/Input 状态、实际人数、真实结果/帧编号、输入与推理 FPS、各槽位未知/无人/有人。UI FPS 不等于每人完整骨骼观测 FPS。

分辨率是相机的采集请求；Video/RTSP 仍采用文件或摄像头码流的实际尺寸。修改 RTSP 的实际分辨率需要在摄像头端选择对应码流，日志会保留请求值和实际值。旧配置包含其它尺寸时，下拉框增加一个“已保存”选项，读取或镜像操作不会悄悄改成 720p。原有界面可由 Editor API `HumanVisionSettingsDemoBuilder.UpgradeResolutionControls(view)` 升级，再保存场景/Prefab；它保留其它布局和引用，只移除宽高输入及空行。

桌面视频列表扫描 StreamingAssets 的 MP4。Android 的 StreamingAssets 是 APK 内的 URL，无法用 `Directory.GetFiles` 枚举，项目可在启动前调用 `view.SetBundledVideos(string[] paths)` 注册构建时生成的真实视频目录；刷新按钮会保留这些条目。选择列表后点击应用；自定义视频路径留空时使用当前选中条目，填写外部路径时则使用该路径。当前 Human-Vision-SDK-Test 已自动生成随包清单并使用 Android jar URL，可直接选择 `video-1.mp4`。标准 SDK 不猜测未声明的包内文件。

配置：`Application.persistentDataPath/HumanVisionSdkSettings/settings.json`，原子替换保留 `.bak`。读到损坏文件时保留原件，显示错误；“恢复备份为草稿”不会自动覆盖原件。

日志位于同目录 `logs/`。统计与骨骼分别节流记录；详细骨骼只写新的有效观测，携带身份、帧和有效坐标。每会话最多四个轮转文件，单文件大小和保留会话可配置。导出 ZIP 和复制路径适用于排查；Android 可把导出的路径交给自己的分享功能。配置文件包含用户填写的输入地址，请自行保护；状态/错误日志隐藏 RTSP 用户凭据。

## 项目接入

控制器 Inspector 显式绑定 SDK、View。可通过 `ReturnRequested` 绑定游戏返回逻辑，或者设置 `returnScene`；指定场景需加入 Build Settings。返回会先应用当前草稿，失败留在设置界面。指定返回场景前安全停止 SDK；如要跨场景保持会话，可在自己的返回事件中管理 SDK 生命周期。

桌面示例尝试加载系统中文字体；Android/发行构建建议在 `uiFont` 中指定有授权且包含中文字符的 Font 资产。所有控件都是 UGUI，可直接改样式、大小和引用；不要删除必需引用。

质量选择只显示本构建已声明的实际档位。Windows 固定合同不显示可切换档位；不把采集分辨率称作模型分辨率。RTSP 快捷按钮使用 Happytime 的 `rtsp://局域网IP:554/videodevice` 或 `video-1.mp4`，实际服务器仍需自行启动。

## 模型等级为什么可能不可选

Windows CPU/DirectML 当前安装配置使用固定模型，不提供高/中/低档。菜单置灰时显示这个实际原因，不会将采集分辨率冒充模型等级。Android NCNN/Vulkan 随包有低 512×288、中 640×384、高 960×576 三档；界面启动时先准备并校验资源，无需先打开输入即可选择。损坏或缺失的等级目录会显示具体错误。修改等级后点击应用，等待模型和输入重新启动。

## 实机诊断日志

进入设置场景就建立日志会话；初始化失败也会留下日志。日志包括 Unity/应用版本、设备/CPU/内存/GPU/图形 API、路径、保存配置、请求采集尺寸、实际 Profile/ModelPack/分析尺寸、Runtime 诊断、应用耗时、操作和状态变化、SDK/Unity 警告及错误堆栈。周期统计记录输入与推理 FPS、各阶段耗时、提交/处理/丢帧、来源帧、结果年龄、区域占用及稳定 ID。Streaming 超过 10 秒没有新结果会记录超时，恢复时另记恢复事件。

高级设置中的“详细骨骼日志”增加按间隔输出的 32 语义关节有效性、API 有效性、派生标记、置信度、像素/归一化坐标和独立观测时间。默认统计 2 秒一次，避免逐帧写盘；最多四个文件轮转，按设置保留会话。RTSP 认证和常见密码/token 参数会统一遮盖。打开目录、复制路径或导出 ZIP 后可携带日志定位实机问题；写盘失败会在界面提示。

主菜单 HumanVision 只保留创建 SDK、设置 Demo、安装模型及 Examples。旧示例位于 Tools → Human Vision → Legacy Examples；验收和探针位于 Tools → Human Vision → Development。
