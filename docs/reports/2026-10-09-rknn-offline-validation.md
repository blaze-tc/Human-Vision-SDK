# RKNN 离线验证与 OnePlus 回归（2026-10-09）

结论：Docker/WSL2 的环境阻塞已解决；固定现有 Low 模型恢复的 ONNX 与非量化
RKNN 候选通过三个离线样本的原有精度门槛。实际 INT8 候选未通过，拒绝采用。
OnePlus 的现有 NCNN Vulkan APK 显示视频与真实骨骼，300 秒测试完成约 23.51
个新推理结果/秒。没有连接 RK3588，因此尚未证明 NPU 驱动兼容性、速度、热稳定性
或整个 SDK 的 NPU 输入/输出生命周期。当前 APK 仍使用 NCNN Vulkan。

## 环境与来源

用户重启后，本机 WSL 2.6.2、Docker Linux Engine 29.1.2 正常；工具镜像
`humanvision-rknn:2.3.2`，Python 3.10.22、RKNN-Toolkit2 2.3.2、Torch
2.4.0+cpu、ONNX 1.16.2、ORT 1.19.2、NumPy 1.26.4。固定基础镜像 digest、
主要依赖版本和厂商 wheel SHA；完整实际依赖保存于 `out/rknn-toolchain/pip-freeze.txt`，
环境与来源 receipt 在同目录。基础镜像、wheel 和实际构建均已运行，非占位结果。

