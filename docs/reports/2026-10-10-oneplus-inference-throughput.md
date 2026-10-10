# OnePlus 真实骨骼识别帧率：命令合批验证

目标是增加新完成的骨骼识别结果，不通过平滑、插值或重复绘制提高显示 FPS。
设备为 OnePlus LE2120 / SM8350 / Adreno 660 / Android 14；场景为
HumanVisionSettingsDemo，Unity 2021.3.45f1。本文不能推算 RK3588 或 NPU 帧率。

## 已完成的交叉测量

使用同一段用户视频：1024×576、25fps、20 秒循环、七人可见；SDK 设置
MaxBodies=4、Vulkan、低档 512×288。人数上限与实际可见人数分别记录。
每次恢复完全相同的原始设置，预热 20 秒后统计，计数来自新发布且含人体的
结果事件。这个计数不等于每个固定 TrackId 的完整关节帧率。

| 实际 APK | 采集时长 | 新人体结果/秒 | 后端均值 | 窗口 ≥20fps | 最低窗口 |
|---|---:|---:|---:|---:|---:|
| 原版，首次 | 120s | 23.403 | 38.828ms | 93.617% | 18.993 |
| 全局 256K 独立候选 | 300s | 24.478 | 33.884ms | 100%，273 个窗口 | 21.621 |
| 回装原版 | 120s | 22.716 | 39.918ms | 92.553% | 17.946 |
| 设备/精度限定的最终候选 | 300s | 24.507 | 33.461ms | 100%，273 个窗口 | 22.610 |

候选的五分钟测试达到了本次低档四人场景“所有预热后统计窗口 ≥20fps”的
近目标。视频只有 25fps，不能用它认证 30 个不同源帧/秒。首轮原版 thermal=0；
候选与回装原版都出现 thermal=0/3。不能把不同温度下的比值宣传为固定加速倍数。
四轮 pipelineErrors 为空；原版首轮有一条被明确标记的硬件 JSON 末尾截断，
候选没有解析丢失。Android 系统 CPU 与 GPU 频率节点权限不足，日志保留原因，
不能以“不提供数据”代替零占用。

## 为什么有效、哪些推断还不成立

