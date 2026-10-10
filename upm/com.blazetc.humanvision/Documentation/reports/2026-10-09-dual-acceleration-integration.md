# 2026-10-09 私有双模式集成与实测

用户要求先完成真实 NPU/Vulkan SDK、在已连接 OnePlus 验证通用功能，再自行在
RK3588 验证。这覆盖原先先硬件后集成的顺序。当前非量化候选只通过七人/一人/
无人三组离线数值比较；RK3588 驱动、真实精度、持续速度与温度仍需实测。
失败的 INT8 候选未纳入。该路径输出模型实际的 COCO-17 点，不生成手指点。

## 设置与 API

HumanVisionSettingsDemo 独立显示计算模式、模型等级和输入分辨率。
Windows 的 GPU/CPU 使用现有 `UseWindowsCpu` 配置；Android 的 NCNN Vulkan/
RK3588 NPU/CPU 使用 `AccelerationMode`（Graphics=0、Neural=1、Cpu=2）。
CPU 复用已存在的 `android-cpu-nohands`：最多两人使用 precision-t-26，三至八人
使用 rtmo-t-416；真实模型尺寸随容量显示，不提供虚构高/中/低选择。
默认 Vulkan，NPU 只允许已校验的 Low
512×288、16:9 输入合同。PC/OnePlus 不支持 Rockchip NPU，选择后明确提示原因。

代码启动仍使用总控的协程生命周期：复制当前完整 `HumanVisionSdkOptions`，
设置 `AccelerationMode = HumanVisionAccelerationMode.Neural`，然后
`yield return sdk.Initialize(options)`。检查 `IsRunning`、`LastError`、
`RuntimeProfile`、`ActiveModelPack` 与 `RuntimeDiagnostics`，避免把下拉框草稿当成
实际后端。切换会先检查资源与硬件、后台创建候选、再退役旧会话并接入输入。
创建失败保留现有会话；不会静默切回 Vulkan 后仍标为 NPU。

NPU 改变人数容量需要用更新后的完整 options 重新调用异步 `Initialize`。
同步 `TrySetMaxBodies`/识别设置请求若需要重建 NPU 会话会返回失败并保留现有
输入与会话；人数未改变的区域设置仍走原有安全更新路径。源帧年龄包括异步
读回排队时间。NPU 原型使用受限异步读回，并没有宣称 DMA/RGA 零拷贝。

## 日志判读

在设置场景点击“复制日志文件夹”，Android 的普通目录为
`Downloads/HumanVisionLogs/session-...`，提供整个会话文件夹即可。
`session.json` 标识 APK、模型和实际设备；`performance-*.csv` 记录输入发布、
已完成结果和新人体结果速率；`timings-*.jsonl` 给出 SDK 阶段与帧号。
`hardware*.jsonl` 包含 CPU/GPU、内存、频率、热状态及 NPU 三核负载/频率/温度。
NPU 启用标记来自已初始化的实际 profile；驱动权限不足以 -1 和原因表示。
驱动负载是整机采样区间的数据，不是本应用独占利用率。

NPU 的 `runtime.diagnostics` 周期记录实际 backend、runtime/driver、core mask、
初始化、`input_set_ms`、`execute_ms`、`output_get_ms`、`output_release_ms`，
以及 Body pre/infer/post。它们是最近一次诊断快照；不要与另一帧的 SDK total
相减，或将包含等待的 wall time 当作纯 NPU 计算时间。输入读回与 RKNN 输入
交付的成本也要计入端到端预算。

## 验证范围

专项 EditMode 103/103、OverlayGeometry 11/11 与发行依赖 PlayMode 17/17
均通过、无跳过；覆盖准备会话
的区域版本、CPU/Vulkan/NPU 选择、后台销毁、停止回调重入保护、CPU 人数
2→3→2 的真实模型与几何切换、输入时间戳与结果年龄。原生全量回归 407/407；
实际 Android ARM64/API26 动态依赖闭包通过；Python
私有打包与既有离线工具 22/22。Windows 发行配置重新构建并保留全部 25 个旧
导出，增加后共 39 个，保留原 DirectML 和 RTSP 依赖。模型/厂商库与 APK 保留
在本地实验目录，未作为公开 Release 资格证明。

完整隔离工程历史测试有 19 失败、34 跳过，不能宣称全套 Unity 测试通过。
已修复的设置布局、元数据与生命周期问题由上述专项结果覆盖；缺失媒体、
旧包和旧 GPU 路由断言未成为本次设备验收证据。

