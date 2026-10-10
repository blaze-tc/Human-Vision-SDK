# RK3588 现场日志分析与一次性交付修复（2026-10-10）

用户输入：Human-Vision-SDK-Test/真实设备日志 中 session-20261010-055047-860-df3d29a3。
原始文件保持不变，附加 `.txt` 的副本在 ignored out/rk3588-field-20261010 中规范化。
使用真实分析器 `python tools/benchmark/analyze_settings_device_log.py SESSION analysis.json --warmup 15`。

## 已确认的原因

- 本次确实运行 `pipeline.yolo.tensor → backend.rknn`，profile 为
  android-rknn-npu-quality-low，runtime 2.3.2 / driver 0.9.2 / core_mask=7。
  CPU-delivered 表示像素通过 CPU 内存送入模型，不表示 CPU 执行模型。
- 较长的 720p 分段：输入发布 13.763 次/s，模型完成 5.690 次/s；后端
  47.372 ms、整个 SDK pre/infer/post 61.626 ms，本地结果年龄 369.674 ms。
  含人体结果率另计，受人离开画面影响，不能混同模型完成率。
- 全会话 137 次周期诊断（含初始零值）平均 input_set 11.168 ms、run 28.998 ms、
  output_get 3.672 ms、release 0.002 ms。分析器现能按 profile/分段/预热自动匹配：
  长 720p 段 85 个实际 RKNN 快照均值为 11.467 / 29.783 / 3.756 / 0.001685 ms。
  快照数量不是推理次数，不能与另一个 SDK 结果的 total 跨帧相减。模型执行有成本，但 5.69 次/s
  对应约 176 ms 的完成周期，明显还存在输入/调度等待。旧日志没有单独测量
  GPU 回读时长，不能把余量直接算成某一函数的精确耗时。
- RTSP 真正解码的是 3840×2160 H.264，硬解 c2.rk.avc.decoder。本地选择
  720p 不会改变摄像机发送的 4K 码流。发布链路约 14 FPS 会限制骨骼更新；
  NPU 算得更快不能凭空产生新的视频帧。
- SettingsDemo 2D overlay 使用预览尺寸除关节/框的像素坐标；CPU/NPU 实际
  回读最大 1280×720。预览换成 1920 或 4K 时因此错误缩放。normalized
  坐标本身正确，不能通过改模型解码或给关节加偏移来补偿。
- NPU 驱动负载是整台设备、驱动采样区间的指标；三核算术均值不是本应用
  利用率，也不能单独证明模型没用 NPU。记录的温度/thermal status 没有直接
  支持“过热导致这次低 FPS”的判断。

## 本次修复

1. overlay 按实际推理像素尺寸换算，预览仍按自己的宽高等比显示。640×360、
   1280×720、1920×1080、3840×2160 的四角、中心和非中心点均自动验证。
   切换回读尺寸时旧结果立即失效，避免旧像素套进新几何。
2. CPU/NPU 最多两个异步回读重叠，源纹理各自保留原有 copy lease，提交仍
   使用不可变 slot 的宽高/帧 ID。延迟旧回调不能覆盖较新已送入的帧。
   原生模型仍串行 latest-frame-wins，不增加模型上下文或结果重排。
3. CPU/NPU 复用已测试的 30 FPS 有界 credit 限流，容许两帧的调度抖动。
   旧 `now+1/30` 每次重置期限会丢弃交替提前/延后的 30 FPS 到达。
   300 次交替 30/36.667 ms 到达全部通过，不允许同一时刻无限突发。
4. 保留已有 uint8/OpenCV 分段取整合同，新增 720p→512×288 的 ARM64 NEON
   精确 5:2 预处理，并优化原尺寸和 2:1。支持四种 RGB/BGR/RGBA/BGRA、
   padded stride、忽略 alpha、不跨行读取、无帧分配。其它比例仍走原插值。
5. 日志新增 CPU/NPU 回读均值/最近/最大墙钟、翻转、SDK submit、Blit CPU
   记录时间、拒绝原因、待回读数及实际像素尺寸。timings.jsonl 和
   pipeline.snapshot 中 cpuReadback 的 lastFrameId 明确独立于模型 resultFrame。
   回读时间包含 GPU 排队与 Unity 主线程调度，不能声称纯 GPU 时间。
   NPU 执行/input-set/output-get 分段及硬件频率/负载仍单独保留。
