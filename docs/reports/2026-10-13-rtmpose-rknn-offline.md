# RTMPose-t Body26：RKNN 替代模型离线验证

日期：2026-10-13。用户选择闭源商业项目优先替换 YOLO；本轮是可复现的转换可行性验证，
不是替代模型交付或 RK3588 性能验收。当前 Unity 项目、正式 profile、模型资源和 Release 均未替换。

## 实际结果

固定 RKNN Toolkit2 2.3.2，Docker `humanvision-rknn:2.3.2`，RK3588 编译目标。
使用已有真实七人视频帧和单人图像派生画布，按已有人工人框裁剪；每人原图、水平镜像、90°旋转，
合计 24 个真实人物裁剪。空场景没有人物裁剪，不计入姿态通过率。
这不是检测器测试，也不构成多人逐帧准确率、跟踪或时序稳定性验收。

| 路线 | 原精度门槛通过 | 观察 |
| --- | --- | --- |
| 完整模型默认 FP16 | 3/24 | 两个单人样本产生非有限输出 |
| NPU 卷积 + FP32 ORT 姿态头 | 3/24 | 无非有限输出，但关键点/置信度仍偏离 |
| 上述分段 + 外部 FP32 归一化 | 3/24 | 没有解决偏差 |
| 上述分段 + RKNN optimization_level=0 | 3/24 | 没有解决偏差 |
| 分段 + 大核池化等价改写 | **23/24** | 仍有一个镜像样本单关节点超限；总门槛不通过 |

所有候选均保留 `numerical_gate_passed=false`、`deployment_ready=false`、
`device_performance_verified=false`。没有用模拟器运行时间估算 NPU FPS。

## 找到的转换问题与有效改动

对 `seven-3-original` 运行 Toolkit 的逐层 `accuracy_analysis`，再用同一 RGB 输入比较
其 FP32 golden 与原始 ORT。输入最大误差约 1.19e-7；5×5 池化输出 `341` 最大误差约 2.67e-5；
**9×9 池化 `342` 最大误差跳到 6.481**，最终卷积特征 `378` 最大误差约 6.817。
因此这部分偏差已经存在于 Toolkit 转换后的 FP32 golden，不能全部归因于 NPU FP16 舍入。
此结论限定于该版本、该导出图和实测样本，未断言所有 RKNN 大池化都存在同一问题。

将同输入并行 5/9/13 池化改成串行三次 5×5、stride=1、padding=2，保留各尺度输出。
只重写经过名称、输入输出和全部属性校验的三个节点；训练权重和阈值不变。
正负值及边界测试逐位一致；真实 24 个裁剪的 ORT 分段/改写输出也逐位一致
（在同一 Linux ORT 进程、关闭图优化后比较）。RKNN 输出门槛由 3/24 改善到 23/24。

剩余失败是 `seven-2-mirror`：第 20 号关节点 x 轴 argmax 从 bin 201 变到 170，
y 保持 450；源图归一化距离 p95=0.002303、max=0.071393，超过原 max=0.03。
未删除该关节点、修改有效掩码、放宽阈值或以 COCO17 子集冒充 Body26 通过。
FP32 姿态头是诊断候选，尚未加入 Android 后端；不能据此宣称 CPU 成本可忽略。

## 合同与来源

- 输入：RGB，192×256；bbox padding=1.25，仿射裁剪，双线性。
- ORT：NCHW FP32，mean=[123.675,116.28,103.53]，std=[58.395,57.12,57.375]。
- RKNN 默认：原始 NHWC RGB uint8，转换中嵌入一次归一化；诊断模式才外部归一化。
- 分段：`378`、[1,26,8,6]，1248 个特征；后续 FP32 SimCC x/y 为 [1,26,384/512]。
- SimCC argmax / 2，逆仿射还原源图；复用 `compare_pose_outputs.py` 原有 Body26 门槛：
  有效点掩码一致，至少 3 点，距离 p95≤0.01/max≤0.03，置信度误差 p95≤0.02。
- 当前导出 ONNX SHA：`7d0075fbc44b2e696af21ac4555333fbb43405bf8dcc8d44c17c98d23818c710`。
- 固定形状 ONNX SHA：`df995f664f467fe54bfbf82ba8f758b0eb9b9412ab078bc0510f4d16e7b8843b`。
- 原图样本索引 SHA：`5ab8bb97034e957f3176162a63b471790b64514c7ba5a34d17622190167d4c34`。
- 本轮裁剪索引 SHA：`61dce27ec04c1c6a719a2d2c42c362945c88904da66b5d85f65e20d42657f89c`。
- 改写后候选 RKNN SHA：`0417fcbdfeae1d41270916528346cb6860ad76eb329b8d0f68218efb5f691b96`。