固定的 NCNN 源码以 rough_score 判断提交预算。实测 Adreno 660 为 9，原版
采用 32K；此前逐层采样低档模型拆成 5 次提交。预算达到门槛会调用真实
submit_and_wait，CPU 转换边界与最终输出也必须等待。独立候选仅将 32K 提高
到有限的 256K；模型、权重、尺寸、FP32、内部 packing、关闭 subgroup 都不变。
没有删除同步，没有修改硬件评级。
源码依据是已固定的 [NCNN net.cpp](https://github.com/Tencent/ncnn/blob/e54f7b1f88434e1d844ea0551b880a1cfb079ce1/src/net.cpp)，
速度结论来自本地交叉实测，不能用上游实现推导硬件帧率。

实际 APK 的 extract/download 均值从 32.508ms 变成 2.295ms，而最后一次提交
等待从 3.446ms 变成 28.620ms。因此绝不能把前一阶段下降直接算作总体加速。
完整后端平均从 38.828ms 降到 33.884ms，下降约 12.7%；新人体结果率从
23.403 增加到 24.478，约 4.6%，更有价值的是低帧窗口改善。GPU 执行工作仍然
存在；没有频率读取证据时，无法精确划分减少的等待、GPU 空闲和调频贡献。

## 独立候选筛选

同一输入、输出保存、每项 10 次预热 +60 次测量，串行 baseline/candidate/
baseline/candidate，全部 Android thermal status=0。以下包含输出下载，不含
Unity、视频解码或跟踪，因此绝不是骨骼 FPS。

| 输入尺寸 | 原版模型均值 | Winograd23 均值 | 256K 模型均值 |
|---|---:|---:|---:|
| 512×288 | 100.220ms / 合批对照 100.806ms | 106.108ms | 61.449ms |
| 640×384 | 146.087ms / 合批对照 145.157ms | 156.373ms | 76.815ms |

Winograd23 分别慢约 5.9%/7.0%，排除。合批独立模型测量下降约 39.0%/47.1%，
实际 APK 的收益明显较小。保留背景 shell 与前台应用条件差异，不能据此承诺
应用获得相同比例提升。

四轮合批候选通过原有数值限制；另 13 组独立数值回归通过，包含原 11 组
单人/七人/空图、长方形/正方形，以及低档两个额外样本。比较原始 FP32 输出、
人数、框、分数、17 点坐标和置信度；有人工框的样本还执行唯一对应检查。
未放宽失败过的 FP16/INT8 限制。此检查属于固定样本，不是全数据集模型精度认证。

## 设备限定与发布界限

后续候选使用 ncnn_dispatch_budget.h：只对名称完全匹配 Adreno (TM) 660、
所有 FP16/subgroup 均关闭且 packing 开启的组合给出 256K。Mali-G610、其他
Adreno、未知设备和其他精度返回原 32K。NCNN 后续更高评级规则仍保留。
这是一项经过单元测试的策略，不是 RK3588 实机性能验证。

实验依赖从完整 SHA 审计的原始 ZIP 和两份审计补丁复制，缓存不修改。
库收据标记 shipping_eligible=false；SDK 构建必须显式启用
HV_ANDROID_NCNN_DISPATCH_EXPERIMENT，正式构建拒绝该收据，层级时间戳也不能
同时启用。默认 UPM 发布库当前不替换为实验库；实验 APK 不上传公共 Release。
CPU、RKNN 的实现不属于本次 GPU 合批变更。

最终限定版本已再次完成 300 秒实测，pipelineErrors 与数据解析警告均为空；
273 个预热后窗口全部 ≥20fps，thermal=0/3。结果平均年龄 89.276ms，首轮
原版为 97.948ms；年龄包括 SDK 局部链路，不是摄像头端到端延迟。
限定版本额外通过 16 组数值回归，包含三组 960×576 与七人左臂抬起检查。
默认其他设备行为已做策略单元测试，仍需由用户在 RK3588 上实机确认。

## CPU 切换回归

最终限定 APK 同一视频、MaxBodies=4，CPU 模式实际运行 60 秒，预热后 34 个
窗口均有新人体结果，截图可见四组骨骼。新人体结果均值 7.506/s，后端均值
125.888ms、SDK 总耗时 132.251ms、Unity 60.074fps、源发布 24.999fps，
pipelineErrors 与解析警告为空。thermal=3，CPU 温度均值 70.488°C；没有同温度
CPU 前后对照，因此只能确认功能回归，不能宣称 CPU 加速或无性能退化。
主要耗时仍在模型后端，而非 0.163ms 的日志刷新。CPU 使用的是
android-cpu-nohands / rtmo-t-416，与 Vulkan 模型不同，不能作为同模型硬件跑分。

## 当前电脑摄像头 RTSP 回归与下一方向

保留正在运行的 RtspServer/VLC，使用原电脑摄像头地址，最终 APK Vulkan/Low/
MaxBodies=4、1280×720 输出运行 120 秒。预热后 94 个窗口，源发布和推理完成
均为 14.427/s，Unity 均值 49.785fps；后端 47.918ms、SDK 总耗时 49.919ms，
thermal=0，管线错误和解析警告为空。实际截图可见摄像头房间画面，本轮缺少
持续的人体动作：偶发含人体结果计数仅 0.349/s，不能当作骨骼速度验收。

RTSP 此时在进入 SDK 前的发布速率就只有约 14.43/s，完成计数并未持续落后
发布计数。降低模型耗时仍有价值，但单独改模型不足以让当前 RTSP 达到
20–25 个不同源帧/秒。22 组稀疏输入样本中，decode-to-submit 均值 9.791ms，
conversion-fence-poll 均值 23.198ms、P95 49.891ms；后者包含渲染线程轮询，
不是纯 GPU 转换时间。21 组 native 样本中最终推理等待均值 40.838ms。

下一步应在持续人体动作、相同温度下同时记录推流输出、解码图像获取和转换
完成速率，再定位源发布减少发生在哪一段。现有 arrival-to-image 时间不能
证明同一个网络包的解码耗时，GPU 频率也不可读取，因此不把问题直接归因于
网络、解码器或 GPU 降频。此次没有修改推流程序或视频源。

## 自动检查

Native Release 全量 411/411，设备/精度预算策略 3/3，源码/收据防护 5/5，
CMake 依赖绑定 9/9，架构/公开接口与 git diff --check 通过。实验安装目录重新
按完整源清单、实际 CMakeCache 参数和七个静态库 SHA 封存，再配置 SDK 成功。
Android ARM64 全部受影响目标构建成功，最终 APK 内容与安装的 native 指纹一致。
数字回归 16/16 使用原数值阈值；这些检查均不能替代其他硬件实测。
最终 Unity Editor debug_get_errors 返回 0。每次采集后均逐字节恢复原设置，
最后安装保留最终限定版 APK，测试项目也保留对应 native 与审计清单。

## 证据位置与命令

完整本地证据：
E:/UnityProject/Human-Vision-SDK-Test/DiagnosticsVerification/OnePlus-Inference-20261010。
SDK 工作树 out/oneplus-throughput-20261010 包含完整源清单、原始输出、运行程序
指纹、热状态、数值比较和构建日志。视频和模型原始字节不新增至 GitHub。

原版 native SHA256：5fd4366a82e4ef038e4e72c311a3302912d6c0cd331e96904ffa123bcb794f5e。
全局候选 native：443d22e9993a7f64b0cf269092743c4017208f40f6149c817418d593111baa05。
全局候选 APK：b5e806042595f65cbc1a67005b1eedf874fe81fa0921574056a59acffd22a27e。
最终限定 native：c98bc770007bf306352e5af710911e6130ed734f0fa0667248381ca57cb18c48。
最终限定 APK：fca5960329373592a034247e9cac756204ee02066cc619e410cf9b299eeb3434。
本地 APK 名称：HumanVisionSettingsDemo-OnePlus-DispatchScoped-Experimental.apk。
API26 ELF/NCNN AHB/ORT CPU/RKNN 符号以及 506 个强导入实际审计通过；48 个公共
HV 导出与原版完全一致。首次构建因旧审计清单中的 native 指纹被拦截，重新
实际审计和同步清单后构建成功，errors=0、warnings=8。

```powershell
py -3.13 -m unittest discover -s tools/test -p test_ncnn_dispatch_experiment.py
py -3.13 -m unittest discover -s tests/architecture -p test_ncnn_cmake_binding.py
py -3.13 tools/maintenance/check_architecture_boundaries.py
cmake --build build/windows-test --config Release
ctest --test-dir build/windows-test -C Release --output-on-failure
py -3.13 tools/benchmark/analyze_settings_device_log.py SESSION OUTPUT --warmup 20
```

完整源缓存正向检查需要 HV_NCNN_BASELINE_NET 指向已审计的原版 src/net.cpp。
实验准备命令参见 tools/benchmark/mobile_ort_probe/README.md，设备限定版加
--adreno-only。所有结论必须绑定实际 APK、native、模型和输入指纹。
