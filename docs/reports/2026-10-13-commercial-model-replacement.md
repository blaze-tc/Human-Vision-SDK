# 闭源商业项目：替换 YOLO 模型的首轮评估

用户决定：没有 Ultralytics Enterprise 授权，优先评估替换模型。评估日期 2026-10-13。
首轮是代码/模型结构与上游资料检查；后续已完成 RTMPose-t 实际 RKNN 转换实验，
尚未验收或接入替代 NPU 模型。见 [离线转换结果](2026-10-13-rtmpose-rknn-offline.md)。

## 结论

优先验证 **RTMDet-nano + RTMPose-t** 的固定形状 RKNN 路线，以 **RTMO-t** 作为多人吞吐对照。
这两条路线能复用已有 Host/pipeline 与语义 API。当前 Android CPU profile 已按人数选择
OpenMMLab TopDown/RTMO；不能把所有 CPU/GPU/NPU 模式都笼统视为 YOLO。
当前 NCNN Vulkan/NPU 所用的 YOLOv8 权重及转换结果才是明确需要替换的部分。

MMPose/MMDetection 源代码采用 Apache-2.0，适合作为候选起点；**代码许可不等于某个权重、
预训练来源及模型分发权已核实**。当前本地 manifest/SOURCES 自身也保留了权重/数据条款待核实。
不直接把已有 Body7/Halpe26 权重改名为“可闭源商用”，不自动改变项目许可证。

## 可复用现状与技术风险

| 候选 | 本地事实 | RK3588 需要验证 | 人数成本 |
| --- | --- | --- | --- |
| RTMDet-nano + RTMPose-t | 已有 320×320 detector、256×192 pose ONNX，TopDown pipeline；历史 NCNN 转换/golden 工具 | detector 原始头导出、NMS 留 CPU；pose 固定 batch/输出；RKNN 编译和精度 | 检测一次，姿态约随人数增加；必须实测 1/4/8 人 |
| RTMO-t | 已有 416×416 官方 ONNX、pipeline.rtmo，Windows/Android CPU 配置已使用 | 拆出动态后处理或静态重导出；NPU/CPU 分界与 NCNN 可执行性 | 单阶段多人更值得对照，不能套用 V100 数据预测 RK3588 |

2026-10-13 在本地真实模型上运行 `onnx.checker.check_model` 均通过，结构统计：

- RTMO-t：729 节点，输入 batch 符号维；输出人数/形状动态；包含 NonMaxSuppression、TopK、
  Range、Erf、Sin/Cos、MatMul 等。存在这些节点不等于必然不支持，但不能认定原图直接可跑 NPU。
- RTMPose-t Body26：168 节点，256×192 输入；SimCC 输出 384/512；batch/关节点维为符号维。
  有 ReduceSum/Sqrt/Div/MatMul，需固定形状并测试精度，不能通过删层假装支持。
- RTMDet-nano：778 节点，320×320 输入；现有 ONNX 已含动态 NMS/TopK/Range，
  RKNN 初始路线应导出固定原始检测头，并复用受测试的 CPU 解码。
- Rockchip 官方 model zoo 当前 main 树未检索到以 rtmdet/rtmpose/rtmo 命名的样例；
  MMDeploy 提供 RTMPose RKNN FP16 256×192 配置。这是转换入口依据，不是本项目 RK3588 性能证明。

## 必须同时满足的替换门槛

1. 固定具体权重 URL/SHA、作者许可、训练/预训练来源，核实闭源部署与随 SDK 分发的依据。
   若找不到充分依据，保留为技术候选；使用权属明确的数据训练或继续筛选，不用下载可得性代替许可。
2. 相同原始图像和视频，0/1/4/7/8 人、遮挡、前排选择与区域身份；对照真实精度和漏检。
   17 点方案不能伪装成真实 Handtip/Thumb 能力。
3. 完成 ORT → NCNN/RKNN 的实际输出对照；保留每帧身份/坐标/镜像/分辨率转换与迟到结果规则。
4. 保留已优化的解码/预览/异步回读/单实例调度；量化只有通过原精度门槛才能采用。
5. OnePlus 检查 CPU/Vulkan，RK3588 检查 NPU；统计逐人新骨骼 FPS、完整率、延迟及持续温度。
   不用插值/显示帧率/单人姿态模型 FPS 代替多人端到端 20–25 FPS。

## 官方依据

进一步核实：MMPose 协作者在 [#2393](https://github.com/open-mmlab/mmpose/issues/2393)
明确说明 RTMPose 实现为 Apache-2.0，项目允许商业使用。
成员在 [#2106](https://github.com/open-mmlab/mmpose/issues/2106) 对 COCO 预训练权重商用给出
正面但带限定的说明。这是许可依据，不能忽略；也不能自动扩展为所有 Body7/Objects365 权重
以及 SDK 再分发方式已获逐项确认。
[#3273](https://github.com/open-mmlab/mmpose/issues/3273) 恰好询问本候选 RTMDet-nano
同 SHA 权重及训练来源，截至本轮查询未见维护者答复；提问本身不构成禁止使用的证据。

- [Ultralytics 模型许可路线](https://www.ultralytics.com/license)
- [MMPose Apache-2.0](https://github.com/open-mmlab/mmpose/blob/main/LICENSE)
- [MMDetection Apache-2.0](https://github.com/open-mmlab/mmdetection/blob/main/LICENSE)
- [RTMPose 模型/部署说明](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmpose/README.md)
- [MMDeploy RKNN 静态 FP16 配置](https://github.com/open-mmlab/mmdeploy/blob/main/configs/mmpose/pose-detection_simcc_rknn-fp16_static-256x192.py)
- [RTMO 官方模型与部署说明](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmo/README.md)

官方 RTMPose pipeline benchmark 默认 detector 间隔 5 帧；RTMO 表格延迟是在 V100/ORT 上测得。
这些数字不能作为本项目 4/8 人、RK3588 NPU 的预计 FPS。固定形状 RTMPose 离线实验已开展，
下一阶段需解决剩余精度失败、完成检测器/RTMO 对照与具体权重许可核实，再决定最终替换包。