权重为已有官方 `rtmpose-t_simcc-body7_pt-body7-halpe26_700e-256x192-6020f8a6_20230605` 导出。
具体权重/分发许可状态见 [商业替代评估](2026-10-13-commercial-model-replacement.md)。
本轮未复制模型、图像、用户视频到 Git。

## 复现

Windows Python 环境为 `E:/Project/Human Vision SDK/.venv-reference/Scripts/python.exe`。
工作目录是本 Git worktree。必须使用新输出目录，禁止覆盖历史证据。

```powershell
& 'E:/Project/Human Vision SDK/.venv-reference/Scripts/python.exe' -m unittest discover -s tests/reference -p test_rtmpose_rknn_candidate.py
& 'E:/Project/Human Vision SDK/.venv-reference/Scripts/python.exe' -m tools.models.rknn.rtmpose_candidate prepare --source 'E:/UnityProject/Human-Vision-SDK-Test/Assets/StreamingAssets/HumanVision/Runtime/modelpacks/precision-t-26/body.onnx' --bank out/rknn-validation-20261009 --bank-sha256 5ab8bb97034e957f3176162a63b471790b64514c7ba5a34d17622190167d4c34 --output out/rtmpose-rknn-20261013/prepared-v3
```

容器挂载本 worktree 为 `/workspace` 后，在该目录运行：

```bash
python -m tools.models.rknn.rtmpose_candidate convert --prepared out/rtmpose-rknn-20261013/prepared-v3 --prepared-sha256 61dce27ec04c1c6a719a2d2c42c362945c88904da66b5d85f65e20d42657f89c --output out/rtmpose-rknn-20261013/split-pool5 --split-head --pool5
```

实际退出码 1：24 样本有 1 个未通过，符合 fail-closed 设计。不能把非零退出码改成成功。
去掉 `--pool5` 为分段基线；去掉 `--split-head --pool5` 为完整模型基线。
`--external-normalization` 和 `--optimization-level 0` 是已试过的诊断变量。

完整本地证据：`out/rtmpose-rknn-20261013/`，包含 prepared-v3、五次候选 receipt/输出/log，
及 `layer-analysis/accuracy/` 原始逐层数据。早期 prepared、prepared-unfused、split-v1
是中间失败记录，不可用于替代最终证据。

## 执行记录与后续边界

- 新增工具测试 8/8 通过；相关 RTMPose 测试共 16 项，9 通过、7 因既有本地 NCNN
  模型缓存缺失而跳过，不称全量通过。架构/公共边界与 `git diff --check` 通过。
- 既有 RKNN 30/30 在 Docker 内通过，显式使用
  `HV_RKNN_PROBE_HOST_BINARY=/workspace/out/rknn-toolchain/hv_rknn_probe_host`。
  Windows 首次未配置该变量时有 4 项报错，随后在正确 Linux probe 环境重跑通过。

- 先写 hash/形状/非有限/坐标/置信度拒绝测试，确认缺少实现时失败，再实现并通过。
- 分段边界、大池化等价与合同拒绝分别先失败再通过。
- Ruling：固定形状和图分段的逐位比较关闭 ORT 图优化，并在同一平台进程对比；
  Windows/Linux 内核差异使用原骨骼门槛单独检查。原因是避免把 ORT 融合/平台舍入误认为改图错误；
  代价是这项逐位检查不能代表生产优化后的 ORT 性能。
- Ruling：增加 NPU/FP32 分段和池化诊断，范围仅离线工具；原始整图候选失败后需要定位原因。
  代价是候选多了 CPU 头，不能从卷积模拟推算端到端速度。
- 下一步：定位剩余 Body26 点的精度差异、完成检测器和 RTMO-t 对照，再决定替换路线；
  需要后端接入、NCNN/RKNN 精度、实际 1/4/8 人测试和具体权重许可证据，才可替换发布包。

独立最终复核：无 Critical/Important；重新准备样本得到相同索引 SHA，重新计算全部保存的
候选输出得到相同通过数，复现池化逐层误差。Toolkit 状态失败、异常/空输出、非有限重跑及
重跑不一致的负向检查都保持拒绝。复核未重跑 Docker 编译，编译/模拟器证据来自本轮主执行。

- Final: minor (deferred)：`convert()` 的 Toolkit 状态失败及 repeat 拒绝路径已做独立检查，
  但尚未纳入持久单元测试；后续工具维护应补入。现有候选精度失败仍是发布阻断项。
- Final: Ruling：硬件吞吐/热行为、检测器及 RTMO 集成、完整运行时替换、具体权重再分发权
  不作为这个离线检查点已完成事项；代价是当前成果仍不能直接作为商业替代包交付。
