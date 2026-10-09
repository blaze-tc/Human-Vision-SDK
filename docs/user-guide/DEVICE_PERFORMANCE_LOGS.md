# HumanVisionSettingsDemo 实机性能日志

此功能用于 `Human-Vision-SDK-Test` 的 `HumanVisionSettingsDemo`。它是测试项目的诊断组件，SDK 的业务接入 API 与初始化方法保持原有用法。

## 获取日志

进入场景即开始记录，初始化失败也保留证据。点击常驻的 **复制日志文件夹**，拿到本次会话目录；无需先导出 ZIP。

- Android 10 及以上：`/storage/emulated/0/Download/HumanVisionLogs/session-日期-时间-随机号/`。在文件管理器的 **Downloads → HumanVisionLogs** 直接获取整个文件夹。后台约五秒同步一次，包括本次 SDK 设置日志；同步状态显示在硬件信息条。点击复制也会请求一次同步。
- Windows/Unity Editor：直接复制实际会话目录，粘贴到资源管理器。
- Android 较旧版本或 MediaStore 失败：显示应用私有目录与失败原因。私有原件仍保留，可使用现有 **导出日志 ZIP**。失败不代表日志被删除。

Android 公共目录通过 MediaStore 写入应用自己创建的文件，不申请全盘存储权限。诊断日志隐藏 RTSP 认证和常见密码/token；不复制原始 `settings.json`。每类原件最多四个轮转文件，单文件默认 8 MB，公共副本随对应原件更新。不同会话文件夹保留用于分析，测试后可以自行清理旧会话。停止输入后等待约五秒，再取文件夹，可包含停止及最后一次统计；直接强制终止应用可能留下最后几秒尚未同步的日志。

## 文件与计数

| 文件 | 用途 |
|---|---|
| `session.json` | 设备、图形 API、版本、构建/native 标识、日志格式 |
| `performance-*.csv` | 每秒的发布帧、提交、处理、结果通知、丢帧、实际人体数、帧年龄、各类 FPS、SDK 汇总耗时 |
| `hardware-*.jsonl` | 约两秒一次的硬件采样、时间、来源、可用性及读取失败原因 |
| `timings-*.jsonl` | SDK 当前结果汇总 + 最新完整的稀疏原生阶段样本；保留各自帧号和时间 |
| `native-*.log` | 本应用原生六阶段原始计时，方便独立复核 |
| `skeletons-*.jsonl` | 默认每秒采样一次新鲜真实结果，记录稳定 ID、关节有效性和坐标；不是全部逐帧结果 |
| `events-*.jsonl` / `unity-*.log` | 操作、状态、链路/超时、Unity 普通/警告/错误/异常及堆栈 |
| `sdk-*-diagnostics-*.log` | 本次运行新建的 SDK 设置日志，公共文件夹中的同名副本 |

Android MediaStore 可能为文本日志追加 `.txt` 后缀（如 `session.json.txt`、`timings-0.jsonl.txt`），内容和原件相同；分析脚本同时支持两种文件名，无需手工重命名。

`fresh_body_result_events` 只累计 **含至少一名人体的新鲜 ResultUpdated 通知**；`fresh_body_results_fps` 是它的一秒窗口速率。它不等于逐人完整 32 关节 FPS，也不把无人的完成通知或重复渲染算成骨骼更新。`processed` 与 `sdk_result_events` 仍保留，可区分原生完成、SDK 接受和人体结果。分析必须按 source/generation 分段，排除停止、计数重置和预热。

此 GPU 路径的旧状态条“输入/推理 FPS”可能显示兼容统计值 0；请以底部真实发布/含人体新结果速率及 CSV 的递增计数判断，不把这个 0 当作没有执行推理。

## 耗时范围

诊断构建只启用六段稀疏壁钟计时：每次运行前 3 帧、随后每 64 帧。所有六段属于同一 `frameId` 且齐全时才发布。缺失或跨帧拼接不能生成完整样本；保留原始行供复核。此构建关闭逐层 GPU 查询，减少测量本身的开销。

| 字段 | 实际范围 |
|---|---|
| `importPreprocessRecordMs` | GPU 图像导入、预处理命令记录 |
| `preprocessSubmitWaitMs` | 输入生产者同步、预处理提交及等待 |
| `extractDownloadMs` | NCNN Extractor 执行；包含内部 GPU 提交/等待及下载命令记录，不能称为纯 CPU 或纯 GPU 时间 |
| `inferenceSubmitWaitMs` | 后续推理/输出下载提交及等待 |
| `denseOutputCopyMs` | 已完成输出的数据复制 |
| `ownershipReleaseMs` | GPU 图像所有权释放及同步 |

六段可在 **同一个原生样本** 中求和或比较占比。SDK `totalMs` / `poseMs` / `trackingMs` 是当前结果的另一组汇总，可能和稀疏样本属于不同帧，不直接相减来推算“CPU 开销”。当前 YOLO Runtime Host 把 backend 的整体壁钟放在兼容 API `PoseMs`；`TrackingMs` 对应输出解码与 GPU role 完成；`TotalMs` 是 pre/infer/post 的和。换用其它 pipeline 后，字段范围应结合 Runtime 诊断判断。

