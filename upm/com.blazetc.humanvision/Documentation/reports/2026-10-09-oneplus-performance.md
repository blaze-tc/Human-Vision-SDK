# OnePlus CPU、Vulkan 与 RTSP 停顿诊断

设备：OnePlus 9 Pro LE2120，SM8350/骁龙 888、Adreno 660，Android 14。
场景：Human-Vision-SDK-Test / HumanVisionSettingsDemo，Unity 2021.3.45f1。
这是本机测量，不能推算 RK3588 的 CPU、Vulkan 或 RKNN NPU 帧率。

## 已确认的瓶颈与修改

Android ORT CPU 原来固定 intra-op=1。对同一 RTMO416 FP32 输入和模型，在
手机上顺序测试 1/2/4/2/1 线程，模型均值为 198.208/138.762/90.051/135.985/
169.588 ms。输出形状相同，四线程与首轮一线程最大绝对差 0.00006104。
生产 CPU 会话改为 min(逻辑核数,4)，未知核数取 1；加速器和 XNNPACK 的
线程池不变，inter-op=1、顺序执行、关闭 spinning 均保留。
这是有界默认策略，没有做设备绑核或持续锁频。

实际应用同一内置视频、CPU/MaxBodies=4 的完成结果率，从 5.421/s 提高到
8.223/s（约 +51.7%）；backend 均值从 180.261 降至 115.759 ms。
四线程帮助 CPU，但尚未达到 20–25 帧目标。CPU 与 Vulkan 使用不同模型，
不能把二者 FPS 之比当作同模型加速倍数。

RTSP 的 GPU 转换完成之前只在渲染回调检查，managed 元数据读取可能再等
一个 Unity 帧。本次允许读取时非阻塞查询真实 fence，并只发布已完成图像。
保持当前活动图像租约、latest-only 排队和 render-only 资源销毁，未增加
等待 GPU 的主线程调用。用户最新确认主要是停顿，当前证据没有证明新旧帧
来回交替，不能称为修复了帧倒退。

## 已完成应用测量

预热排除 30 秒，以 native processed 的增量计算完成率。含人的新结果
窗口也独立统计；render FPS、重复画骨骼和单次截图不参与推理计数。

| 输入 / 配置 | 构建 | 完成/s | 发布/s | 含人一秒窗口 ≥20 | 本地结果年龄均值 |
|---|---|---:|---:|---:|---:|
| 电脑相机 USB RTSP，Low，1280×720 | 原 APK fa0 | 24.252 | 27.698 | 91.071% | 107.713 ms |
| 同相机 USB RTSP，Low，1280×720，300s | CPU4 a290 | 24.590 | 28.371 | 96.183% | 101.913 ms |
| 同相机 USB RTSP，Low，1280×720，180s | completion poll e879 | 25.076 | 29.100 | 100%（144 个窗口） | 102.057 ms |
| 电脑相机 Wi-Fi RTSP，Medium，1280×720，120s | e879 | 15.538 | 28.502 | 不能满足目标 | 152.793 ms |

USB 最后一轮最低含人窗口 22.598/s，未发现 pipeline 错误；停流时转换和
提交数一致，缓存引用、视图及 pipeline 资源归零。相对 a290 只测得小幅
发布/完成率提升，本地结果年龄没有下降；一次顺序对照不足以证明所有网络
或热状态下都有同等收益。

Medium 是枚举 0 / 640×384，Low 是枚举 2 / 512×288，High 是 1 / 960×576。
Wi-Fi Medium 复现了约 16 帧；NCNN extract/download 平均约 50.365 ms，
USB Low 对照约 31.553 ms。较小模型输入有明显速度空间，但两轮输入链路
不同，且较低等级可能损失远距离、小人体准确性，需针对实际场地复测。
采集/本地发布尺寸与模型尺寸是两项独立设置。

名为 pacing-gpu-camera-lan-low640 的 300s 测试中途从请求 640×480 改到
4K 并重新打开输入，因此不作为固定配置验收：前段实际 640×360 完成
18.596/s；后段实际 1280×720 完成 23.492/s、发布 28.929/s。
此差异可能涉及负载/调度，GPU 频率节点权限不足，当前不能归因于锁频。

最终日志 APK a3ff（300s 固定 Wi-Fi、Low512×288、1280×720）实测：
发布 29.093/s、完成 23.297/s、含人的新结果 23.263/s；262 个一秒窗口
有 92.748% ≥20，最低 16.749，P05 19.742，所有窗口均有人。没有 pipeline
错误或 source/generation 切换。**尚未达到全程稳定 20–25 帧**。
本地结果年龄平均 111.997 ms，P95 148.741 ms；它不含相机曝光、网络或
硬解码的完整端到端延迟，未证明动作画面停顿已经消失。

