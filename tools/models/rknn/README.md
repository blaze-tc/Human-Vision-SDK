# RK3588候选转换

此目录目前仅有候选转换工具，尚无可运行的SDK RKNN后端或合格NPU ModelPack。
依据：[官方工具](https://github.com/airockchip/rknn-toolkit2)、
[官方姿态示例](https://github.com/airockchip/rknn_model_zoo/tree/main/examples/yolov8_pose)、
[SDK接入计划](../../../docs/plans/2026-10-09-rk3588-npu-acceleration.md)。

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
当前Windows主机Docker/WSL2缺可用虚拟化，未执行转换。保护性自动测试：
`python -m unittest tests.reference.test_rknn_candidate_conversion`。
