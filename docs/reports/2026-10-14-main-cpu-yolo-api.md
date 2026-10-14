# 2026-10-14 恢复版 main 的 CPU YOLO 与 API 封装核验

本轮源基线为 main `ba1815abe8fa07a134fda8c308a8d1312b5a05fd`，开发版本
SDK 0.4.0-preview.8 / Input 0.1.0-preview.5。原恢复版 APK、47 项资源和公共 preview.7 Release 保留。
最终候选修复 APK 已构建并安装 OnePlus；本报告末节记录其实际对照结果。
公共 Release 未替换，SDK 导入包和完整私有资源包均有独立来源和校验和。

## 实现范围

Android CPU 通过 `pipeline.yolo.cpu` 使用单个 YOLOv8n-pose FP32 ONNX，512×288 固定输入，
支持 1–8 人。CPU 配置不再按人数切换 TopDown/RTMO。RGB uint8 经复用的 NCHW 缓冲归一化一次，
114 填充的 letterbox 支持 16:9、4:3、竖屏和奇数尺寸；继续使用现有 DFL/COCO-17 解码及追踪。
CPU 插件热路径缓冲复用已测试，通用 ORT 后端仍可能分配内存，不宣称整个 SDK 零分配。

Vulkan 与 NPU 的原模型、默认几何变换及资格门禁保持原样。新 Android ELF 保留原 dispatch
及 RKNN input-memory 构建参数。Input、ORT、RKNN vendor、Windows 库、原模型和 importer
等 65 个保护文件前后哈希一致。Windows CPU/DirectML 配置及 native DLL 未更换。
V1/既有 C 与 Unity 导出没有删除，新增加 CPU 插件查询导出；最终 C/Unity 导出为 51 项。

实机初测发现新增 CPU 没有沿用全画面候选预算，先截四人会引起 ID 变化。已把
`pipeline.yolo.cpu` 纳入现有候选预算：最多八个真实候选先进入公共追踪，再按公开人数和区域选择。
单次模型执行次数、TopDown ROI 数、Vulkan/NPU 条件不改变。真实 seven 控制的原始候选为七，
公开上限四仍输出四；候选顺序变化的 60 轮保持四条身份。

公共 Unity API 签名保持不变。`Initialized` 移到成功提交配置之后：回调能读取本次配置，
仅准备为 Ready，打开输入成功为 Running，输入失败不触发成功回调。

## 已完成验证

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| CPU 插件注册 RED | 新插件未注册时确实失败 | `out/yolo-cpu-native-red.log` |
| CPU 非 16:9 RED | 4:3 用例在修改前确实失败 | `out/yolo-cpu-native-letterbox-red.log` |
| CPU/原 NPU/RGB 原生目标用例 | 29/29 | `out/yolo-cpu-native-green.log` |
| 最终相关可用原生回归 | 91/91 | `out/yolo-cpu-native-candidates-regression.log` |
| 真实 CPU 七候选/公开四人 RED → GREEN | 原始候选四导致失败 → 两例通过；真实 ORT、公共追踪和区域 | `out/yolo-cpu-candidates-{red,green}.log` |
| 历史 GPU 夹具 | 28 项缺少两处磁盘上的旧归档，未通过/未验收 | `out/yolo-cpu-native-regression-first.log` |
| Unity CPU 部署 guard RED | 7 passed /14 failed /0 skipped；新模型被旧 guard 拒绝 | `cpu-model-contract-red.json` |
| Unity CPU 部署 guard GREEN | 22/22，0 skipped | `cpu-model-contract-green.json` |
| 初始化回调与失败配置 PlayMode | RED 后 5/5 | `initialized-event-green.json` |
| 最终 Unity 生命周期/查询 PlayMode | 20/20 | `managed-lifecycle-green.json` |
| Unity 可用 EditMode | 首跑 402 passed /1 failed /40 skipped；持久化模式测试修正后单独 1/1 | `managed-editmode-broad-first.json`、`managed-mode-selection-green.json` |
| 新 Android 编译与已部署库审计 | AArch64、API26、506 个强导入全部闭合 | `out/main-cpu-android-candidates-audit.log` |
| 资源/补充包 Python 测试 | staging 16/16、addon 9/9、原 addon 5/5、相关 package 45/45 | 本轮工具测试日志 |
| 完整本地补充包 | 52 项 Runtime；SDK/Input GUID 分区及逐文件哈希通过 | `out/main-cpu-private-addon-preview8-candidate-fix-20261014/manifest.json` |

真实 FP32 输入的 RGB/BGR/RGBA/BGRA 和带步幅图像逐元素一致；真实 ORT 的 seven/one/empty
及 4:3/竖屏/奇数竖屏输出与参考张量匹配；原图框和 17 点误差检查上限 0.002 像素。
16:9 CPU 路线与原 NPU 默认解码几何的等价性单独检查，不改变原 NPU 的初始化/几何约束。

## 部署身份

- Android SDK ELF：`32ce9715ad8a6daae5b47dd5d628e6340f354715eca24c11b84f651407694804`。
- Input：`0639a50ecbe765bda84a4dbc6616bd9f5fcedbf51839e2dfb7c1588650511457`。
- ONNX CPU 模型：`6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3`。
- RKNN driver：`07a8398be5caed21a1998ffa313e0425bb9e76b154a8dbbc667d71722c009351`。
- RKNN 低档模型：`1b3ba8dd5bb81e3d968cfeef019c42b1aa3bffca6f0ec1c9f89f257fe0c08030`。
- 完整本地 Runtime index：`24fedd115e8365fa22e56436dd3b1ec5ea16b00683af5a15e62286aed2974835`。

