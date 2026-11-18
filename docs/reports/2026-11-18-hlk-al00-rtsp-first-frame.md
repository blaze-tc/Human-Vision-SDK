# HLK-AL00 Android RTSP H.264 首帧

测试设备为 HONOR 9X HLK-AL00、Android 10 (API 29)、Kirin 810 / Mali-G52。基线是 `v0.4.0-preview.16` (`0dead88c2a40989393629f5d1475038587859ada`)，Input `0.1.0-preview.9`。同一台手机上重新运行已安装并核对哈希的原版《跨栏王》宿主，原流已收到关键帧，海思解码器重配置输出端口时报 `-1010`，零解码输出、零取图，最终 `INPUT_START_FAILED`。未进入比赛，全部比赛指标均为「无样本」。

素材 `Exercise-11.mp4` SHA-256 为 `daae54318ba3c2a793fcfefaF3b6df658d33fb6e4ef21acb79c0cf90176f41f6`。MediaMTX 使用 FFmpeg 从 20 秒开始流复制，RTSP/TCP，H.264 Main Level 4.0、30 FPS。SPS 编码尺寸是 1920×1088，可见裁剪 `[0,0,1920,1080)`。MediaCodec 配置和 AImageReader 初始尺寸为 1920×1080。

同机诊断 APK 在一次会话中捕获 90 个从关键帧开始的 H.264 访问单元，再逐一路径重放同一份数据、SPS/PPS、PTS 和送包节奏。固定 `OMX.hisi.video.decoder.avc`；原始 A/B 只替换输出 Surface。两路 `csd-0` 都使用 FFmpeg 的 52 字节 SDP extradata，FNV-1a `49693f28d2d95fb7`，压缩包及时间戳指纹为 `74d3a7187c0ee4ff`。读者容量复验自身使用同一批数据，指纹 `b584ff4eb9e9dfd2`。

| 对照 | 端口重配 | 首帧 / 结果 |
| --- | --- | --- |
| PRIVATE + GPU_SAMPLED_IMAGE，4 张 | 17、16、15、14 均被拒，`-1010`；NDK 出队返回 `-10000` | 没有输出格式或首帧；0 个输出、0 张 AImage |
| 普通 SurfaceView | 分配 13 个输出缓冲区 | `video/raw`，1920×1088，stride 1920，slice-height 1088，inclusive crop `(0,0,1919,1079)`。首帧 PTS 5096789 µs；86 个输出，有屏幕画面 |
| PRIVATE + GPU_SAMPLED_IMAGE，3 张 | 16、15、14 被拒，13 成功 | 输出格式和裁剪同上；首输出与 AImage 时间戳同为 5095000 µs（换算后），AImage 1920×1080，AHB 1920×1088。86 个输出、73 张 AImage |
| PRIVATE + GPU_SAMPLED_IMAGE，2 / 1 张 | 均分配 13 个输出缓冲区 | 均有 1080p 首张 AImage、86 个输出；分别取得 64 / 71 张 AImage |

诊断回放未提交 EOS，计数用于对照启动行为。端口在读到 SPS 后报告 OMX 最小缓冲区数 9。4 张读者的额外保留使最低申请数为 14；3 张读者时最低为 13。只改容量即恢复输出，所以本候选保持原 CSD、关键帧规则、配置尺寸和 Vulkan 输出。仅在型号 `HLK-AL00`、API `29`、该海思解码器三项同时匹配的生产路径使用 3 张，其余仍为 4 张。兼容分支的满容量状态视为临时背压。解码时间戳、源帧标识、代次及预览/分析租约沿用既有路径。记录原始出队错误时保持原有重连策略。

构建命令为 `pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Android -ApiLevel 26 -RunTests`。新设备选择测试在修复前如预期失败；候选通过 57/57 项 Input 测试和 API26 ARM64 动态依赖审计。与原库相比，828 个导出符号（其中 47 个 Input/Unity 入口）及所有动态依赖保持一致；新增读型号和 API 的 API26 系统导入 `__system_property_get`。架构、组件目录、UPM 换行检查通过。

| 产物 | SHA-256 |
| --- | --- |
| 原 Input arm64 库 | `5990e77ce2491f59d4d01443da6bd168c0168dbd22bf6870f94f962c4f2a981b` |
| 候选 Input arm64 库 | `78e4a938814286105644f516967ec3aef711f6bcb38943d273df62c614ec8149` |
| Input `0.1.0-preview.10-hlk.1` 离线包 | `29e38d290f505392e2eb737481df6c6dc38ac6fdacfdd265d8776c469375bf69` |
| Surface A/B 诊断 APK | `2d50ff4a004b3bf57e6c538b30aab9e25518b5f7f5e4536250c3c0a59e71385a` |
| 跨栏王正式 AAR | `7523630629d9780d61c0f1f52b87bc3201be37af4ab98638fb2c7964a213edc9` |
| 测试宿主 APK（设备安装后复核一致） | `f8902f6c8520d0e36758b0878d351fe881d4e7935f515c42395976f2bfd885ec` |

