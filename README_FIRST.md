# HumanVisionSDK Codex 快速 Demo 资料包 v2

> 版本日期：2026-09-01  
> 当前目标：先完成 Windows x64 + Unity 的可测试多人视觉 Demo，再进入正式跨平台 SDK 产品化。

## 先读什么

Codex / 开发人员进入项目后，严格按以下顺序阅读：

1. `AGENTS.md`
2. `docs/DEVELOPMENT_STATUS.md`
3. `docs/DEMO_SCOPE.md`
4. `docs/ARCHITECTURE.md`
5. `docs/MODEL_MANIFEST.md`
6. `docs/SDK_API.md`
7. `docs/TOOLCHAIN.md`
8. `docs/CODEX_DEMO_EXECUTION_PLAN.md`
9. `docs/LONG_TERM_ROADMAP.md` 仅用于理解长期方向，不得提前实现。

## 第一阶段必须看到的结果

第一版可测试 Demo 的完整链路：

```text
Local MP4 / Unity VideoPlayer
        -> RGBA Frame
        -> HumanVision Native DLL
        -> RTMDet-tiny
        -> simple tracker
        -> RTMPose-s
        -> Latest Result
        -> Unity
        -> Video + BBox + TrackId + COCO17 Skeleton + HUD
```

随后只增加：

```text
RTSP IPC -> 同一 HumanVision Pipeline -> 同一 Unity Demo
```

## 第一阶段明确不做

- Android / iOS / Linux / macOS
- RKNN / RK3588
- TensorRT / CUDA 优化
- INT8 / FP16 量化
- 人像抠像 / Segmentation / Matting
- 动作识别、手势识别、人脸识别
- 完整 Interaction Demo
- 大规模架构重构

这些能力保留在长期路线中，但不得阻塞 Demo。

## 验收优先级

1. 能否稳定检测真实 1~4 人。
2. 远距离人体是否仍能稳定检测与出骨骼。
3. 背景复杂时误识别是否可接受。
4. 多人靠近/交叉时 Track ID 是否基本稳定。
5. Unity 是否不卡主线程，端到端延迟是否持续累积。
6. RTSP 断线是否不崩溃并可恢复。

## 必须由测试数据回答的问题

不要在文档里承诺“最多 4 人”。`MaxBodies` 是运行时配置；第一轮只是默认用 4 人测试。

建议至少准备：

- `tests/testdata/person_1.mp4`
- `tests/testdata/person_2.mp4`
- `tests/testdata/person_4.mp4`
- `tests/testdata/far_person.mp4`
- `tests/testdata/crossing_2.mp4`
- `tests/testdata/occlusion.mp4`

如果目前没有完整素材，D0 可以先用 1 个单人视频 + 1 个多人视频启动，不要等待全部测试资产齐备。
