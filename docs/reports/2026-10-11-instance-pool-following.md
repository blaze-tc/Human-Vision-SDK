# 2026-10-11 单实例、画面时间线和 OnePlus 实测

本节取代下方早期多实例启用计划。用户已明确要求放弃导致掉帧的多实例方案。
CPU、Vulkan、RKNN 的当前组合入口均固定单实例；UI 与自动对照不再提供多实例阶段。
底层实验池与回归测试保留，但不由当前入口启用。显示 1/1 表示真实忙碌/创建数。

## 为什么输入 FPS 也会下降

界面输入数来自发布帧 ID 的每秒增量，而不是摄像头标称帧率。Android RTSP 在
RtspFrameSource.Update -> AndroidRtspGpuSource.Tick 中驱动渲染事件并轮询可用 GPU
纹理，只有成功发布才递增 FrameId。GPU 转换/拷贝、有限纹理槽和 Unity 更新调度
都会影响这个数。视频示例实际是 25 FPS，不能当作 30 FPS 源。

同设备 RTSP 仅预览阶段 60.007 秒发布 1768 帧，约 29.46 FPS。下面的 Vulkan
实验使用本地 25 FPS 视频；开启推理后发布速率下降说明异步链路仍有共享资源
竞争。不能仅凭发布计数分配网络、解码、GPU、主线程各自的损失比例，也不能用
重复帧提高输入计数。预览应独立流畅，识别按能力处理最新帧。

## Vulkan A/B/A/B 证据与边界

session-20261011-103140-048-ff7d3e10；统计只取运行且有人体的快照，中位数。
这些是全局新结果事件率，不是每人完整骨骼验收结果。

| 阶段 | 热状态 | 新结果 FPS | 发布输入 FPS | Unity FPS | 原生后端采样 ms |
|---|---:|---:|---:|---:|---:|
| 单实例，早期 | 0 | 21.49 | 24.78 | 50.33 | 43.02 |
| 双实例 | 3 | 10.70 | 21.62 | 29.34 | 107.26 |
| 单实例，热机 | 3 | 12.80 | 23.19 | 32.64 | 73.89 |
| 双实例，热机 | 3 | 9.77 | 19.08 | 27.19 | 135.95 |

原生分阶段样本按 UTC 去重。热机重复对照中双实例比单实例更慢，并增加同步等待、
GPU 忙碌比例和内存。初次 21.49 到 10.70 的全部差值不能归因于实例数，因为热
状态同时变化。系统 dumpsys thermalservice 确认 Thermal Status 3（非 override），
机身传感器约 45°C；GPU 频率读数被系统拒绝，不能编造具体降频幅度。单实例
GPU 提交等待采样约 37 ms 与 66 ms 两档，对应明显不同吞吐。不能证明每次短时
20 到 12 的变化都由热限制单独造成。NPU 多实例未在 RK3588 对照，按用户决策
同样退回单实例，不宣称从 OnePlus 证明所有 NPU 并行都不可行。

## 跟随与闪烁处理

- 8 个有界独立显示快照；源纹理在 GPU 复制完成后释放。显示历史承担延迟，
  不让推理一直占用解码纹理。Unity 2021/OnePlus 使用 CPU synchronization fence。
- RenderTexture 快照复制采用匹配 descriptor 的 CopyTexture；同尺寸 Blit 在
  设备像素检查出现差异，因此已替换。非兼容纹理保留有检查的回退。
- 显示时间线单调前进，保留源/代/尺寸身份；源变化清空历史。
- 原 80 ms 显示保持窗口低于低帧率结果间隔，会在两次结果之间清空而闪烁。
  改为最多 300 ms 有界保持。同身份可做显示插值；不同身份在下一观测时原子
  切换；显式空结果和超时清空。原始 SDK 结果及真实新骨骼计数不变。
- 展示 input/result/display/pose-before/pose-after 帧号和实际显示延迟。
  80–200 ms 是请求的自适应目标，输入/调度停顿可能使实际延迟更大，不能作
  200 ms 的端到端保证。

## 验证

- 最终原生全回归 443/443；Android API26 依赖检查通过。
- Unity PresentationPoseHistoryTests 4/4（61dac0dc），包括 8 Hz 观测、16 ms
  显示采样、身份切换、空结果、超时和原始数据不变。实例计数测试 5/5。
- OnePlus 中间单实例包（与最终包同一 C# 显示实现）四档显示快照 × 镜像
  8/8 像素完全一致；模型 RGB 检查 6/6 完全一致。
- session-20261011-110554-008-8afe24c5 的 Video/source12 有 171 个快照：
  发布输入中位 24.65 FPS，新结果 20.93 FPS，Unity 49.51 FPS。显示模式
  interpolated=160 / observed=3 / held=5 / unavailable=3。1 Hz 采样不等于
  连续渲染的无闪烁证明，也不能将显示插值计入识别帧率。
- 10 张设备截图抽样显示四人骨骼随动作对应；设备 screenrecord 崩溃，未取得
  连续录像，不能称为连续视觉或持续 20–25 FPS 验收。
