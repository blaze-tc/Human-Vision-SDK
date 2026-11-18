# HLK-AL00 Runtime Prefab 实时预览与同帧拍照

测试场景为 `Human-Vision-SDK-Test/Assets/HumanVision/Samples/RuntimePrefab/RuntimePrefabAcceptance.unity`，Unity 2021.3.45f1。HLK-AL00 运行 Android 10。使用原始 Exercise-11，自第 20 秒起按 H.264 Main 1920×1080、30 FPS 原码流发布到 RTSP/TCP。下列性能对照的两个分支使用同一个 APK、同一个 CPU 模型和 30 FPS Unity 目标。渲染 API 为 Vulkan，识别后端为 ONNX Runtime CPU（`backend.ort.cpu`，`android-cpu-nohands`）。场景没有跨栏动作判定，起跳与比赛计分无样本。

## 首 30 秒

| 指标 | 旧式同帧画面 | 实时画面与独立骨骼 |
| --- | ---: | ---: |
| Unity Player 渲染 FPS | 30.07 | 30.10 |
| 预览每秒不同源帧 | 2.95 | 25.78 |
| 解码完成至 Unity 提交绘制 P50 | 378.2 ms | 49.9 ms |
| 解码完成至 Unity 提交绘制 P95 | 720.0 ms | 81.6 ms |
| 新识别结果 FPS | 8.74 | 8.63 |
| SDK 推理耗时 P50 | 110.5 ms | 111.7 ms |
| SDK 整段推理流水线 P50 | 112.0 ms | 113.3 ms |
| 电池温度 | 38°C | 35°C |

窗口从各自首次实际绘制计时。帧率使用落在窗口内的完整采样区间。温度不同，Android 热状态均为 0，这两组推理时延不能用于归因微小差异。延迟起点为设备本地解码完成，终点为 Unity 提交预览绘制。摄像机采集、跨网传输和屏幕扫描不在该区间。

同窗口内 Android Thermal HAL 的 GPU 传感器最高为 59/57°C，CPU cluster0 最高为 62/60°C，cluster1 最高为 70/68°C，按旧式/实时顺序列出。电池温度取自 SDK 日志中的 Android battery 传感器。

识别实际提交 512×288，预览源和精确同帧照片为 1920×1080。照片临时启用同帧需求，使用 GPU 快照及异步回读。精确匹配按来源、代次、输入帧标识和 SDK 提交标识校验。实测照片输入帧 1012、提交帧 970、结果序号 325、来源 2、代次 2。识别图用作同帧底图的选项为 false。

## 显示与恢复

- 左右横屏允许自动切换，通过 Android WindowManager 分别观察到 `SurfaceOrientation: 1` 与 `3`。手持传感器触发不在这次自动验证范围。
- 常规界面使用整个窗口背景。全屏可选择保持完整视野或按原比例裁切铺满。两种模式下骨骼和相机遵循同一显示区域。
- 在连续有人且未触发开关的 30 条逐秒采样中，最终的实时骨骼都保有 96 个点。修复前可见计数来回切到 0。180 ms 保持限于同一输入绑定，明确无人结果、断流或主动关闭会清除。
- 切断 RTSP 六秒后恢复，输入代次从 2 到 3。恢复后的实时绘制与最新发布保持同一来源、代次和帧标识。
- HLK 的「智能」分辨率在测试中曾将 Unity 窗口降至 1560×720，再合成到 2340×1080。系统也出现过原生 2340×1080 窗口。独立于系统缩放，线性色彩项目在预览采样时校正 Android RTSP 的 RGB 显示码值。相同画面缩到约 807×454 对比，RGB 中位绝对差从 52 降为 2（8 位码值）。

实机归档位于 SDK 工作区 `out/diagnostics/hlk-live-preview-20261118/`。原始 FPS 对照在 `final/comparison-first30.json`，色彩复测在 `color-check/live/`，截图 RGB 对照在 `screen-rgb-reference.json`。

## 单 APK 手动选择

在顶部选择分析设备和模型等级，填写 RTSP 地址后点击「连接相机」。已连接时切换选项会用新配置重启识别会话。状态显示实际后端、送入尺寸和模型尺寸。自动验收继续使用固定的 512×288 输入，并可通过命令行选择原有同帧画面或实时画面。

| 后端 | 已打包模型尺寸 | 设备条件 |
| --- | --- | --- |
| ONNX Runtime CPU | 512×288 | Android CPU 档 |
| NCNN Vulkan GPU | 512×288、640×384、960×576 | 通过 SDK Vulkan 能力检查 |
| RKNN NPU | 512×288、640×384、960×576 | RK3588 与配套本地资源 |

手动切档构建报告为 `Succeeded`，0 个错误。APK 使用 ARM64、Unity IL2CPP 和 APK v2 签名。APK SHA-256 为 `be790c5ba265ad86c0462f0d3107481bb0cb94c118b84cbfdd7d57b194210390`。内嵌 Input 为 `0.1.0-preview.10-hlk.1`，`lib/arm64-v8a/libhumanvision_input.so` SHA-256 为 `78e4a938814286105644f516967ec3aef711f6bcb38943d273df62c614ec8149`。

在 HLK 上安装同一 SHA 的 APK，连接原始 Exercise-11 后通过界面依次切换。每档取四个已产生结果的采样窗口，以下为短窗口中位值。电池从 34°C 变化到 36°C。

| 实际后端和档位 | 实际送入 | 模型尺寸 | 新识别 FPS | SDK 流水线 |
| --- | --- | --- | ---: | ---: |
| ONNX Runtime CPU 低 | 512×288 | 512×288 | 8.25 | 113.6 ms |
| NCNN Vulkan 低 | 512×288 | 512×288 | 8.25 | 119.9 ms |
| NCNN Vulkan 中 | 1920×1080 | 640×384 | 4.85 | 191.0 ms |
| NCNN Vulkan 高 | 1920×1080 | 960×576 | 2.43 | 424.0 ms |

四档实时预览均为 1920×1080，NPU 在 HLK 界面显示为不可用。手动拍照导出 1920×1080，帧身份为来源 2、代次 2、输入帧 221、识别提交帧 201、结果序号 71。日志、截图和 PNG 位于同一归档的 `selector/`。
