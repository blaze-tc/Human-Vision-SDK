# RK3588双模式加速接入计划（用户2026-10-09授权）

目标：HumanVisionSettingsDemo在Android可选择NCNN Vulkan、RK3588 NPU或CPU，
PC可选择GPU/CPU，独立“计算模式”一行，保持业务骨骼API、
区域、稳定ID、初始化/停止语义；以20–25个真实新结果/秒为当前优化目标。
这是尚未完成的接入计划，不是NPU功能/性能验收。

## 模型与基准先行

1. 固定Linux RKNN-Toolkit2 2.3.2、来源ONNX/训练权重SHA、输入/输出合同。
   当前NCNN模型资产不能直接加载到NPU。原始权重/导出图必须证明等价，或明确
   作为新模型比较；不能把官方另一导出图的输出直接交给现有两输出解码器。
2. 先生成非量化候选作为精度对照，再生成真实校准集的INT8/混合精度候选。
   使用已有1人/7人/空画面、抬臂及实测视频帧；记录框、人数、关节误差和置信度，
   保持既有门槛，不修改golden阈值来让候选通过。
   YOLOv8n-pose的17个直接人体点不包含真实Hand/Handtip/Thumb；加速不会补齐
   缺失关节，不把这次body性能目标写成完整手部/32关节验收。
3. 转换工具 `tools/models/rknn/convert_candidate.py` 只产出候选与固定输入哈希
   receipt。`deployment_ready/numerical_gate_passed/device_performance_verified`
   均为false，真实后续验证单独给出证据。
4. RK3588上先独立模型runner：记录SDK/driver版本、tensor类型/stride/输出形状、
   实际core mask、warmup、模型时间和温控；单核/多核分别测，拒绝静默CPU回退。

## 后端与输入

在 `runtime/plugins/backend/rknn` 添加算法无关后端，以既有版本化插件ABI
注册。RKNN类型和张量不进入Unity公共API，也不进入Host算法分支。
模型格式、归一化、输出名称和量化参数来自独立ModelPack合同；不改V1 ABI。
RKNN创建、运行、取输出及销毁均在有明确生命周期的原生工作线程执行。
上下文、输入/输出缓冲区复用；每步检查错误，借用输出在下次run前复制/解码。

初版对现有Vulkan输入提供有测量的预处理与张量交付适配，保留原AHB/sync fd
所有权和generation拒绝语义。Vulkan仍使用原有GPU链路；私有NPU原型允许受限
异步GPU读回，必须保留源帧年龄、禁止主线程ReadPixels、限制在途缓冲并测量成本。
DMA/RGA可作为后续优化，必须证明fd、stride、格式、
缓存一致性及完成同步，单独记录每段成本；不在首版直接宣称零拷贝。

每帧测量input观察、预处理、输入交付、NPU等待、输出读取/反量化、骨骼解码、
Host发布；报告采样帧号和时间域。NPU模型时间与端到端时间分开。
运行库采用已验证Android ARM64依赖：记录来源/许可/SHA、动态加载失败和
目标driver不兼容原因；不能直接覆盖固件全局librknnrt。

## 配置与UGUI

业务配置使用语义加速预设，Android界面展示NCNN Vulkan、RK3588 NPU、CPU；
PC复用UseWindowsCpu展示GPU/CPU。新增Cpu枚举值不改变已有Graphics/Neural值。
每个预设绑定独立合格profile/ModelPack。后端模式与模型等级、输入分辨率分别保存。
日志保存requested/actual/backend/model/precision/core/版本；强制NPU失败时明确报错，
不能悄悄用Vulkan后仍显示NPU。默认保持现有NCNN Vulkan。

APK可同时含两个后端时，Android metadata使用通过测试的显式profile白名单，
不接受任意字符串绕过既有模型/依赖校验。后续用户要求的CPU复用已安装
android-cpu-nohands、backend.ort.cpu和真实容量模型，纳入精确五profile白名单。
CPU等级固定并显示实际模型尺寸，不冒充Vulkan同模型性能。切换前先验证能力、资源与合同；失败
保留当前会话。成功后退役旧输入/工作者/模型会话，再建立新会话，不共享旧generation。
UGUI不伪造已安装/支持状态；缺模型、非Rockchip硬件、driver失败分别解释。

## 验收与当前阻塞

自动验证：V1/C ABI不变；后端失败/无回退；模型哈希/输出边界；热切换取消与退役；
配置保存/恢复；Android依赖闭包；已有NCNN、Windows和Input回归；架构文档一致。
物理验证：相同RK设备/视频、相同可见人物与设置，新旧模式至少预热30秒后长测；
报告新结果/逐人速率、P95年龄、输入发布、框/关节精度、CPU/GPU/NPU有效遥测及热状态。
另测相同RTSP，并区分上游4K解码与GPU竞争；不能把录屏/插值率当推理率。

2026-10-09重启后的实际进展：Docker/WSL2已工作，固定NCNN图/权重恢复ONNX的
ORT比较通过3/3，非量化RKNN的PC simulator通过3/3；INT8实际99帧校准候选
失败3/3并保留拒绝证据。来源/张量/精度工具和独立ARM64/API26设备probe已生成。
详见[离线报告](../reports/2026-10-09-rknn-offline-validation.md)。
本机仍没有连接RK3588。用户随后明确要求现在完成真实backend/pipeline/UGUI
双模式、私有测试APK，先在OnePlus验证Vulkan与不支持NPU时的会话保留。
该直接指令覆盖之前先硬件后集成的顺序。RK3588的driver、三样本输出、mask1/7
模型时间及端到端性能仍由用户后续实测；OnePlus不能验证Rockchip RKNN。
既有300秒结果属于旧APK，更新后的APK必须重新验证。
现场旧日志已经足够定位大瓶颈，不再以采集同类旧日志代替真实转换/运行验证。
