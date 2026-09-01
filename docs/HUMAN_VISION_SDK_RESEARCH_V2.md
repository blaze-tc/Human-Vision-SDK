# HumanVisionSDK 快速 Demo 与长期架构研究方案 v2

> 日期：2026-09-01

## 结论

原始跨平台方向保留，但研发顺序改成“先验证垂直闭环，再产品化”。第一版不等待 Android、RKNN 或抠像，先在 Windows x64 + Unity 中验证普通 RGB/IPC 对 1~4 人场景的真实稳定性。

### D0/D1 基线

- RTMDet-tiny, 640x640, person only
- RTMPose-s, 256x192, COCO-17
- ONNX Runtime CPU baseline
- runtime `MaxBodies`, default 4
- lightweight replaceable tracker
- asynchronous latest-frame processing
- Unity local MP4 first, RTSP second
- HUD measures actual bottlenecks

### 为什么改变顺序

原计划把 Unity 放到较晚里程碑，导致大量底层工作完成后才第一次看到真实效果。新的顺序要求 D0.4 就出现 Unity 可视化；RTSP 紧接其后。Android/RKNN/Segmentation 只有在真实视觉效果值得继续时才进入开发。

## 核心架构

HumanVisionCore 仍然是跨平台核心；Unity 与输入源不耦合到模型实现。C ABI 使用动态 body buffer，单个 body 使用固定 COCO-17 joint schema。帧输入使用 `HV_SubmitFrame` 异步语义，worker 只处理最新帧，防止延迟堆积。

## 模型与未来抠像

D0 先用普通 RTMDet-tiny，不使用 RTMDet-Ins 阻塞骨骼 Demo。后续如果确实需要每人独立抠像，可评估 RTMDet-Ins-tiny；如果更关注边缘质量，则做独立 human segmentation/matting POC。公共 API 不提前绑定具体分割模型。

## 后续产品路线

D0/D1 验证成功后依次考虑：USB/WebCam、Tracker/Filter、Segmentation、Windows Beta、Android ARM64、RK3588/RKNN。ONNX 始终作为通用母版，RKNN 是可选 backend。

详细执行规则见本资料包其余 Markdown 与 `02_Codex_快速Demo开发实施方案_v2.docx`。