- 最终 APK: HumanVisionSettingsDemo-SingleFollowing.apk；SHA256
  a36088df9feba55bacca21e3302425477388d5f90d7ee3db30dd587e41beb136。
  原生 SHA256 6c3730b675389931453e0d5f4eebe7c76ae87d9104495fc1ed4993456ac3084e。
  47 个运行时索引文件及 4 个图候选文件逐项哈希检查通过，ADB 安装成功。

最终包烟测 session-20261011-112753-853-6749fb6a：Video 174 个有人体快照，
输入中位 22.62、新结果 12.88、Unity 32.71 FPS；RTSP 160 个有人体快照，输入
14.77、新结果 12.78、Unity 34.40 FPS。均实际单实例。热机状态下性能目标未通过。
RTSP 有一次断线/自动重连，随后恢复；不能把整个阶段的损失只算给 GPU。
最终截图确认本地视频四人骨骼和 RTSP 人体骨骼可显示，未完成连续视觉验收。

最终 native 日志 48 个稀疏 gpu_color_completed 样本：decoded_us 到 submitted_us
中位 12.54 ms，submitted_us 到 converted_us 中位 49.31 ms（最大 79.167 ms）。
后者是提交到完成的墙钟时间，包含排队/同步，不能写成着色器纯执行时间。这比
仅看摄像头标称帧率更直接地证明了 RTSP 纹理转换/发布路径存在等待。尚无等量
逐帧网络、解码、Unity 调度完整轨迹，不能对各项损失作精确百分比分摊。

RK3588 实测、逐人完整骨骼持续目标、RGA 直连与长时间热稳定性仍未验收。
原始记录：out/four-path-20261011/；可交付证据另同步到测试项目
DiagnosticsVerification/SingleFollowing-20261011/。这不是公开 Release。

---

# 联合优化：供帧、RKNN I/O、模型图与实例池

本轮目标是新骨骼结果及时跟随独立更新的预览。Unity 渲染 FPS、重复画面、
插值次数和 NPU 利用率都不能代替逐人真实骨骼帧率。本轮未取得 RK3588
实测，20–25 FPS 仍为目标，不能据离线通过推算为已经达到。

## 变更及默认行为

| 项目 | 已实现 | 验证边界 |
|---|---|---|
| 供帧 | 原生捕获时间额度替代固定间隔；Unity 暂时拒绝提交后重试最新帧；一次接受只处理一次、输入计数不重复 | 抖动门槛测试、真实 Unity GPU 回读测试；预览不等待推理 |
| RKNN I/O | 原生 FP16 输出候选；显式缓存维护和默认维护分开；输入输出内存复用 | 查询类型/维度/密集步幅，完整输出同帧比较及原有至少 5% 总 backend 提升门槛；不支持、数值不符、异常或更慢均回退 |
| 模型图 | 两个输出头各三个 Transpose 合并为尾部一个 Transpose；权重不变、输出合同不变；额外候选与原模型同包 | ORT 图变换等价；真实 RKNN 编译和原有七人/单人/空场景 PC 模拟器门槛通过；不是设备速度证明 |
| 实例池 | 显式 1/2/3 个独立 pipeline/backend；单实例三核基线与多实例分核对照 | 最多三个处理中实例加一个最新待处理帧；源帧与时间单调发布；迟到结果丢弃；创建失败清理并记录回退 |
| RGA | 实际解码 AHB 格式/步幅/usage、库可访问性、平台 AHB 导入符号诊断 | 仍未接通 RGA→NPU；不是零拷贝实现，也没有性能通过声明 |

## 为什么实例池可能提速而不保证减小延迟

若每次独立推理为 50 ms，且设备确实可同时以原速运行两个实例，理论吞吐
可由 20 次/秒提高到 40 次/秒；单张画面的推理延迟仍至少约 50 ms。
实际 RK3588 已请求三核，因此把模型分到单核可能增加单次耗时，DDR、缓存和
温度也会影响结果。不能用实例数乘以前一轮 FPS 作为预测。

Host 不包含 RKNN 类型；私有实验在 composition 中选择核 1/2/4，未更改
公共 C ABI。人物跟踪仍在共同的服务层，独立模型不各自分配公开人物 ID。
先完成帧 102 后完成帧 101 时，101 不得覆盖 102，也不计为新的已发布骨骼。
停止会等待在途实例退出，销毁所有上下文；每帧不新建线程或模型。

## 同一安装包的自动对照

HumanVisionSettingsDemo 的「一键自动对照」按设备能力顺序执行：

- 固定像素检查与 RTSP 仅预览。
- RK3588：视频、RTSP 各自执行原基线、I/O 候选单实例、双实例、三实例、
  图优化候选单实例、重复基线；最后长时运行。没有创建出请求数量的实例时，
  该阶段记录 UNAVAILABLE，不把回退单实例伪装为并发结果。
- OnePlus：明确跳过 RKNN，执行视频及 RTSP 的 CPU/Vulkan 与长时视频回归。
- 结束/取消后恢复环境变量与原配置；测试不写入保存的用户偏好。

