# RK3588 候选恢复、转换与验证

此目录是离线模型与独立设备工具。私有 SDK 已接入 backend.rknn 与
pipeline.yolo.tensor；2026-10-10 用户提供的 RK3588 日志确认 NPU 实际执行。
这不等于公开分发资格或新优化包的设备性能验收。
依据：[官方工具](https://github.com/airockchip/rknn-toolkit2)、
[官方姿态示例](https://github.com/airockchip/rknn_model_zoo/tree/main/examples/yolov8_pose)、
[SDK接入计划](../../../docs/plans/2026-10-09-rk3588-npu-acceleration.md)。
2026-10-09重启后Docker/WSL2已可运行；实际恢复图、非量化RKNN通过三样本门槛，
INT8三样本失败并拒绝采用。OnePlus现有Vulkan画面/骨骼测试通过，不能验证RKNN。
实际证据、哈希、OnePlus指标和设备包位置见
[本轮报告](../../../docs/reports/2026-10-09-rknn-offline-validation.md)。

## 固定环境

`Dockerfile` 固定Python3.10基础镜像digest及官方2.3.2 wheel SHA。
`requirements.lock.txt` 固定主要依赖，完整实际transitive版本另存pip-freeze receipt。
wheel来自Rockchip revision `42aa1d426c0a9e0869b6374edba009f7208a1926` 的
`rknn-toolkit2/packages/x86_64/`，文件名为
`rknn_toolkit2-2.3.2-cp310-cp310-manylinux_2_17_x86_64.manylinux2014_x86_64.whl`。
将wheel和本目录requirements.lock.txt放入本地`out/rknn-toolchain/`，从仓库构建：

```powershell
docker build -f tools/models/rknn/Dockerfile -t humanvision-rknn:2.3.2 out/rknn-toolchain
docker run --rm --memory=4g --cpus=4 --mount "type=bind,source=$((Get-Location).Path),target=/workspace" humanvision-rknn:2.3.2 python -c "import rknn.api, onnx, onnxruntime, torch; print(torch.__version__, torch.version.cuda)"
```

NCNN源文件在Windows junction外时，先把真实字节复制到挂载目录内；容器不能把
外部junction当作已存在的模型。缓存、vendor头/库和候选不进入Git或公开发布物。

## 源图与数值门槛

`recover_pinned_onnx.py` 仅接受当前固定NCNN图/权重SHA及其11类算子合同，
消费所有权重字节。输出静态Low RGB `[1,3,288,512]`，外部归一化 `/255`，
保留两个原始张量`[1,3024,65]`和`[1,3024,51]`，不添加人体结果或手部点。
恢复receipt仍是候选，必须真实执行ORT对比才证明样本数值一致。

```bash
python tools/models/rknn/recover_pinned_onnx.py \
  --param out/rknn-validation-20261009/source-ncnn/yolov8n_pose.ncnn.param \
  --bin out/rknn-validation-20261009/source-ncnn/yolov8n_pose.ncnn.bin \
  --output out/rknn-next/recovered.onnx
```

`simulator_gate.py` 校验固定bank的index/输入/reference/执行日志SHA、张量形状与
精度门槛。bank有七人、一人、空画面；空画面为114分析性负样本，一人是实际
人物图片的填充控制。两者不是完整实机数据覆盖。`convert_candidate.py` 搭配
`--validation-index`与`--validation-index-sha256`先做ORT gate，再实际编译、
导出并在同一个RKNN实例中运行PC simulator；所有API返回码均检查，失败保留证据。

```bash
python tools/models/rknn/convert_candidate.py \
  --onnx out/rknn-validation-20261009/recovered-low.onnx \
  --source-sha256 6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3 \
  --validation-index out/rknn-validation-20261009/index.json \
  --validation-index-sha256 5ab8bb97034e957f3176162a63b471790b64514c7ba5a34d17622190167d4c34 \
  --output out/rknn-next/non-quantized
```

使用Linux和固定 `rknn-toolkit2==2.3.2`，需要ONNX库。输入为有来源/固定SHA的
原始RGB YOLO式ONNX：静态NCHW、batch1、3通道，H/W为32倍数、不超过960。
工具固定RGB输入、mean0/std255；源图必须使用此归一化合同，已带图内/255的
模型不能再使用此配置。它记录实际输出名称/形状，不假设可以复用NCNN两输出解码。

```bash
python tools/models/rknn/convert_candidate.py \
  --onnx /models/pinned-pose.onnx --source-sha256 <actual-64-character-sha256> \
  --output out/rknn/non-quantized

python tools/models/rknn/convert_candidate.py \
  --onnx /models/pinned-pose.onnx --source-sha256 <actual-64-character-sha256> \
  --precision int8 --calibration /models/calibration.txt --output out/rknn/int8
```

校准列表每行一张真实已预处理的RGB图片路径，相对路径相对于列表目录；路径不要
含空白。校准数据需覆盖实际人物大小、多人、姿态、光照和摄像头；文件存在检查
不证明数据代表性。空白/缺失/伪造结果不能满足精度门槛。
输出目录必须全新，失败时保留候选目录供诊断；成功保留模型哈希、源图、校准
图片哈希和转换器版本。非量化请求不保证所有算子都为同一浮点精度。

转换产物始终 `deployment_ready=false`，不修改已发布包、不产出已验收profile。
官方姿态INT8示例的特定混合量化节点不能直接套到另一导出图；如果INT8精度
失败，按实际图做独立混合量化候选并复测，不放宽关节/置信度门槛。
当前INT8 99帧校准实验的坐标偏差约8–10像素，超过既有3像素门槛，不能使用。
PC模拟器不给RK3588设备速度/热/驱动结论；非量化通过不等于部署资格。

## 独立设备工具

`device_probe.cpp` 动态加载私有RKNN运行库，检查实际tensor/driver/core mask。
输入RGB uint8 NHWC，输出预分配FP32缓冲区复用，错误明确失败，无CPU回退。
不使用返回上一帧的ASYNC_MASK，也不使用影响性能的COLLECT_PERF_MASK。
`run_device_probe.py` 在上传前校验SHA/路径、匹配候选精度receipt并确认RK3588；
OnePlus只会得到拒绝报告。默认mask1/7各运行三个控制，每个30次warmup/300次测量，
保存全部原始输出并应用原门槛。它不修改APK/设置/固件全局库，远端UUID缓存保留。

编译私有probe需要固定vendor header（不随仓库分发）和Android ARM64 NDK/API26：

```powershell
& "$ndkBin/clang++.exe" --target=aarch64-linux-android26 -std=c++17 -O2 -Wall -Wextra -Werror -static-libstdc++ -Iout/rknn-toolchain/vendor/include tools/models/rknn/device_probe.cpp -ldl -o out/rknn-toolchain/hv_rknn_probe_android
py -3.13 tools/models/rknn/run_device_probe.py --help
```

`$ndkBin` 是当前Unity NDK的`toolchains/llvm/prebuilt/windows-x86_64/bin`。
已构建私有设备包的路径、调用命令与文件职责见本轮报告。bundle.json的`files`
是每个相对文件的SHA256；`probe/runtime/candidate/index/offline_report`角色均必须
绑定其中的文件。bank本身仍使用已冻结的NCNN reference合同。

结果记录inputs_set/run_call/outputs_get_wait/输出检查与释放/total/vendor_perf_run
的mean/P95/max。它是重复静态图片模型基准，不能转换成SDK视频的新骨骼FPS。
runtime版本/驱动错误/无效shape/非有限输出会失败，原始日志和输入输出全部保留。
正式backend、UGUI模式和端到端性能验收仍按接入计划推进。

保护性自动测试：

```bash
python -m unittest tests.reference.test_rknn_candidate_conversion tests.reference.test_rknn_ncnn_recovery tests.reference.test_rknn_simulator_gate tests.reference.test_rknn_device_bundle
HV_RKNN_PROBE_HOST_BINARY=/path/to/compiled/host/probe python -m unittest tests.reference.test_rknn_device_probe
```

host probe使用Linux g++、相同vendor头和`-std=c++17 -O2 -Wall -Wextra -Werror -ldl`。

## 图绑定混合量化候选（2026-10-10）

使用 `--precision hybrid --calibration calibration.txt --hybrid-config hybrid.json`。
配置只接受 schema_version=1、实际 source_onnx_sha256 和 ranges 三个字段；
ranges 是最多64个 `[start_tensor,end_tensor]`，必须是此图真正产出的张量、
沿图路径存在并且不重复。不能照搬官方示例其它图的节点名称。

```json
{"schema_version":1,"source_onnx_sha256":"实际64位SHA","ranges":[["实际起点输出","实际终点输出"]]}
```

工具实际调用 Toolkit2 hybrid_quantization_step1/step2，校验返回码及
.model/.data/.quantization.cfg 中间件，再执行原 simulator gate。Toolkit 可能
优化掉源图别名；这种 SDK 端失败必须保留，不能冒充已生成模型。
2026-10-10 三个导出候选仍未通过原始张量/关节门槛，第四个范围在导出前被
Toolkit 拒绝，全部不进入设备包。校准来自99个真实视频帧，但单一视频并不
证明数据代表性。参见 [现场修复报告](../../../docs/reports/2026-10-10-rk3588-field-repair.md)。

`tools/benchmark/rknn_preprocess_probe.cpp` 对比真实旧缓存系数循环与新精确
NEON 输入预处理，包含四种像素格式及 padded stride；它不执行模型。
必须先逐字节相同，再报告实际壁钟。OnePlus 的 CPU 预处理速度不能标记为
RK3588 NPU FPS。native `YoloRgbPreprocess.*` / Python `test_rknn*.py` 是维护入口。