诊断 APK 源码在 [`tools/test/android_rtsp_surface_ab`](../../tools/test/android_rtsp_surface_ab/README.md)。本地证据目录 `out/diagnostics/hlk-rtsp-20261118/` 下，`baseline/current-system-logcat.log` 为本轮基线，`probe/ab-system.log`、`probe/count-system.log` 为同码流对照，`probe/result-summary.json` 绑定原始日志哈希；构建和原生 ABI 审计在 `candidate-final/`，Input 离线包在 `delivery/`。视频、截图和设备日志随本地交付保留。

## 跨栏王真机结果

Unity 2021.3.45f1 以发布 SDK `0.4.0-preview.16` 和本地候选 Input 重建 `hostTesting=false` 正式 AAR，并用 Gradle 组装独立宿主。AAR、APK 与离线 Input 包的 arm64 库逐字节一致。临时构建配置已恢复，跑跳判定、动作阈值、分道及计分源码和资源未改。候选宿主只额外记录 `FramePresented` 事件，以测量比赛内实际生成的离屏高清同帧图。

- **首帧通过。** 生产会话选中海思解码器和 `PRIVATE + GPU_SAMPLED_IMAGE` 3 张容量。原流第一个提交关键帧 PTS `5094011 µs`，约 115 ms 后取得同 PTS 的 MediaCodec 输出，再过约 3 ms 取得同 PTS AImage。可见 1920×1080、AHB 1920×1088，`vk_format=0 external_format=238 sampled=1 query=1 failure=0`。Vulkan 发布实际配对的 preview / analysis 序号。
- **尺寸和照片通过。** `VisionSession` 明确 `auto_stream=False model_sized_input=True matched_analysis_preview=False`。比赛采样的 `decoded/preview=1920x1080`、`submitted=512x288`；同帧计数仅统计实际 1920×1080 的相机底图与合成纹理。Ready 从两次有效同帧相机图（3、4 道，`image=1920x1080`）截取头像，实际写盘并由宿主读取，PNG 均为业务现有的 500×500 裁剪尺寸。
- **显示和重连通过。** 设置页原流识别并绘制人体骨骼，关骨骼显示 1920×1080 实况，再开启恢复配对。实际中断 RTSP，重连期间 `display=empty` 且无画面或结果身份；同 URL 临时恢复到取自录像的独立无人 1080p 测试流（只保留没有人物的地面，不用于原流比赛验收），代次 3 显示高清实况、不绘制骨骼，诊断画面显示原始人体数为 0。再次断流并恢复原始 Exercise-11 后，代次 4 成功展示 1080p 同帧，首个配对图/骨骼帧 `11679`，同时间戳 `246614188 µs`。记录的配对中未发现身份错配或复用旧代次。Settings 有人的实际高清同帧为 7.08 FPS，26 个同代次配对统计区间。
- **首局流程完成，玩法验收未通过。** 原始 1080p 流下开赛至结束约 60.10 秒，宿主收到成绩、写盘、返回排行榜并回 Ready；完整比赛没有记录到有效的 `outcome=started` 起跳事件。未修改阈值或以模拟输入填补。

首 30 秒实际后端 `backend.ncnn.vulkan`，设备驱动识别 `Mali-G52`。除高清同帧以实际 98 张 / 29.637 秒计数外，下表为窗口中位数（游戏指标 14 个窗口、原生解码 30 个窗口）。

| 首 30 秒 | preview.16 旧包 | HLK 候选 |
| --- | --- | --- |
| 游戏渲染 FPS | 无样本 | 12.13 |
| `InputPublicationFps`（高清预览） | 无样本 | 8.31 |
| `FreshResultFps` | 无样本 | 3.76 |
| 1080p 离屏同帧 FPS | 无样本 | 3.31（98 对，0 拒绝） |
| SDK `TotalMs` | 无样本 | 284.90 ms |
| 硬解 / AImage 取图 FPS | 无样本 | 29.99 / 29.99 |
| 电池 / 机框 / GPU 温度 | 无样本 | 35–36 / 35.34–35.99 / 51–53 °C |

系统 Thermal Status 始终为 0（USB 供电）。兼容读者未触发满容量计数；硬解和取图接近 30 FPS。Unity 渲染、Vulkan 纹理转换和推理同时运行时，发布和推理明显慢于解码。284.90 ms 的推理耗时对应约 3.5 次/秒的量级；单轮证据不能拆分 GPU 算子、队列、带宽和 Unity 场景各自的贡献，也不能将温度状态作为 GPU 频率无变化的证明。现有证据不支持把更换播放器作为解决低推理帧率的办法。

运行证据的本地位置：`host/device-round/device-epoch.log`、`round-summary.json`、`photos/receipt.json`、`sensory_demo_scores.xml`；设置页证据在 `host/device-settings`，`rtsp/route-events.txt` 记录真实中断与恢复。AAR/APK 内容审计和设备安装哈希分别在 `host/aar-audit.json`、`host/host-apk-audit.json` 和 `host/installed-verified.json`。