输入侧保留发布 FPS、待复制数量、提交/处理计数和本地发布后的结果年龄。当前没有独立测量摄像机曝光、网络传输或硬解码时长，因此结果年龄不是摄像机到屏幕的端到端延迟。Unity CPU/GPU frame time 是 **渲染帧时长**，不是 GPU 使用率或模型 GPU 时长。

2026-10-09 Input优化构建的常规原生明细只保留启动3帧和每64帧，错误、重连及
终态资源统计仍完整保留。`events` 用 `native.batch` 索引原生原文；具体内容见
`native.log`，避免双份写入。`timings` 的 `diagnosticFlushMs` 为最近一次日志批次
实际写入/关闭耗时，`diagnosticFlushMaximumMs` 为本会话峰值，不是推理耗时。
Input新样本的 `decoded_us → submitted_us` 是本地调度，
`submitted_us → converted_us` 包含GPU完成及渲染线程轮询。
`received_us` 只是取图时最近demux到达观测，未证明同包对应关系；不能据此声称
测到了精确硬解时长。PTS也不能与本地monotonic时间相减。

判断方向：输入发布低于目标先检查视频帧率/码流/解码；模型及等待占主体先优化模型输入等级或验证加速后端；提交/导入等待大再检查生产者同步；解码/释放大再检查结果处理。保留全部段，避免根据单个汇总武断移除 GPU 等待。

## 硬件指标

应用 CPU 从自身进程所有线程的 CPU 计数差分得到，同时列出 **全部逻辑核容量占比** 和 **一核为 100% 的占比**；后者可超过 100%。Android 从 `sysconf(_SC_CLK_TCK)` 查询计数频率。系统 CPU 用 `/proc/stat` 差分，guest 不重复计算。应用内存用 Android PSS，另记系统可用内存。

GPU 使用率读取可访问的 Adreno KGSL 或已知 Mali 驱动计数，是设备整体负载。KGSL `gpubusy` 已给出驱动统计区间 busy/total，不再按累计计数差分。NPU 负载没有可读取来源时显示不可用；当前 CPU/NCNN Vulkan profile 未使用 NPU。电池温度不冒充 CPU 结温，Android thermal status 不单独证明未降频。CPU 各核频率、GPU 频率能读取时一并写日志。

RK3588构建自动发现 `/sys/class/devfreq/*gpu*`/`*mali*`，支持Rockchip
`负载@频率Hz`格式；NPU/DMC节点不算GPU。`cur_freq`独立于负载读取，保留
`gpuFrequencySource/Status`。`thermalSensors` 保存至多32个热区的type、路径、
温度和状态，`cpuTemperatureC/gpuTemperatureC` 按热区名称汇总，与电池值分开。
缺权限/坏格式保留原因，不申请root、不修改频率或温控。
字段含义依据[Rockchip devfreq实现](https://github.com/rockchip-linux/kernel/blob/develop-5.10/drivers/devfreq/devfreq.c)。

所有不可读取的数值用 **-1 + 原因/来源** 表示，界面显示 **不可用**，不伪装成 0% 空闲。采样和公共目录同步在后台线程执行；主线程按一秒写统计、约两秒写硬件，不逐帧输出 JSON。

原始 API 参考：[Unity FrameTimingManager](https://docs.unity3d.com/2021.3/Documentation/ScriptReference/FrameTimingManager.html)、[Android PSS](https://developer.android.com/reference/android/os/Debug.MemoryInfo#getTotalPss())、[Android thermal status](https://developer.android.com/reference/android/os/PowerManager#getCurrentThermalStatus())、[KGSL gpubusy 驱动实现](https://android.googlesource.com/kernel/msm/+/b8b5669957f3fe0378c431013dca33757e05bda0/drivers/gpu/msm/kgsl_pwrctrl.c)。

## 视频复测与分析

在设置场景选择 **Video → video-1.mp4 → 模型等级低（512×288）→ 应用**。人数和区域按需要选择；人数减少不会减小此全图模型的图计算量。当前连接 OnePlus 9 Pro 的正常 Fix2 基线：相同 25 FPS 视频，中等级约 15.93 次完成/s，低等级约 23.09 次完成/s，低等级预热后的窗口约 21.67–24.74 次/s。这是已有合格低等级 profile 的选择收益，伴随分辨率/精度取舍，不能外推 RK3588 或证明完整手部能力。新增日志构建的长测数据见 [性能复测报告](../reports/2026-10-09-device-performance-telemetry.md)。

取得目录后可直接运行：

```powershell
python tools/benchmark/analyze_settings_device_log.py "日志会话目录" "analysis.json" --warmup 30
```

输出分段后的新结果/含人体结果 FPS、窗口分布、六段耗时、硬件可用性和错误，保留计时范围。将 **整个会话文件夹 + 同时段测试视频/截图** 留给后续分析即可。

分析器按实际profile切换和输入代次分段；人物覆盖率单列，不把无人大段统计成
有人时的骨骼帧率。强制退出可能留下最后一条未写完JSON，分析器只忽略未换行的
坏尾条并记录 `dataQualityWarnings`；完整行或中间损坏仍报错。
RK3588实测结论与NPU当前状态见[现场分析报告](../reports/2026-10-09-rk3588-field-analysis.md)。
