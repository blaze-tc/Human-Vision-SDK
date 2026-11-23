# SDK 独立运行候选的本地验收

适用范围：2026-11-23 本地未发布候选。使用 `Human-Vision-SDK-Test` 的既有
`Assets/HumanVision/Samples/RuntimePrefab/RuntimePrefabAcceptance.unity`；不要用公共样例覆盖这个工程的私有 partial 扩展。
当前活动状态和最终证据以 [DEVELOPMENT_STATUS](../DEVELOPMENT_STATUS.md) 为准。
接口用法见[配置说明](../user-guide/SDK_CONFIGURATION.md#原生独立运行候选)，原生所有权见[控制合同](../../runtime/composition/RUNTIME_CONTROL.md)。

## 验证什么

`CompatibleUnityInput` 保留默认行为。原生 RTSP 模式由控制线程启动 source，复用现有 Session/Host 完成输入、预处理、推理和 canonical 发布；
Unity 只在合法线程读取快照、发出 `ResultUpdated` 和上传/显示预览。VideoPlayer、WebCamTexture、Unity 游戏纹理仍是异步兼容来源；
`IndependentRequired` 当前拒绝尚无原生 driver 的组合。

Windows 的原生 CPU RGBA 可以送 CPU 或 DirectML。Android 的原生 Vulkan 图像服务可以送 GPU，或经自己的 staging 回读送 CPU/RKNN。
NPU 不必等待 RGA 才能使用共同的异步调度。RGA 及共享设备内存是另外的设备优化门槛。
独立线程或逻辑设备不会隔离物理 GPU、内存带宽、功耗和散热。

验收时分开记录：

| 量 | 使用/核对来源 | 不代表什么 |
|---|---|---|
| 原生接纳输入 | `NativeInputFrames`、`InputFrameId` | 解码包率、原始传感器 FPS |
| 新 canonical 观察 | `NativeFreshFrames`、`FrameId`、source/generation/epoch | 重复读取、插值、逐人完整骨骼 FPS |
| Unity 消费/绘制 | probe 的 callback、delivery、render 计数 | 后台推理实际吞吐 |
| 结果年龄 | Runtime 单调时钟下的本地 decode-to-query 年龄 | 未验证的传感器端到端延迟 |
| 原始/分析/预览尺寸 | StatsV3 和实际预览 metadata | 请求尺寸；未知原始几何不能填成已知素材尺寸 |

基础 Demo 的实时叠加显示当前预览和同一来源身份下最新仍有效的骨骼；画面与骨骼不要求来自同一帧。
精确同帧模式必须另外核对 source/generation/frame/config epoch 和像素身份，实时叠加可见不能替代精确同帧验收。
当前基础交互、实际画面和骨骼仍待 Unity 验证，不能用原生合同测试通过代填 UI 通过。

## 准备一致的候选

1. 保留项目源文件、插件、meta 和 PlayerSettings 的备份。SDK 与 Input 必须来自兼容的一组候选。
2. 使用 [stage_runtime_acceptance.py](../../tools/test/stage_runtime_acceptance.py) 从候选 checkout 导出 managed/shader/asmdef，并保存 receipt。
   默认是预览；显式 `--apply` 才写入工程。脚本跳过所有 Samples，保留已有 GUID，异常 GUID 差异会被拒绝。
3. 使用 [stage_independent_probe.py](../../tools/test/stage_independent_probe.py) 将探针加入现有 RPA，而不是替换 Demo。
   它保留备份，增加原生计数 readiness/watchdog、15 FPS、最长 3600 秒阶段和可控停顿。
4. 单独替换匹配的 native 文件并记录 SHA-256。Windows 的 `humanvision.dll` 必须搭配同一构建的
   `humanvision_onnxruntime.dll`；启用 DirectML 的构建还需其固定私有 `hv_dml.dll`。不要混用 CPU 与 DirectML 构建的 ORT。
5. Unity 一旦加载 native DLL，managed 重新编译不会卸载旧库。确认测试场景无未保存修改，关闭该测试 Editor，换入文件后冷启动。
   [重启辅助脚本](../../tools/test/unity/RuntimePrefabIndependentEditorRestart.cs) 只处理指定测试工程，脏场景会拒绝退出。不要关闭其他业务 Editor。
6. 等编译完成，先运行 `HumanVisionIndependentRuntimeTests`、`InputAdapterTests` 和 graphics Editor 中的
   `NativePreviewOwnershipTests`。测试名/实际数量必须与结果一致，零匹配或只通过容器不能算 fixture 通过。

测试工具不使用 Windows 屏幕捕获；进度写入独立的完整日志，避免 Console 对长消息的截断。
探针只是消费者：在 `Thread.Sleep(500/2000)` 紧邻两端重新查询原生计数，不用缓存计数证明后台前进。

## 原生无 Unity 闭环

构建 [hv_independent_runtime](../../tools/benchmark/independent_runtime_harness.cpp) 后，命令参数为：

```text
hv_independent_runtime ROOT PROFILE INPUT_LIBRARY RTSP_URL SECONDS [HARDWARE_AVC_CODEC]
```

使用已有有效 Runtime 根目录、真实 profile/ModelPack 和同源实时 RTSP fixture。Windows Input DLL 路径必须是绝对路径。
CPU 构建可用 `cpu`；DirectML 需要单独启用 `HV_USE_DIRECTML` 的构建，并核对输出实际为 DirectML，不能把普通 CPU 测试通过当作 GPU 通过。
程序在观察窗口只读累计统计，不调用 Copy、journal 或 Unity；退出还检查真实 close/release。
保留库、模型、profile、素材 hash 及实际 source/image-service/backend。不要用不同 provider 版本或模型的数值计算改造提升率。

## Unity RPA 主线程停顿

[run_unity_independent_runtime.py](../../tools/test/run_unity_independent_runtime.py) 通过 UnitySkills 控制既有测试工程。
当前脚本是 **native CPU Editor gate**：readiness 要求原生进度，stall 控制校验期待 CPU；不要把其 `--execution compatible` 参数当作已实现的旧会话 A/B 验收。
它不启动 RTSP server/publisher；先提供持续播放、按真实帧率送流的同源 fixture。`--source` 用于记录对应素材 hash。

以下参数组合用于本地原生 CPU 停顿检查；工程和素材路径按当前验收环境给出，输出目录必须是新目录：

```powershell
py -3.13 -B tools/test/run_unity_independent_runtime.py `
  --project "E:/UnityProject/Human-Vision-SDK-Test" `
  --output "out/independent-acceptance/native-15-stall2000" `
  --fixture-url "rtsp://127.0.0.1:18573/Exercise-11.mp4" `
  --source "E:/UnityProject/YS-Sensory game project/测试资源/Exercise-11.mp4" `
  --seconds 20 --warmup 4 --target-fps 15 --stall-ms 2000 --preview-off
```

脚本确认 `/health` 的工程名和已保存的 RPA 场景，等待实际 streaming/fresh，再采样完整窗口；输出包含 receipt、native 库 hash、完整 progress log 和 Console 错误。
source/generation/config epoch 改变、计数倒退、未知计数、缺失停顿标记或请求控制不符都不能算通过。
有效停顿应观察到原生 input/fresh 增长，Unity callback/delivery/render 不增长，原生错误不增长。

短功能检查通过后，分别做 15/30/60 限帧、500/2000 ms 停顿、预览开关、慢/不读消费者及独立 CPU/GPU 压力。
`--cpu-workers` 和 `--gpu-passes` 施加测试负载；检查实际 probe 字段确认负载生效，不能只看请求参数。
长时检查按设计使用 300 秒预热及 1800 秒持续窗口，另外核对最慢 10 秒、年龄 p95、drop、温度和频率。
保留原始日志及运行条件；一次短窗口不能替代整个矩阵。

## 已观察证据与未通过门槛

下列是中间候选的功能证据，**不是最终 head 的性能 SLA**，也不能证明逐人完整骨骼 30 FPS：

| 本地证据 | 观察 | 边界 |
|---|---|---|
| `out/independent-runtime-native-20261123/cpu-1` | 约 20 秒，514 个原生输入、222 个 fresh；正常退出，无已记录输入/运行时错误 | CPU profile；不是 UI 或设备验收 |
| `out/independent-runtime-native-20261123/dml-1` | 约 20 秒，530 个原生输入、325 个 fresh；实际 DirectML | 不同 provider 构建，不能与上一行做性能 A/B |
| `out/unity-independent-rpa/native-nopreview-15fps-stall2000-full` | 中间 `f47...` DLL：目标 15 FPS；约 2001.715 ms 停顿期间 input +60、fresh +60，Unity 三类消费计数均 +0，错误 +0，完整日志无 malformed 行 | 无预览、Windows CPU；最终修复候选仍需按 hash 重跑 |

本地 native/managed 合同、编译或 mock/fence 测试只证明相应协议，不能代替 Android 驱动、像素或吞吐证据。
本轮用户已暂缓新的真机 APK 安装，因而没有本次 OnePlus 新 APK、RK3588、RKNN 或 RGA 的硬件验收；此处不提供继续安装步骤。

Windows 原生预览当前复制分析尺寸 RGBA，不是原始高清图。它随已准备来源帧逐帧尝试三槽有界复制，槽位或拷贝预算不足时跳过，没有固定 5 FPS 上限。
Android 原色高清 live preview 当前最多 5 FPS，压力下可降至 1 FPS，慢 copy 会暂停后续预览；可选高清预览/抓拍提交前还有 `1920×1080×4` 字节上限。
这些是保守准入值，尚未经过设备 GPU copy budget 资格验证。
分析和可选 HD 抓拍仍经过 Input 中同一转换提交/完成 fence，capture-only 失败也可能转成 source GPU error。
因此“Unity 慢读不持有 decoder/analysis lease”已经有独立合同，强意义的“延迟硬件预览不影响分析/抓拍失败完全隔离”仍是 Input 层待验收门槛。

新的控制 C 表以独立 handle 使用，旧 V1/语义 ABI 和 Plugin ABI1/2/3 保持原合同。
当前表有 21 个函数，x64/ARM64 为 176 字节；托管端识别旧 20 函数/168 字节候选前缀，缺少 `CancelSnapshot` 时明确不可用。
逻辑取消与真实硬件完成分别处理。只有 `TryRelease=OK` 才允许最终释放，BUSY/超时不能被解释成已完成。

1–8 人、真实双手关键点、同帧像素、逐人完整骨骼 30 FPS 和目标板热机目标继续独立验收，不能由这里的 fresh 帧率代填。