6. GPU 输入适配器直接提交，不经过 CPU 回读池。回读就绪/尺寸屏障只约束 CPU/NPU；
   Vulkan 仍使用原有 source/generation/sequence/提交帧范围校验。新增 RED→GREEN 回归。
7. SDK 字符串日志、测试项目 JSON 和硬件面板同步更新；不复用此前 Vulkan
   六段样本来冒充当前 CPU/NPU 耗时。日志按钮继续复制整次会话文件夹。

## 精度优先的模型尝试

固定 Toolkit2 2.3.2 环境真实编译三种混合量化范围，原始 SHA 与图节点先验证，
99 个真实视频帧校准，固定七人/单人/空画面门槛不放宽。

| 候选 | 七人最大关节偏差 | 单人最大偏差 | 结果 |
|---|---:|---:|---|
| pose-projection | 10.223 px | 5.097 px | 拒绝 |
| pose-head | 10.083 px | 1.490 px | 拒绝 |
| all-heads | 4.826 px | 1.490 px | 拒绝 |

均有原始张量/置信度或 3 px 关节门槛失败。更早分支输入范围第四次尝试被
Toolkit 优化图的别名校验拒绝，在导出前结束。三种失败候选没有进入设备包。
新转换工具检查实际图输出、路径祖先关系、返回码和 receipts；成功转换仍
不能写 deployment_ready=true。当前合格非量化模型字节保持不变。

## 验证证据

- 分辨率 RED：12 项中 1080p/4K 两项失败；修复后 12/12。
- 回读/几何过渡 RED、修复后 14/14；限流 RED 后通过。
- 最终输入专项 16/16（含新增 Vulkan 展示回归）；早期 15/15。EditMode 人工 Awake 的缓冲池显式释放，避免测试
  夹具自身 NativeArray 泄漏；运行时资源等待完成再销毁。
- 原生 414/414；ARM64 API26 构建通过；506 strong imports；C 导出精确保持 48。
- RKNN Linux 参考测试 25/25，架构/文档边界与公共接口检查通过。
- OnePlus 上真实 ARM64 12 个像素试验全部与冻结旧实现逐字节相同。
  720p RGBA 两次：旧 1.33055/1.329821 ms，新 0.330519/0.330898 ms。
  这是 OnePlus 预处理测量，不能写成 RK3588 的 NPU 整体 FPS。
- 新现场修复前的预处理包 Vulkan 120 s 回归：完成 24.887/s，含人体
  >=20 窗口 98.96%。它不包含本页新的 Unity 输入修复，作为独立回归记录。

Unity 全套尝试得到 337 passed / 65 failed / 32 skipped，不能宣称全套通过。
该实测工程有私有 43 文件 runtime，而部分夹具要求隔离无原生插件工程、Q2/CPU
专门模型索引、HV_TEST_RUNTIME_ROOT、发行默认 profile 或固定视频个数。
更严重的是原安装器夹具会删除 staged receipt，并以公共 20 文件索引覆盖私有索引。
已恢复原有 43 文件索引并逐项校验 SHA；实际模型文件未改变。安装器变更夹具
现在明确仅允许运行在无 staged receipt 的一次性工程，本实测工程 8 项 skip（非 pass）。
恢复后 NPU 合同 20/20、输入 16/16；硬件/会话日志类在全套中 11/11、12/12。
最终 Editor Console 错误 0，编译/刷新均已结束。

最终 APK：HumanVisionSettingsDemo-Vulkan-NPU-Experimental.apk，282648322 bytes。
SHA256 `d98d31d8b50304ee687299899fca1a59034ff74f1985c4cbfedefb9ed487ea84`。
原生 `eb7e1141c4409a16dba1cef8580caeea8b4ed2f93374f0567f6c07daf030e353` 与实际 ZIP
一致；APK 内 43 个 indexed Runtime 文件全部逐项 SHA 核验。封装 Succeeded，0 errors /
8 warnings（包括测试工程原有 Kinect Vulkan shader 警告），不是零警告声明。
该 APK 已成功安装到 OnePlus，原用户设置每次按字节恢复。

最终 APK 真机四档 CPU RTSP 回归使用同一 3840×2160 H.264 人物视频，解码源不变：