实际测试工程 Unity2021.3.45f1 构建成功，0 错误、8 警告。新实验 APK
SHA256 为 cc65b79376c9ac4a23e6df37d91e56f3ab7df91808a9632899111795ddee2d56。
APK 压缩文件内 43 项索引资源、index/安装保留收据及四个 ARM64 SDK/输入/ORT/
RKNN 库逐项匹配本次构建。包为 282647590 字节；BuildReport totalSize 是
901929097，不能将其当 APK 文件大小。OnePlus 安装成功。该性能包沿用了旧的 native_tests 身份描述，随后只修正
资源内的测试说明，最终 APK 为 fa0db69b23aff9463b68eb5777fbee86e06c4f497be1d79a0c2809903d29a00c
（282647754 字节、2:44 构建、0 错误/8 警告）。两包 libil2cpp.so、四个
推理/输入库和模型索引字节相同。下列持续指标绑定 cc65 性能包；最终身份包
另外进行安装与功能复测，不能混称为同一个 APK。
旧的 8243 APK 成绩未作为新包成绩。cc65 性能包的 300 秒视频 Low/4 人：
预热 30 秒后发布 24.975 FPS、完成 23.624 FPS、新人体结果 23.276 次/秒，
97.33% 有人体的一秒窗口达到 20，P95 本地结果年龄 134.99 ms，无管线错误。
CPU 固定 rtmo-t-416/4 人两分钟：发布 25.001、完成/新人体结果 5.165 次/秒，
P95 年龄 515.93 ms，无管线错误，真实四人画面和骨骼可见。
RTSP Vulkan Low 两分钟：发布 16.851、完成 16.376、新人体结果 16.245 次/秒，
P95 年龄 166.66 ms，无管线错误。RTSP 使用 USB reverse 的本机 25 FPS 源，
不能视为 RK3588 局域网成绩；尚未达到 20–25。
以上预热从各 Running/Streaming 连续段开始计，按完成/发布计数增量计算。
视频采样中 force-stop 丢失一条 hardware 尾部半行，分析器明确报告；CPU/RTSP
数据质量警告为零。新人体结果是含人体的新通知，不是每人完整 32 点帧率。
CPU 与 Vulkan 模型/点集不同，不能视为等质量性能对比，也不能证明 NPU 速度。

最终 fa0 包已安装到 OnePlus。150 秒界面复测从实际 CPU 会话切换到实际
Vulkan Medium，会话源随成功切换重新建立，两路均有真实人体骨骼；该短测试
用于功能验收，不作为 Low 稳态性能成绩。最终包另测 RTSP Low 两分钟：发布
16.883、完成 16.288、新人体结果 15.920 次/秒，P95 年龄 150.10 ms，无管线错误。
选择 RK3588 NPU 被 SM8350/qcom 硬件检查拒绝，原 Vulkan Low 的 source/generation
保持 1/1、实际 profile 不变，结果继续推进，画面和骨骼继续显示，NPU 未启用。
这是不支持硬件的安全处理验收，不是 NPU 推理验收。

日志身份与尾部说明：场景继承的顶层 `inputPackage` 仍显示旧 fix2 标签；分析
应以 `deviceBuildInfo.input_package=0.1.0-preview.4.rk3588fix3` 和
`input_native_sha256=e6b712f7b5e2ca4aa2602c7a1e6af3e659e01dd6d34a54409e89239c255fd0ed`
为准。最终 CPU→Vulkan 功能采集的 events 文件有一条 force-stop 尾部半行；
性能分析器不读取 events，因此它的警告列表不能代表全部文件完整性。原始
文件保留该半行，未伪造补齐。最终 RTSP 各类 JSON 行完整。
旧 SDK 状态栏的 Vulkan 输入/推理 FPS 仍为 0；性能判读使用常驻诊断栏和
上述 fresh/processed/published 计数，不能用这个旧状态栏认定推理未运行。
所有采集结束后恢复原设置文件的精确字节并停止应用。

本地交付：`Builds/HumanVisionSettingsDemo-Vulkan-NPU-Experimental.apk`；
原始会话、截图与操作记录在测试工程 `DiagnosticsVerification/Dual-20261009/`。
测试结果及离线通过只覆盖本文明确范围；实验 NPU 不生成真实 Handtip/Thumb，
物理 RK3588 驱动、精度、持续速度和热状态仍未验收，未创建公开 Release。

## RK3588 实测顺序

先安装本地实验 APK，在视频输入使用同一 16:9 视频、Low 和相同人数配置。
各模式至少预热 30 秒再测 300 秒，保存实际模式、画面骨骼和完整日志。
然后测试同一 RTSP。若 NPU 初始化失败，保留具体 runtime/driver/错误码日志。
独立 probe 的 mask1/7、三组原始输出与数值比较继续用于判定驱动兼容和模型
精度，不能只凭界面能选 NPU 宣告完成。以新完成结果计数衡量 20–25 FPS，
同时检查 P95 年龄、输入发布和热降频。
