# RK3588 NPU 宿主启动回归

2026-11-24，基线 `v0.4.0` / `1a7251d9ee9b04348a4bb75361d45e99dba889ab`，分支 `codex/npu-android-startup`。本轮范围为 SDK 修复、本地配套和 Human-Vision-SDK-Test 验证。

用户提供的业务回归报告显示 NPU/Low 模型配置和本地 AAR 的资源闭包与预期一致。新版新增独立 RTSP 控制与原图 live 路径。现场原始 backtrace、故障 APK 中 native 库的 BuildId、实际图形 API 和 RK3588 A/B/C 对照仍待取得。客户现场崩溃的根因待确认。

## 修改

- Android Input live 对 Vulkan instance、physicalDevice、device、graphicsQueue 和 loader 入口逐个校验，同时校验已导入的 Unity 目标图像。loader 在空值时返回错误。
- `BeforeReset`、`Shutdown` 使旧设备代次失效，下一次初始化取得新代次。GPU fence 查询、提交和对象销毁受该代次保护。Unity 可延迟执行 AccessQueue 回调，回调结束前保留引用。无法确认 GPU 已完成时，隔离对应旧资源并记录错误，恢复可能需要重启进程。
- live 渲染失败通过 `PreviewError` 上报，分析错误仍从分析状态和诊断读取。明确关闭预览时直接读取源状态，不去挂接可选的 Android live 桥，B 组可隔离新增原图预览。
- 验收 APK 新增显式 `hv_npu_startup_case` 模式。A/B/C 均使用真实 SDK 初始化、单一 RuntimeView 和相同 Neural/Low 合同。启动日志记录选项、实际状态、首输入、首原图、首结果、错误与可获取的模块映射。A/B/C 之外保持验收 Demo 原本入口。

## 可复现验证

| 检查 | 结果 |
| --- | --- |
| Vulkan loader 防御 | 旧 loader 的无 device/instance 合成回归失败，无 proc 测例出现条件性空调用。新 loader 定向 9/9，NDK `aarch64-linux-android26` 生产头包含及实际 UnityVulkanInstance 类型编译通过。 |
| Input Windows host | `ctest --test-dir out/input-native/windows -C Release --output-on-failure` 74/74，覆盖设备代次和延迟队列引用。Host C ABI 测试不执行 Android RenderLive。 |
| Unity Skills 8091 | 旧 PreviewDisabled 测例预期挂接 0 次实际 1 次。更新后该 fixture 5/5，实际 EditMode 回归 151/151，0 失败、0 跳过。 |
| 8091 Play Mode | RuntimePrefabAcceptance 实际 RTSP 1920×1080，纹理帧 469→943、像素哈希变化，4 人 96 点 / 92 线，SDK 错误为空。当前 Windows Editor DLL 为既有 33b637…，Input 385efc…，本轮修改的 Vulkan 桥在 Android 库内。 |
| ABC 诊断参数 | 实际 SDK/Unity 2021.3 程序集下 Editor 和 Android 条件分别编译，纯参数与日志编码各 42 项检查通过。未在这些检查中启动 NPU。 |
| Android native | ARM64/API26 新构建 SDK RKNN-ON 与 Input，9 SO 强依赖审计、Input 原 56 个 `HV_Input_*` 导出一致、74 项原模型/profile 哈希一致。 |
| Unity Android APK | 使用 8091 对应项目构建 RuntimePrefabAcceptance，0 构建错误、1 警告。APK 和已安装 APK 的 SHA-256 为 `f9455d9415664b057d709a39b2ffaa3a8fe0c0d55d0b8d4066d1c137a9dde551`，包内核对了 9 个 ARM64 SO、74 个资源和启动诊断入口。 |

实际回执位于 `out/npu-startup-20261124/`，loader 原始证据位于 `out/startup-vulkan-loader-20261124/`。本地成套 tgz、版本标记、库 SHA-256 和 BuildId 位于 `out/npu-startup-20261124/native/candidate-r1/`。该本地候选使用独立候选名与可核对来源，公开 tag 保持不动。

## Android Vulkan 预览冒烟

同一台一加 9 Pro（LE2120）、同一 1080p RTSP 样本、同一候选 APK，独立输入和 Vulkan GPU 低档分别冷启动预览开关两组。两组的来源及代次均稳定，原生输入和 Runtime 错误均为 0，采样日志未见该进程崩溃。

| 预览 | 连续采样 | 原图发布累计 | Unity Texture | 原生新结果 / Unity 交付累计 |
| --- | --- | --- | --- | --- |
| 开启 | 7 点，跨度 6.13 秒 | 109 → 289，1920×1080 | 1920×1080，观测 19.36–23.22 FPS | 32 → 167 |
| 关闭 | 6 点，跨度 5.14 秒 | 持续为 0，分析帧 100 → 254 | 0 FPS | 20 → 129 |

首次关闭预览的采样受手机休眠中断，复跑唤醒后取得上表结果。回执为 `oneplus-render-smoke.json` 与 `oneplus-awake-preview-{on,off}-logcat.txt`。这是 Android Vulkan 启动和预览隔离验证，RK3588 Neural/Low 及客户宿主启动仍待验。

另外冷启动开启预览复测，44 个运行采样跨 44.14 秒，持续有 1920×1080 源原图，实时绘制观测 92–96 点、86–92 条线。最后统计为 1424 张原图和 1139 次 Unity 新结果交付，来源及代次稳定，Input 和 Runtime 错误均为 0。证据为 `oneplus-preview-on-confirmation.txt`。正常实时预览关闭精确配对，所以日志里汇总精确配对的 `bodies` 和 `skeleton_fps` 为零。实际绘制读取 `joints` 和 `bones`。

## RK3588 对照判定

用故障的同一台 RK3588、同一显式 RTSP URL、相同 TCP、镜像、模型和资源，分别冷启动 A（独立并预览）、B（独立关闭预览）、C（兼容并预览）。每次保存完整 logcat、可取得的 tombstone、APK SHA 和实际 SO BuildId。通过条件是 A 组持续有实际原图与有效骨骼，且能正常退出、再次启动。完整命令和证据口径见[启动对照诊断](https://github.com/blaze-tc/Human-Vision-SDK/blob/v0.4.1/tools/test/NPU_STARTUP_DIAGNOSTICS.md)。