官方工具版本固定到 `42aa1d426c0a9e0869b6374edba009f7208a1926`（v2.3.2），
wheel SHA256 为 `6cb783ddf293ac509f39bf9127acf6a5492bbb67e4b4b4ac33a7c6d2cefb4f3c`。
厂商官方示例支持无 target 的 PC 模拟器；该模式不能代表实体 NPU 的速度或驱动。
依据：[RKNN 工具](https://github.com/airockchip/rknn-toolkit2/blob/v2.3.2/README.md)、
[官方模拟器示例](https://github.com/airockchip/rknn-toolkit2/blob/v2.3.2/rknn-toolkit2/examples/onnx/yolov5/test.py)。

没有找到可证明与当前模型等价的原始 PT/ONNX。此次使用
`recover_pinned_onnx.py` 从固定 NCNN 图与原有权重恢复有界静态 ONNX，
不重新下载另一份权重替代。仅接受当前图的明确算子/参数、权重 tag、全部字节
消费和固定 SHA；不认识的图或权重立即失败。

| 资产 | SHA256 |
|---|---|
| 原 NCNN param | `908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905` |
| 原 NCNN bin | `6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9` |
| 恢复 ONNX | `6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3` |
| 非量化 RKNN 候选 | `1b3ba8dd5bb81e3d968cfeef019c42b1aa3bffca6f0ec1c9f89f257fe0c08030` |
| 被拒绝的 INT8 候选 | `dd6196bb312569b6dee9e1c467ad3c3a2e9326ad662ce4403db7b6f1e3699f23` |
| 验证 bank index | `5ab8bb97034e957f3176162a63b471790b64514c7ba5a34d17622190167d4c34` |

原 bin 为磁盘 FP16 fused 权重存储，NCNN 在当前执行路径转为 FP32。
恢复器消费 6,591,904 字节，205 层、72 卷积导出为 298 ONNX 节点。
ONNX 输入是 RGB FP32 `[1,3,288,512]`，外部 `/255`；原始输出为
`[1,3024,65]`、`[1,3024,51]`。RKNN 编译采用 mean0/std255，设备输入
是相同像素的 RGB uint8 NHWC，不再次 `/255`。非量化请求不保证每个内部算子
都采用同一浮点精度，设备工具会记录实际 tensor type/format/stride。

## 实际数值结果

保留三组新 reference，均实际执行原 NCNN CPU runner 并冻结输入、输出和日志 SHA。
七人来自 `video-1.mp4` 原帧1500及已有人工框；一人控制来自真实人物图片，置于
114填充的1024×576画布后缩放；空画面是统一114的分析性负样本，不能冒充真实
空摄像头。三个控制不是完整场景/光照/RTSP/镜像/手部覆盖。

门槛原样复用：raw max≤0.2、raw mean≤0.01、人数一致、框 IoU≥0.95、
人体/关节置信度误差≤0.01、所有17关节源坐标误差≤3像素；七人还检查人工框
唯一覆盖及七人的左肩/左腕有效抬臂。没有放宽阈值，也没有构造固定人体结果。

| 实际执行 | 七人 | 一人 | 空画面 | 最大关节源坐标偏差 |
|---|---|---|---|---|
| ORT 恢复图 vs NCNN CPU | PASS | PASS | PASS | raw 最大绝对误差仅 `8.3923e-5` |
| 非量化 RKNN PC simulator vs NCNN CPU | PASS | PASS | PASS | 七人0.08354px；一人0.08556px |
| INT8 RKNN PC simulator vs NCNN CPU | FAIL | FAIL | FAIL | 七人10.224px；一人8.405px |

非量化 raw max（out0/out1）：七人0.02430/0.00849、一人0.04076/0.01422、
空画面0.04913/0.01760；raw mean均小于0.00089。七人最大关节置信度误差约0.00104。
INT8 使用99张真实视频采样帧，排除 golden 帧1500附近±50帧，记录每张图片 SHA；
但校准只覆盖单一视频，不能代表全部现场。INT8 人数仍为7/1/0，raw 精度与
关节坐标却明显超标，所以不能以“能识别人”作为量化模型合格依据。

实际比较 JSON、转换日志、校准 receipt、候选和中间图保存在
`out/rknn-validation-20261009/`。独立 comparison 才表示三样本离线通过；
转换 receipt 的 `deployment_ready/device_performance_verified` 始终为 false。

## OnePlus 的实际验证范围

ADB 实际设备 `e7c07019`、OnePlus9Pro LE2120、Android14，`ro.soc.model=SM8350`、
`ro.hardware=qcom`、`ro.board.platform=lahaina`。这台设备的 Qualcomm NPU
不能运行 Rockchip RKNN；要利用 Qualcomm NPU 需要另一个后端及独立模型/驱动验收。
依据：[OnePlus 官方规格](https://www.oneplus.com/global/support/spec/oneplus-9-pro)、
[Qualcomm Snapdragon888 平台](https://www.qualcomm.com/smartphones/products/8-series/snapdragon-888-5g-mobile-platform)。

本轮安装的仍是已经构建/验证的 `HumanVisionSettingsDemo-RK3588-Optimization.apk`，
APK SHA 为 `8243b3cd2e569a4b759f9df050e539df1978379b8f204d0efdc8935b7a61ec80`，
SDK/native/model未因离线实验修改。实际运行 HumanVisionSettingsDemo：
bundled video 1024×576/25FPS，Low512×288，MaxBodies4，区域关闭，300秒，排除前30秒。

| 指标 | 实际结果 |
|---|---|
| 输入发布 | 24.968FPS |
| 新完成推理 | 23.507FPS |
| 含人体的新结果通知 | 23.309/s |
| 含人体1秒窗口达到20/s | 94.815%，最低17.718/s；不是每秒均≥20 |
| 结果本地年龄 | P50 100.968ms；P95 134.844ms；不含摄像头/网络采集年龄 |
| Unity 平均帧率 | 59.438FPS，独立于骨骼新结果率 |
| app CPU / GPU / PSS | 12.442%所有8核总量（约99.535%单核量）/76.081%/332.18MB |
| 电池 / CPU温度 | 31.3–37.6°C /46.9–60.2°C；缺失的 GPU温度/时钟明确不可用 |
| 管线错误 / 数据质量警告 | 0 /0 |

100个完整稀疏 native 样本：模型执行/内部等待33.398ms，六段总和40.616ms。
真实截图中可见视频、人体框和骨骼；单帧截图不能替代全部窗口的帧率统计。
旧顶部 HUD 的 FPS 项仍显示0.0，本次统计依据下方诊断的新结果计数及文件记录，
没有把旧 HUD 当性能证据。原设备偏好已逐字节恢复，应用已停止。

本轮 session：`session-20261009-064005-517-16530687`。
设备日志文件夹：
`/storage/emulated/0/Download/HumanVisionLogs/session-20261009-064005-517-16530687`。
电脑日志与截图文件夹：
`E:/UnityProject/Human-Vision-SDK-Test/DiagnosticsVerification/Performance-20261009/rknn-offline-vulkan-regression-300s/grade-2/`。
已核对9个共享文件，公开日志均是私有日志的精确前缀，其中8个完全相同、events
公开副本少最后8192字节；额外SDK日志保留在公开目录。

用户随后打开的 Editor 已现场查询：Unity2021.3.45f1、Human-Vision-SDK-Test、
HumanVisionSettingsDemo，isPlaying=false/isCompiling=false，Console errors=0；
UnitySkills当前端口8090。该检查不代表 Editor 正在进行推理。

## RK3588 设备工具与下一步

独立私有测试包：
`E:/UnityProject/Human-Vision-SDK-Test/DiagnosticsVerification/RKNN-Offline-20261009/device-bundle/`。
包含32个经过SHA校验的文件：ARM64/API26 probe、固定 runtime/非量化候选、三个输入/reference、离线
receipt 和全部文件的哈希清单；不覆盖固件全局运行库，不安装/修改 Unity APK。
vendor缓存仅用于本地验证，未加入仓库或公开 Release。

测试包上一级提供`Run-RK3588.ps1`，连接后可在该文件夹执行
`.\Run-RK3588.ps1 -Serial '实际ADB序列号'`。它给出新建日志文件夹路径，调用
相同的SHA/硬件门槛，使用当前电脑的Python3.13及NumPy，不改变Unity工程。

`run_device_probe.py` 在上传前校验 bundle 全部路径/SHA、候选与离线结果对应关系、
输入合同和实际 SoC；OnePlus 的真实拒绝测试没有上传 runtime/模型、没有执行 NPU。
另独立验证 ARM64 probe 可在 OnePlus 启动：无参数时返回usage/exit2，该结果只验证
可执行文件启动。它不能代表 RKNN init/run 成功。

连接 RK3588 后从 SDK 仓库执行（替换实际 serial；output目录必须新建）：

```powershell
py -3.13 tools/models/rknn/run_device_probe.py `
  --adb 'D:\Developer\2021.3.45f1\Editor\Data\PlaybackEngines\AndroidPlayer\SDK\platform-tools\adb.exe' `
  --serial '<RK3588-serial>' `
  --bundle 'E:\UnityProject\Human-Vision-SDK-Test\DiagnosticsVerification\RKNN-Offline-20261009\device-bundle' `
  --output 'out/rknn-device-first-run' --warmup 30 --iterations 300 --cores 1 7
```

默认分别做单核mask1和三核mask7，每组都比较真实硬件的三个输出。
每次记录 runtime/driver、实际输入/输出类型和形状、core设置返回结果、预热与
完成次数、mean/P95/max以及原始张量：`inputs_set`、`run_call`、`outputs_get_wait`、
`output_check_perf_query_release`、`total`、可用时的`vendor_perf_run`。
read-only `/proc/stat`/thermalservice 快照保留缺失/权限原因。每次仅在独立 UUID
目录内测试，保留缓存和失败日志；任何失败都明确失败，无 CPU回退。

这个工具使用重复静态输入，给出的是模型速度；结果里的 SDK performance/deployment
仍为false。单次前后温度快照也不是长期热稳定性证据。完成真实设备精度/驱动/模型
时间检查后，才继续[接入计划](../plans/2026-10-09-rk3588-npu-acceleration.md)中的
RKNN backend、独立 tensor pipeline、双模式 allowlist、UGUI选择和 SDK 长时间
视频/RTSP的新结果性能验收。当前没有可用 NPU 下拉模式或新的生产 Release。

## 自动保护性测试

RED/GREEN：恢复器4项、模拟器3项、native探针4项、bundle3项均先确认缺失实现
失败，再通过；之前已有转换3项及日志分析4项继续通过。
Windows17/17、Linux21/21，真实 host可执行文件包含在Linux失败路径测试中。

```powershell
py -3.13 -m unittest tests.reference.test_rknn_candidate_conversion tests.reference.test_rknn_ncnn_recovery tests.reference.test_rknn_simulator_gate tests.reference.test_rknn_device_bundle tests.reference.test_settings_device_log_analysis -v
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/package/check_public_surface.py
git diff --check
```

Linux多4项native probe测试，需要先构建真实host executable，再设置
`HV_RKNN_PROBE_HOST_BINARY=/workspace/out/rknn-toolchain/hv_rknn_probe_host`。
Android探针实际使用 Unity附带NDK构建 `--target=aarch64-linux-android26`、C++17、
`-O2 -Wall -Wextra -Werror -static-libstdc++ -ldl`。
探针ELF依赖仅libdl/libm/libc；私有runtime依赖liblog/libm/libdl/libc。
public ABI和架构/文档边界均PASS；自动测试不能代替RK3588物理验收。

最终独立只读审查未发现重要问题，复算全部保存的实际输出和bundle哈希通过。
host探针4项仅覆盖init前的参数/文件/动态库失败，不能作为run阶段设备清理验证。
最终测试日志在`out/rknn-toolchain/final-windows-tests.log`、`final-linux-tests.log`。

## NPU 能提升多少 FPS：当前证据允许的估算

用户追加要求量化提升。当前可严谨给出的是条件预算，不能给出未经RK3588执行
验证的非量化NPU FPS。由原现场最长Low段同帧83个完整native样本计算：模型执行/
内部等待85.0384ms，六段总和98.2019ms，其余旧阶段13.1634ms。实际新完成率9.9918FPS，
RTSP本地发布15.3298FPS。这些是既有日志实测，不是NPU结果。

在仅替换模型阶段、其余旧开销不变且输入充足的简化模型中：
`处理能力FPS = 1000 / (13.1634 + 85.0384 / 模型阶段加速倍数 + 新增输入转换ms)`。
这是平均墙钟预算；新路线还可能改变原有等待/预处理/解码并发，不能证明长期P95
或每秒稳定性。下面的2/3/4倍是情景变量，没有测得候选已经具备这些加速倍数。

| 模型阶段假设加速 | 新模型阶段 | 不新增开销时处理能力 | 若另增加10ms输入转换 |
|---|---:|---:|---:|
| 2倍 | 42.52ms | 17.96FPS | 15.22FPS |
| 3倍 | 28.35ms | 24.09FPS | 19.41FPS |
| 4倍 | 21.26ms | 29.05FPS | 22.51FPS |

因此，把当前约10FPS提升到20–25FPS（增加约10–15FPS，最终约2–2.5倍）是
下一阶段目标，不是已确认的预测。无新增开销时，20FPS要求模型阶段≤36.84ms
（≥2.31倍提速）；25FPS要求≤26.84ms（≥3.17倍）。若额外输入转换10ms，预算
收紧为≤26.84/16.84ms，分别要求≥3.17/5.05倍提速。

当前RTSP若保持15.33FPS，所有“无新增开销”的情景最后均受限于约15.33个新输入/
秒，相对当前约10FPS仅增加约5FPS。25FPS本地视频即使模型处理能力29FPS，骨骼
新结果也最多接近25FPS。NPU卸下GPU推理负载可能改善输入发布，但这尚未证实；
上游4K码流/硬件解码、发布同步和新输入转换需要分别测量，不能假定都会随NPU提速。

实际核对[Rockchip官方Model Zoo性能表](https://github.com/airockchip/rknn_model_zoo/blob/main/README.md#model-performance-benchmarkfps)：
YOLOv8n-pose INT8、640×640，RK3588单NPU核55.9模型FPS，约17.89ms；平台使用
最高NPU频率，未指定时不含预处理/后处理。这是姿态任务在此硬件上可加速的直接
依据，但模型导出图/精度/驱动/计时范围不同，不能直接用55.9作为当前SDK FPS。
当前非量化候选只证明离线精度，尚无实体NPU速度；本次全INT8候选已因8–10px误差
被拒绝，不能拿它套用官方INT8速度。官方姿态转换使用特定分支的混合量化，下一
精度优化需针对本图重新选节点并保持原门槛，不能直接复制另一图的节点名。
依据：[官方姿态转换](https://github.com/airockchip/rknn_model_zoo/blob/main/examples/yolov8_pose/python/convert.py)。

条件预算JSON和本次查询的官方源版本/SHA保存于`out/rknn-validation-20261009/`
的`npu-fps-budget.json`、`official-model-zoo-source.json`；在测试项目evidence文件夹
有相同副本。设备工具先量出模型时间、输入交付和输出等待，正式SDK接入后再测
新结果率。这样可判断模型/NPU速度还是输入转换与4K视频链路成为下一瓶颈。
