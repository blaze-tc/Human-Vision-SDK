# RK3588 原生输入/输出复用验证

本轮针对实测输入提交及输出获取开销继续优化。没有连接 RK3588，以下不代表
该设备已经达到 20–25 FPS。模型、精度门槛和坐标/帧身份合同没有改变。

## 基线与改动

用户 session-20261010-080649-482-b1084e4c：完成结果约 8.43 FPS，发布约
11.83 FPS。SDK 总耗时均值约 51.95 ms；另一个周期采样集合的 RKNN 阶段均值
为输入 14.24 ms、执行 27.74 ms、输出 4.45 ms。两种采样不能直接相减。
回读生命周期约 136.61 ms 包含 GPU 排队和 Unity 回调调度，并非纯拷贝耗时。

保留上轮 GPU 独立快照、复制完成即释放源纹理、模型尺寸回读资格检查、有限
队列、视频缓存播放及每帧坐标元数据。本轮增加：

- 管线显式传入经过验证的模型 mean/norm；后端不猜测归一化。
- 按查询到的 FP16/NHWC 格式、行步幅和通道填充建立复用输入缓冲；查表合并
  归一化与 FP16 转换，pass_through=1 避免重复转换。
- 绑定可复用 FP32 输出内存；执行后同步到 CPU 并复制到原有输出缓冲。
- 独立上下文依次尝试原生 FP16 输入/共享输出、共享 UINT8 输入/输出、共享
  UINT8 输入；不支持、错误、全输出逐位不同或收益不足时自动回退。
- 两帧预热仍检查数值，再比较五帧；整体后端至少快 5% 才启用。检查期间只
  发布基线结果，不将额外推理计入骨骼帧率。启用后错误则原帧重跑基线上下文。
- 日志记录路径、次数、比较耗时及最近一次候选回退原因。

共享内存依据 Rockchip 的
[rknn_create_mem_demo](https://github.com/airockchip/rknn-toolkit2/blob/master/rknpu2/examples/rknn_api_demo/src/rknn_create_mem_demo.cpp)
及固定 2.3.2 头文件实现；支持与提速须设备确认。RGA 保持关闭。选择策略是
有界优先顺序，不是穷举所有路径并证明全局最快。

## 验证证据

- 测试先行：对齐与回退诊断测试修复前 2 项失败；修复后全量 native CTest
  **426/426** 通过，包括 C ABI、资源清理、原帧回退、步幅/容量/舍入检查。
- Android ARM64 Release 编译成功；API 26 检查解析 506 个强动态导入。
- OnePlus 独立 ARM64 探针：3,072 个值与编译器 FP16 转换逐位一致，全图和
  填充检查通过。512×288 紧密 RGB3 输入打包 p50 0.335 ms、p95 0.518 ms；
  8 通道填充 p50 0.381 ms。**不含 RKNN 同步、推理和输出，不是 RK3588 测量**。
- Toolkit2 2.3.2 PC 模拟器：七人/单人/无人控制组，显式归一化 FP32 NCHW
  与 UINT8 NHWC 的两个输出全值一致。模拟器拒绝原生 FP16 输入，因此不能
  用模拟器宣布原生共享内存通过；设备必须执行完整数值检查。
- 架构和公开接口检查通过。原始 RKNN 模型 SHA256 保持
  `1b3ba8dd5bb81e3d968cfeef019c42b1aa3bffca6f0ec1c9f89f257fe0c08030`。

原生 SHA256：`cd4605b58f92feacb4237ca172e9e0fb90fb6366462ce98eacd9e4dbff29d820`。
完整记录保存在 `out/rknn-throughput-20261011/`。

```powershell
tools/test/run_native_tests.ps1
cmake --build build/android-dispatch-scoped-experiment --parallel 6
python tools/test/verify_android_native.py --library build/android-dispatch-scoped-experiment/bin/Release/libhumanvision.so
python tools/maintenance/check_architecture_boundaries.py
```

`tools/benchmark/rknn_native_input_probe.cpp` 用 Android NDK ARM64 API26、
`-O3 -std=c++17 -static-libstdc++ -Iruntime` 编译并通过 ADB 执行。

## 20–25 FPS 的条件

20 FPS 对应每次新结果 50 ms，25 FPS 对应 40 ms。若约 27.7 ms 模型执行保持，
输入、输出、预处理及后处理合计必须降到约 12–22 ms，同时持续提供相应速率的
新帧，避免回读和 Unity 调度产生串行空档。本轮针对其中可避免的转换、分配、
获取与同步开销；不能假设这些开销全部消失或直接相加得出收益。

这是可检验的工程预算，**不是设备帧率预测或验收承诺**。同包自动对照须确认
优化路径实际启用、逐人真实完整骨骼帧率达标、延迟和尾部耗时合理、录像跟随
正确后才能宣布通过。驱动拒绝或数值不同会保留基线，不放宽精度换指标。

## 联合 APK

`HumanVisionSettingsDemo-Combined-NativeIO.apk`，SHA256：
`1365344855552187785a4e1402b15e608715e4292287b4df3052ec35aa092f69`。
交付目录：`E:/UnityProject/Human-Vision-SDK-Test/真实设备日志/联合优化-20261011`。
APK CRC、原生/模型/厂商运行库哈希通过；Unity 2021.3.45f1 构建 0 错误、8 警告。
首次打包被旧原生 SHA 审计记录拒绝；重新检查真实库和符号、更新私有构建记录
后构建通过，没有移除或绕过构建门槛。50 个对外 HV/UnityPlugin 导出保持不变。

新 APK 已安装至 OnePlus。当前 RTSP 的仅预览发布为 899/60≈14.98 FPS，独立
ffprobe 观察 r_frame_rate=15000/1001，流声明的 avg_frame_rate=30/1；不能只凭
声明值认定有 30 个新画面。该测试源不足以证明 20–25 个真实新骨骼帧/秒。
现场 RTSP 画面无人，因此本轮只验证该源的播放、模式切换和结果完成链路；
含人体的本地视频另行记录真实骨骼。没有用无人结果代替人体识别验收。

OnePlus 会话 `session-20261011-044719-184-ba45eb6d` 于 05:00:11 UTC 完成全部
自动对照。6 个 GPU RGB/镜像检查全字节一致，CPU/Vulkan 各运行视频和 RTSP
60 秒，Vulkan 视频持续测量 300 秒。分析器没有数据质量警告，Unity 日志没有
Error/Exception/Assert。结束后复制日志文件夹与计算模式下拉框均可使用。

| 输入/计算 | 完成结果吞吐 FPS |
|---|---:|
| 视频 / CPU | 8.75 |
| 视频 / Vulkan | 24.82 |
| RTSP / CPU | 7.61 |
| RTSP / Vulkan | 14.33 |
| 视频 / Vulkan，300 秒 | 23.02 |

该表是全局结果完成率，**不是逐人完整骨骼验收**。长测人体结果窗口达到
20 FPS 的比例约 73%，所以也不声明所有时段稳定 20–25。逐人日志包含离开、
遮挡、身份切换和视频场景切换；原始统计已随包保留，不能筛除低值冒充达标。
OnePlus 仅用于公共输入/播放/CPU/Vulkan 回归，明确跳过 RKNN 硬件验证。
源码和 APK 构建通过均不能代替 RK3588 的 NPU 实测。