这轮平均 backend 40.323 ms、SDK total 42.288 ms。96 个完整稀疏样本的
extract/download 均值 32.802 ms，约占六段总和 40.284 ms 的 81.4%；
预处理等待 2.427 ms、后续推理/输出提交等待 3.577 ms。应用平均 CPU
占全核容量 12.34% / 一核口径 98.75%，驱动 GPU busy 76.11%，PSS
340 MB，PowerManager thermal status=0。主要剩余优化方向在模型执行及
其 GPU 同步路径，而非提高 Unity 的 60 render FPS。频率权限不足，不能
断言设备没有降频或将 GPU busy 当作模型独占使用率。

最终同视频 CPU 120s：完成 7.463/s，backend 127.895 ms、total
134.251 ms；应用 CPU 一核口径平均 346.16%，配置实际 intra-op=4。
这轮 thermal status=3，CPU 温度平均 69.16°C；Android 将 3 定义为
[严重热节流](https://developer.android.com/reference/android/os/PowerManager#THERMAL_STATUS_SEVERE)。
先前 CPU4 轮同时有 status 0 和 3，原 CPU1 轮为 0；因此保留 7.46–8.22
帧的实际范围，不能将不同热状态的 +37.7%～51.7% 当作恒定加速倍数。
本次没有修改温控或强制锁频，停止测试后让设备自然冷却。
公共和私有 native-0.log 均确认记录了四线程初始化行；实际截图可看到
四个人的骨骼。Vulkan Wi-Fi 截图可看到电脑摄像头及骨骼。

## 未采纳的候选

同 RTMO 输入的 XNNPACK 四线程实验约 148.361 ms，未胜过 ORT CPU4 的
90.051 ms，未切换生产后端。成功 append EP 不代表所有节点都在该 EP 上。
NCNN 独立后台探针的 SGEMM/no-local-memory 未胜过各自控制，未更改生产
选项。探针 GPU 后台时钟/调度与前台 Unity 不同，不能把其约 143 ms 当作
应用 GPU 耗时。本次固定 NCNN 构建 OpenMP=OFF，CPU num_threads 参数
不能被描述为实际四个 OpenMP worker。先前失败的 FP16/INT8 数值门槛仍
排除；没有更换未经验证的模型权重。

依据：[ORT 线程配置](https://onnxruntime.ai/docs/performance/tune-performance/threading.html)、
[XNNPACK 线程池](https://onnxruntime.ai/docs/execution-providers/Xnnpack-ExecutionProvider.html)、
[NCNN OpenMP](https://github.com/Tencent/ncnn/wiki/openmp-best-practice)、
[NCNN Vulkan](https://github.com/Tencent/ncnn/wiki/vulkan-notes)。最终取舍来自
此设备实际输出校验和应用日志，而非芯片宣传参数。

## 验证与证据

主要复核命令（从本次 SDK worktree 执行）：

```powershell
cmake --build build/windows-test --config Release
ctest --test-dir build/windows-test -C Release --output-on-failure
ctest --test-dir out/input-native/windows -C Release --output-on-failure
cmake --build build/android-hardware-stages --parallel 4
python tools/maintenance/check_architecture_boundaries.py
python tools/package/check_input_package.py --root upm/com.blazetc.humanvision.input
python tools/benchmark/analyze_settings_device_log.py <会话文件夹> <结果.json> --warmup 30
```

Windows native 构建使用 VS v143/14.44 的 x64 开发环境；普通终端缺少
MSVC includes 时先运行 VsDevCmd.bat -arch=x64 -host_arch=x64 -vcvars_ver=14.44。
Unity 通过已打开测试项目的本地技能服务执行实际 Android BuildPlayer。
独立项目 `out/sdk-api-verification/package-project-final` 通过 Unity
`-batchmode -nographics -runTests -testPlatform EditMode -testFilter ...`
生成真实 leaf 测试 XML；不是仅检查接口返回 success。

- CPU 策略测试先 RED 后 GREEN；Windows SDK native 408/408。
- 独立 Input native 49/49；SDK/Input ARM64 构建均通过。
- 日志过滤回归先缺少 BuildLogcatCommand 而失败，修改后独立 Unity 项目
  HumanVision.TestProject.Tests 40/40。当前工作 Editor 的 REST 曾把零个
  leaf 测试的 root suite 报为 1/1；该结果明确不计入通过证据。
- CPU init 标签 HumanVisionCpu 已加入 PID 限定的 native 日志采集，保留
  其它标签静音。线程数初始化记录可与同会话 performance/events 复核。
- SDK native SHA256：5fd4366a82e4ef038e4e72c311a3302912d6c0cd331e96904ffa123bcb794f5e。
- Input native SHA256：5ad008563b23ef5281c7387800cb47bd79f7542de85cc7b493e70714a8b6c2b3。
- 43 项 Runtime 索引资产校验；模型资产字节保持一致。V1 C 导出保持，
  Input 新增一个私有 C++ helper 符号，不声称全部 ELF 导出集合逐字相同。
- 性能日志版本 Unity APK build Succeeded、0 errors / 8 warnings，00:03:15；
  APK SHA256 a3ffeb5ca648ee01fd3c9f837807587cfee451f5a35ac4db827b931a7dc7c200。
  SDK/Input native 和 Runtime index 与 e879 对照完全一致；修改为日志捕获
  和构建身份。原始构建告警保留在 Editor/build 日志中。
- ORT 手机独立探针的错参数、非法尺寸、缺失输入及缺失模型返回码 4/4
  符合预期；17 项离线分析/RKNN 工具回归通过。边界与公开 API 检查通过。
- SettingsDemo 的电脑摄像头快捷入口原来直接使用空 IP。新测试确认空
  IP 的 camera/video 两例先失败（手动 IP 例通过），现在优先手动 IP、
  空 IP 回退到构建时写入场景的电脑地址；不枚举手机 IP，仅更新草稿。
  SettingsDemo EditMode 26/26 通过。修复后的 UI APK 保持推理 native 和
  模型不变；五分钟性能数据归属于上面的 a3ff APK，不混淆构建身份。
- 中间控制器修复 APK SHA256：02a050b4edcc71ebae561ef1bf8e208e3f22c9c1f5f31e3823232ddcd4ba7710；
  build Succeeded、0 errors / 8 warnings，00:03:48。43 项资产、Runtime index
  和两项 native 与性能日志版本完全一致。该版实机仍报告缺少构建电脑
  地址，因此不能作为自动填入成功证据。继续修复 BuildProcessor 对设置
  场景的遗漏：新增 SettingsDemo bake 用例先失败，SharedQualityUiTests
  随后 37/37 通过；正式手机验证属于后续最终 APK。

本地证据：out/oneplus-opt/（native-tests.log、input-host-tests.log、
log-tag-red.xml、log-tag-green.xml、threads.json 及每轮 analyzer JSON）。
用户可获取的录像/会话/APK：Human-Vision-SDK-Test/
DiagnosticsVerification/OnePlus-Optimization-20261009/。
私有双后端 APK 包含实验 RKNN 模型；RK3588 驱动、性能、全手部观测仍待
实体设备验收，本次不发布为公开 Release。

## 2026-10-10 最终 UI APK 验证

最终 APK SHA256：1efc3ba975618156d7fa227fc87306c095da005b8e0edb1315646fad38ee7a82；
BuildPlayer Succeeded，0 errors / 8 warnings，00:02:45。与 a3ff 性能对照
相同的两项 native、Runtime index 和 43 项模型资产再次逐字/哈希校验通过。
空电脑 IP 的实际手机按钮成功填入 192.168.1.40:554/videodevice，未使用
手机 192.168.1.214；截图 autofill-after.png。Low + Vulkan + 1280x720/
30 请求实际 RTSP 打开成功，点击应用并保存后配置确实写入。该启动检查
不作为另一次稳定帧率验收。

同版内置 video-1.mp4 / MaxBodies4 / Vulkan Low 连续120s，排除30s预热：
完成23.293/s、含人的新结果22.917/s、发布24.950/s；85个一秒窗口
94.118% >=20，最低17.751；backend40.083ms、thermal status0、无pipeline
错误。截图确实有人体及骨骼，不把重复渲染或数字HUD当作识别证据。
目标仍未全程稳定；不是CPU/GPU同模型倍数测量。

最终手机保存 Low/Vulkan/电脑相机1280x720、自动启动关闭；原配置保留在
每轮 capture 的 original-settings.json。测试使用的ADB8554反向端口已移除，
用户原有34998反向端口和RtspServer/VLC保持。停止后取完整文件夹，包含
公共native CPU配置和本次测试的分析JSON。RK3588/NPU性能仍需实体测试。

停止后另行归档 final-preset-session 完整文件夹。清空测试留下的旧 APK 绝对 jar 视频 URL（保留此前 JSON 备份），以便切到视频时使用当前安装包内的 video-1.mp4；其余已保存的设置保持不变。