逐人日志保留源、generation、StableTrackId、真实新结果/完整 COCO-17 数量、
局部结果年龄及阶段耗时。用 `tools/benchmark/analyze_combined_benchmark.py`
按完整测量区间统计，预热、重复基线的不同区间、未完成阶段不能混算。

私有环境变量只供测试，SDK 公开 API 不暴露它们：

| 变量 | 值及语义 |
|---|---|
| `HV_RKNN_INSTANCE_COUNT` | `1` 默认；`2`/`3` 显式分核实验 |
| `HV_RKNN_MODEL_CANDIDATE` | `baseline` 默认；`folded-output` 选择严格固定 SHA 的附加候选 |
| `HV_RKNN_INPUT_MEMORY_MODE` | `legacy` 基线；`auto` 原有同帧资格检查及回退 |

任何候选原型都不是新 Release。私有 RKNN 模型与 vendor 运行库不能因这次
对照而被视为可公开分发。

## RGA 的明确限制

核对官方 librga 提交 `2b32edcb97b601b25683e2941d888c8515da6d55`：
Android 平台头 `im2d_expand.h` 需要平台 `ui/GraphicBuffer.h`，其文档列出
`importbuffer_AHardwareBuffer`；官方 AndroidNdk ARM64 库未导出该符号。
当前 SDK 解码为 PRIVATE AHardwareBuffer，不能拿到一个 fd 就假定线性 NV12。

新增 `HVInputGate` 的 `rga_capability` 行在实际解码后记录库加载错误或符号
可见性，以及真实 AHB 描述。即使符号存在，仍需实际 buffer import、modifier、
fence、像素/骨骼精度验证，日志固定报告 `enabled=0`。后续必须根据 RK3588
系统 BSP 与此能力结果接入；本轮没有通过降低安全或精度约束强行启用。

来源：[官方平台扩展头](https://github.com/airockchip/librga/blob/2b32edcb97b601b25683e2941d888c8515da6d55/include/im2d_expand.h)、
[官方 Android NDK 库](https://github.com/airockchip/librga/tree/2b32edcb97b601b25683e2941d888c8515da6d55/libs/AndroidNdk/arm64-v8a)。

## 软件证据

- `tools/test/run_native_tests.ps1`：436/436；包含原生 ABI、架构、Host 并发、
  初始化失败清理、乱序、FP16 全码空间、图候选选择等。
- `tools/package/build_input_native.ps1 -Platform Android -RunTests`：ARM64 编译、
  API26 依赖检查及输入宿主回归 49/49。
- Unity `HumanVision.Tests.InputAdapterTests`：22/22；暂未准备好的输入重试测试
  先 0/1 失败，再修复后通过；覆盖原有分辨率与真实异步回读行为。
- 图变换 Python 测试 3/3；固定候选 staging 2/2；对照日志分析器 2/2。
- Android SDK ARM64 编译与实际动态依赖闭包 506 强导入审计通过。
- `check_architecture_boundaries.py`、`check_public_surface.py` 通过。

原始证据保存在工作树 `out/four-path-20261011/`。设备回归另附实际测试记录；
构建成功本身不代表视频、跟随或 RK3588 性能验收。

## Follow-up: stable person selection and instance counters

Full-frame CPU RTMO and RKNN YOLO now admit bounded candidates before common
tracking/public selection; Vulkan YOLO already admitted them. Stable display slots
use normalized height plus measured COCO completeness. Rank smoothing, an 0.08
improvement margin sustained for 500 ms, and 350 ms loss grace prevent minor changes
from continually changing the selected people. Only the weakest incumbent can be
upgraded. Raw results remain fresh-only; region binding is preserved. These
full-frame routes add no per-person model executions. This is an apparent-depth
heuristic and does not guarantee identity through all occlusions.

UI and each pipeline snapshot include actual busy / total inference instances,
explicit unavailability and fallback reason. GPU busy brackets process_gpu;
CPU/RKNN counts active pipeline calls including waits. This is not utilization.

Native full regression 440/440; Unity instance tests 5/5 (8d8eadca, after compile);
Android build and API-26 closure 506 imports passed. Native SHA256:
8e85512152cd6854645f6c36ceeeb8ee550eb202abbeaca2ce93750917285df9.

## Earlier OnePlus comparison, before stable selection

Session session-20261011-082716-563-d6e2ce0e completed four 60-second video/RTSP
CPU/Vulkan measurement phases. The 300-second phase was cancelled; it is NOT an
endurance pass. No collector/Unity errors in the collected snapshot. RTSP Vulkan
completed 196 -> 1679 model executions in 60.015 s (~24.7/s). Its longest-lived track
measured 23.45 fresh observations/s but zero full COCO-17/s in the limited camera
framing. Do not call this complete-skeleton acceptance. Video identity/visibility
changes prevent global event FPS from establishing stable per-person performance.
CPU remains substantially slower in these scenes. Raw logs/analyzer output:
out/four-path-20261011/phone-baseline*. These observations do not qualify RK3588
or the later selector. Latest combined APK device verification follows separately.
