# UGUI 设置场景

执行 **HumanVision → Create SDK settings demo assets**，在 `Assets/HumanVisionSettingsDemo/` 创建普通 `.unity` 场景和可编辑 Prefab；重复执行保留已有布局。也可通过 Hierarchy **Human Vision → Create SDK Settings UI** 放入当前场景，支持 Undo。Package Manager 的 **SDK Settings** Sample 提供预生成资产。

布局按 Sensory 项目的 Setting 界面改编：1600×900、左侧 72% 视频/骨骼、右侧 28% 滚动设置、顶部 Camera/Video/RTSP，底部应用、应用并保存、停止和返回。无需 GameData/GlobalManager、动作识别或业务场景依赖。

## 设置流程

1. 选择输入模式，填写视频路径或 RTSP 地址；摄像头列表可刷新。
2. 选择人数 1–8、镜像、采集预设或高级宽/高/FPS。人数变化会均分重建草稿区域。
3. 可开启区域绑定并点击拉框编辑；框内移动，右下角缩放。也可逐项填写左上归一化 X/Y/W/H。重叠或越界整组应用会拒绝。
4. 点击“应用 / 重连”，等待 Input 实际 Streaming。可以修改线宽和点大小。
5. “应用并保存”在本次输入成功启动后保存；“仅保存草稿”只写配置，不改变运行会话。“重读保存配置”只加载为草稿。

草稿、成功应用配置和磁盘保存配置分别保留；状态条显示 SDK/Input 状态、实际人数、真实结果/帧编号、输入与推理 FPS、各槽位未知/无人/有人。UI FPS 不等于每人完整骨骼观测 FPS。

配置：`Application.persistentDataPath/HumanVisionSdkSettings/settings.json`，原子替换保留 `.bak`。读到损坏文件时保留原件，显示错误；“恢复备份为草稿”不会自动覆盖原件。

日志位于同目录 `logs/`。统计与骨骼分别节流记录；详细骨骼只写新的有效观测，携带身份、帧和有效坐标。每会话最多四个轮转文件，单文件大小和保留会话可配置。导出 ZIP 和复制路径适用于排查；Android 可把导出的路径交给自己的分享功能。配置文件包含用户填写的输入地址，请自行保护；状态/错误日志隐藏 RTSP 用户凭据。

## 项目接入

控制器 Inspector 显式绑定 SDK、View。可通过 `ReturnRequested` 绑定游戏返回逻辑，或者设置 `returnScene`；指定场景需加入 Build Settings。返回会先应用当前草稿，失败留在设置界面。指定返回场景前安全停止 SDK；如要跨场景保持会话，可在自己的返回事件中管理 SDK 生命周期。

桌面示例尝试加载系统中文字体；Android/发行构建建议在 `uiFont` 中指定有授权且包含中文字符的 Font 资产。所有控件都是 UGUI，可直接改样式、大小和引用；不要删除必需引用。

质量选择只显示本构建已声明的实际档位。Windows 固定合同不显示可切换档位；不把采集分辨率称作模型分辨率。RTSP 快捷按钮使用 Happytime 的 `rtsp://局域网IP:554/videodevice` 或 `video-1.mp4`，实际服务器仍需自行启动。