本地补充包及私有 APK 不上传公共 Release。原始模型资格标记不变。
OnePlus 仅能提供 CPU/Vulkan 的实机证据，RK3588 NPU 的设备兼容和性能仍由 RK3588 验收。

## APK 与测试边界

最终测试包 `HumanVisionSettingsDemo-YOLO-CPU-GPU-NPU-preview8-candidates-fixed.apk`：
303,966,808 字节，SHA-256 `9e42ec27863bfcf220109b77763a763d04199ea7750ca069fa09d5e07a81e595`。
Unity 2021.3.45f1 构建 0 errors /3 warnings；包内 52 项 Runtime 逐文件哈希、
SDK/Input/ORT/RKNN 单一 ARM64 库所有权及准确版本通过。旧恢复包和早期 CPU 包分别保留。

EditMode 的 40 项跳过来自打包环境及外部资格夹具，不计为通过。早期深目录的无界面 clone
测试有路径长度/外部夹具/无界面渲染失败；它不是实际项目的最终回归收据。
实际项目保持已保存的 HumanVisionSettingsDemo，结束时检查编译错误与场景状态。

CPU 与 Vulkan 在实际项目中为同一个视频源和人数设置，但 Linear 项目下 CPU 的 sRGB
回读目标和 GPU 的 Linear 目标不同；未取得同一冻结帧最终张量逐像素一致证据，
不宣称 CPU/GPU 数值完全等价。本次保留原 GPU/NPU 颜色与模型合同。

本次不会以显示插值、Unity 刷新率或 NPU 利用率作为骨骼性能结果。
OnePlus 对照每阶段预热 30 秒、测量 30 秒，避免 video-1.mp4 前段片头；仅截图，
没有屏幕录制。各阶段热状态记录于原始日志；顺序短测不能证明恒温提速比例或长时稳定。

## OnePlus 最终同包实测

设备 LE2120（连接序号 e7c07019），Unity Settings 场景，视频25 FPS、1024×576。
完整收据、日志、JSON/CSV表格和截图目录：
`E:/UnityProject/Human-Vision-SDK-Test/DiagnosticsVerification/cpu-yolo-main-20261014/oneplus-candidates-fixed`。


视频 1024×576 /25 FPS；每阶段预热30秒、测量30秒；4人上限；只截图。

| 模式 | 输入 FPS（采样中位） | 新骨骼结果 FPS | 主要逐人新骨骼 FPS | 主要逐人完整 COCO-17 FPS | 模型耗时 ms（采样中位） | 结果年龄 ms（主要逐人均值） | 轨迹 ID 总数 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 旧 CPU / RTMO | 24.69 | 7.16 | 4.80–7.16 | 0.00–0.30 | 130.34 | 222.28 | 5 |
| YOLO CPU | 24.69 | 14.23 | 13.80–14.23 | 11.97–12.60 | 67.38 | 153.68 | 5 |
| Vulkan 低档 | 24.76 | 24.60 | 24.53–24.60 | 21.16–21.90 | 33.72 | 58.77 | 4 |
| YOLO CPU 镜像 | 24.68 | 13.39 | 11.33–13.39 | 10.00–12.63 | 73.64 | 157.71 | 6 |

“主要逐人”指该阶段观测次数最多的四条身份，分母是整个30秒窗口；所有身份的原始计数见各阶段JSON。完整17点要求本次观察、非派生、非预测、有效且有限；重复源帧不计数。
结果年龄是本地管线年龄，不含摄像头曝光/网络全链路。模型时长来自每秒采样，非逐次执行的恒温均值。
CPU修复前同窗产生21条身份，最终为5条；仍有少量替换，镜像为6条，不宣称完全零身份变化。

热状态：
- 旧 CPU / RTMO：CPU传感器 66.8–70.1°C；PowerManager thermal status=3。
- YOLO CPU：CPU传感器 66.1–70.4°C；PowerManager thermal status=3。
- Vulkan 低档：CPU传感器 58.2–61.2°C；PowerManager thermal status=3。
- YOLO CPU 镜像：CPU传感器 65.8–71.7°C；PowerManager thermal status=3。

四阶段截图均有画面/骨骼，镜像位置跟随；有限坐标错误为0。不同热状态下的顺序短测不是固定提速比例或长时稳定验收。Vulkan此次完整17点约21FPS/人，仍需长时测试；RK3588 NPU本轮未连接。

四阶段完成收据是运行完成标记，并非所有性能目标均已通过：CPU仍未达到20–25新骨骼FPS；
Vulkan本轮30秒内主要四人完整17点为21.16–21.90 FPS，但没有长时/恒温验收。
CPU模型执行采样中位约67.38ms，明显是其约14FPS的主要限制；输入仍约25FPS。
Vulkan原模型保持，回调统计的原始新结果24.60FPS；旧GPU stats.InferenceFps=0不用于本表。

最终导入组合：Input 0.1.0-preview.5 + SDK 0.4.0-preview.8 +
`HumanVision-LocalAndroidCpuResources-0.4.0-preview.8.unitypackage`。
完整addon SHA-256 `e65b1e1ede64de39a790e5b53d86a2fdefeda91c2abd0d0879c2c3c83bd41c03`。
权重及RKNN vendor库仅在本地完整资源包/APK中，不上传Git或公共Release。
独立最终代码审查无Critical/Important阻塞；恢复版 Vulkan/NPU/Windows源与受保护资源保持原样。
