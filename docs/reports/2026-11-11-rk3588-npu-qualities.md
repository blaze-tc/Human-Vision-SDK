# RK3588 NPU 三档接入

| 档位 | 模型尺寸 | 两路输出行数 | 16:9 图像上下各填充 |
| --- | --- | --- | --- |
| 低 | 512×288 | 3024 | 0 px |
| 中 | 640×384 | 5040 | 12 px |
| 高 | 960×576 | 11340 | 18 px |

三档使用 YOLOv8n-pose、非量化 RKNN 和 RGB UINT8 NHWC 输入。中、高档从原 NCNN
权重恢复为静态 ONNX，再用 RKNN-Toolkit2 2.3.2 为 RK3588 编译。低档沿用已验证的
模型和回执。每档绑定独立的 Profile、ModelPack、模型哈希和两份离线回执。后端为
`backend.rknn`，初始化失败会返回错误。独立手部模型关闭。
高档的两路 FP32 输出合计约 5.02 MiB，超过现有共享内存资格检查的 4 MiB 上限，
因此走常规 RKNN 输出读取路径。

使用 Assets 安装时，先导入 `HumanVisionInput-0.1.0-preview.6.unitypackage`，
再导入 `HumanVisionSDK-RK3588-NPU-Three-Qualities-Local.unitypackage`。
执行 **HumanVision → Enable Local Android NPU Qualities**，在 SDK Settings 中选择
**RK3588 NPU** 和模型等级。通过代码配置时使用
`HumanVisionAccelerationMode.Neural` 与 `ModelInputQuality.Low/Medium/High`。
本地 Unity 包包含已核验的 62 个 Runtime 文件及 RKNN 运行库。

## 验证

中、高档均实际执行 NCNN CPU、ORT 和 RKNN PC 模拟器。七人、单人和空画面均通过人数、
人工框关联、抬臂和关节数值检查。中档和高档的最大关节坐标偏差分别为 0.074 和
0.073 源像素，原始张量最大绝对差分别为 0.08531 和 0.09629。低档回归重放了原
RKNN 模拟器输出与同帧像素参考。

- `cmake --build out/rka --parallel 12 --target humanvision` 构建 Android ARM64。
  `py -3.13 tools/test/verify_android_native.py --library out/rka/bin/Release/libhumanvision.so`
  通过 API 26 ELF 检查，解析 506 个动态强引用。原有 51 个导出保留，新库同时含一个
  私有 Windows YOLO 插件查询导出。
- Windows 原生 `humanvision_native_tests --gtest_filter=YoloTensorPipeline.*:RknnBackend.*:RknnLibrary.*:YoloRgbPreprocess.*:YoloCpuPipeline.*`
  56/56。中、高档比对整张 letterbox 像素，解码实测保存的七人输出，并拒绝旧档输出形状。
- `py -3.13 -m unittest tests.reference.test_rknn_candidate_conversion tests.reference.test_rknn_simulator_gate tests.reference.test_rknn_experimental_stage tests.reference.test_rknn_folded_staging tests.reference.test_rknn_ncnn_recovery`
  22/22。
- Unity 2021.3.45f1 Android 目标 EditMode，`HumanVisionDualAccelerationTests|HumanVisionNeuralContractTests`
  49/49。三档资源、实际尺寸和 APK 配置白名单通过验证。
- Unity 2021.3.45f1 构建 ARM64 IL2CPP Settings APK。使用 `aapt dump xmltree`
  核对三档 NPU 白名单，`apksigner verify --verbose --min-sdk-version 26` 验证签名，
  `verify_apk.py` 核对 APK 内 62 个 Runtime 文件、三档模型及原生库哈希。
  APK SHA-256 为 `06bd8559fb56272f914faec4b8f0b01f8d7b9dd3b0cbd22dfa00a9e2484b7860`。
- `py -3.13 tools/maintenance/check_architecture_boundaries.py` 和
  `py -3.13 tools/maintenance/generate_component_catalog.py --check` 均通过。

模型转换日志、参考张量、回执和测试输出分别位于
`out/rknn-640x384-20261111/` 和 `out/rknn-960x576-20261111/`，交付包位于后者的
`delivery/`。实际 RK3588 驱动兼容性、持续新骨骼 FPS 和热稳定性待真机验收。