| 显示纹理 | 实际推理像素 | 输入发布/s | 含人体新结果/s | 骨骼位置 |
|---|---|---:|---:|---|
| 640×360 | 640×360 | 24.876 | 6.589 | 正常 |
| 1280×720 | 1280×720 | 24.733 | 6.105 | 正常 |
| 1920×1080 | 1280×720 | 24.293 | 6.108 | 正常 |
| 3840×2160 | 1280×720 | 24.577 | 6.015 | 正常 |

已读取实际 Running/Streaming 快照，确认 SDK 接受含人体结果且 adapter/bridge/manager/SDK
错误为空，并逐张检查截图。旧、最终两个包都做过矩阵；表中只取最终 APK 的 sealed
会话。无人体片头控制流的早期记录单独保留，未作为骨骼验收。
CPU/NPU 读回路径在 OnePlus 的累计回调墙钟约 48–49 ms，两个并发请求使 24–25 次/s
能够实际送达；不能把它当成串行每帧阻塞，也不能当成 RK3588 的回读时间。
CPU 视频功能试验新结果 8.651/s，SDK total 平均 115.053 ms；未宣称 CPU 达到 20–25。

最终 APK Vulkan 视频 150 s（排除前 15 s）完成 20.536/s，含人体 19.661/s；
backend 平均 45.854 ms，thermal status=3。此前连续 CPU/RTSP 测试使手机升温，
不能与 thermal=0 的旧记录直接计算提速或退化百分比。Vulkan RTSP 60 s（预热 10 s）
发布 18.510/s、完成 17.701/s、含人体 16.794/s，thermal=3；画面、骨骼和资源释放正常。
手机自然空闲至 thermal=0 后，同一个 APK 再跑视频 150 s：预热后完成
24.177/s、含人体 23.243/s，backend 平均 35.046 ms，含人体窗口 >=20 占 88.976%。
最低窗口 17.741/s，尾段再次出现 thermal=3。两次记录均保留，未通过持续稳定
20–25 的条件，不以冷机均值代替热机低帧，也不推断 RK3588 会得到相同速度。
新增详细 HUD 的“含人体新结果”
是真实事件率；旧状态栏 GPU FPS 字段未填充时可能为 0，不代表停止识别。

可复查命令/证据：
- `cmake --build build/windows-test --config Release`，
  `ctest --test-dir build/windows-test -C Release --output-on-failure`：414/414。
- `cmake --build build/android-dispatch-scoped-experiment --config Release`：ARM64 PASS。
- `python -m unittest discover -s tests/reference -p test_settings_device_log_analysis.py`：5/5。
- `python -m unittest discover -s tests/reference -p test_rknn_candidate_conversion.py`：6/6；
  Docker 中完整 RKNN/reference 25/25。
- `python tools/maintenance/check_architecture_boundaries.py`、
  `python tools/package/check_public_surface.py`、
  `python tools/test/verify_upm_git_newlines.py`、`git diff --check`：PASS。
- 实测项目证据：DiagnosticsVerification/RK3588-FieldRepair-20261010/，
  包括最终 APK identity、sealed-resolution-matrix.json、完整日志/截图/温度。
- 原生、RED/GREEN、数值门槛、分析输出位于 SDK ignored out/rk3588-field-20261010/
  和 out/rknn-optimization-20261010/，未写入公共发布包。

## 边界与参考

此包是私有 NPU/Vulkan/CPU 联合试验包；保留 vendor/model 原有发布边界。
OnePlus 无 RK3588 NPU，不能用于验证 rknn_run 或 RK3588 最终帧率。此次提供
的 RK3588 日志证明旧包 NPU 执行正常，新输入修复后的持续速度仍未在本机实测。
不承诺不存在测量依据的 20–25 FPS，也不以插值、重复绘制替代骨骼识别率。

已核对 [Rockchip 官方姿态转换示例](https://github.com/airockchip/rknn_model_zoo/blob/main/examples/yolov8_pose/python/convert.py)：
其中节点范围属于其自己的图，不能复制到本 SDK 恢复图。
[官方 Model Zoo](https://github.com/airockchip/rknn_model_zoo) 的模型 benchmark
依赖自己的输入、精度与测试条件，不能拿来保证 Unity/4K RTSP 的端到端速度。
[RKNPU 驱动负载实现](https://github.com/rockchip-linux/kernel/blob/develop-5.10/drivers/rknpu/rknpu_debugger.c)
用于解释逐核区间值；无法读取时保留 -1 和错误原因。
