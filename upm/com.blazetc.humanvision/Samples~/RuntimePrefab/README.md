# Runtime Prefab Acceptance

配套 **Human Vision SDK 0.4.1 / Input 0.1.1**。在 Unity Package Manager 的 **Human Vision SDK → Samples** 导入 **Runtime Prefab Acceptance**，打开 `RuntimePrefabAcceptance.unity`。离线安装后，在 `Assets/HumanVision/Samples/RuntimePrefab` 打开同一场景。

## 连接和使用

1. 进入 Play Mode，等模型和后端检查结束。点击左侧「输入连接」。
2. **RTSP** 填写设备可访问的完整地址，按需选择 TCP、镜像和执行方式。Unity 或 Android 上的 `localhost` 指运行 Demo 的设备本身。独立执行仅用于 RTSP，具体能力以平台和安装资源为准。
3. **本地相机** 刷新并选择 WebCamTexture 设备，设置请求宽高、帧率及镜像。Android 需要相机权限，实际采集规格由设备协商。
4. 点击「应用并连接」。成功后在当前应用保存输入配置。取消弹窗会继续当前会话。分享诊断时使用导出的 JSONL，该应用保存的输入配置可能含相机地址和认证信息。
5. 在左侧选择可用的 CPU、GPU、NPU 和模型档位。Android 构建使用 ARM64、IL2CPP、Vulkan，RTSP 构建将 Internet Access 设为 Require。RK3588 NPU 还需要配套的本地资源和双模式构建。以日志的实际后端、Profile 和输入状态为准。

「识别与显示设置」可编辑人数、区域、置信度、结果时效、预览及 Unity 限帧。「一键测试」重新连接当前输入，等取得有效结果后记录 180 秒。结束时导出 JSONL，分别记录解码、预览、推理和有效骨骼。

## 预览、点线与日志

右侧只显示一份颜色预览，并在同一区域叠加识别框和点线。普通 RTSP 颜色纹理使用所填码流的可见原尺寸。分析图像及被采样的有效骨骼独立更新。业务可在 `Update` 直接读取有效关节坐标，例如 `Sdk.TryGetJointNormalizedPosition`。

「拍照（精确同帧）」临时启用 `MatchedColor`，等待同一来源的新鲜精确配对，保存匹配原色帧至应用目录下的 `runtime-photos`，再恢复实时模式。此功能需要图形栅栏和异步 GPU 回读。全屏支持等比显示和填满裁切。

UGUI、弹窗、EventSystem、骨骼相机和 8 组场景点线在场景中预先配置。可在 Scene 中选中 `Live Skeleton Scene/Body N` 子节点的 GameObject 或 LineRenderer。场景的 `RuntimePrefabAcceptance` 控制器在 Inspector 引用一个 `HumanVisionRuntime` 和所有控件。

「一键采集日志」导出脱敏 JSONL、显示实际路径并复制到剪贴板。Android 优先写入公共下载目录的 `HumanVisionLogs`，不可用时写入应用目录。日志底部显示系统可读取的 CPU、GPU、NPU 和电池温度，缺少相应读数时显示「不可读」。

[完整接入说明](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.1/docs/user-guide/UNITY_RUNTIME_PREFAB.md)
