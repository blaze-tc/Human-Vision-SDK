# preview.7 本地 Android 资源补充包

适用 SDK 0.4.0-preview.7 + Input 0.1.0-preview.5。这个文件说明如何迁移用户已有测试资源；
不授予新的模型或第三方运行库许可，也不是公开分发授权。不要把“仅本地保留”理解成厂家禁止分发。

## 在另一个项目使用

1. 先安装 Input，再安装 SDK；UPM 和 unitypackage 选择一种，不能重复安装。
2. 导入本地提供的 `HumanVision-LocalAndroidResources-preview7.unitypackage`。
   它写入 `Assets/StreamingAssets/HumanVision/Runtime` 完整 47 项资源及索引、
   `Assets/Plugins/Android/arm64-v8a/librknnrt.so` 和一个 Editor 检查工具。
   会替换同路径模型；若目标项目自定义过模型，先备份该目录。
   不要在已经包含同一个 librknnrt.so 的项目重复安装。
3. 执行菜单 **HumanVision > Enable Local Android Resources**。
   检查文件哈希、三个 ARM64 原生库及平台导入设置，然后选择 `android-dual-vulkan-npu` 构建模式。
   缺文件、版本不匹配或重复插件会报具体错误；不会静默换成 CPU。
4. Android 构建保持 ARM64、IL2CPP、最低 API 26，关闭 Auto Graphics API 并把 Vulkan 排第一。
   使用 HumanVisionSettingsDemo；OnePlus 可选择 CPU/Vulkan，RK3588 可选择 NPU（低档 512×288）。

补充包从现有项目的哈希索引导出，包含 CPU/Vulkan 共用资源及 NPU 模型/优化候选，
没有用户视频、日志、APK、场景或输入地址。原有 `staged-runtime.json` 确保模型安装器不覆盖它。
这是资源部署验证，不表示 RK3588 驱动兼容、20–25 FPS、商业授权已经验收。

## 重新导出

在 SDK 仓库运行（输出目录必须不存在）：

```powershell
py -3 tools/package/package_local_android_addon.py --project "E:/UnityProject/Human-Vision-SDK-Test" --output "E:/UnityProject/Human-Vision-SDK-Test/Builds/LocalAndroidResources-preview7"
```

导出前逐项核对已有模型、索引、固定版本的 librknnrt.so；不会从用户视频目录收集文件。
本地补充包不自动上传 GitHub。公开仓库仅提供导出工具与部署说明。

## 来源和公开发布边界（2026-10-13 核对）

- RKNN Runtime 来自瑞芯微官方仓库固定提交 `42aa1d426c0a9e0869b6374edba009f7208a1926`，
  Android ARM64 `librknnrt.so` SHA256 为 `07a8398be5caed21a1998ffa313e0425bb9e76b154a8dbbc667d71722c009351`。
  [官方文件](https://github.com/airockchip/rknn-toolkit2/blob/42aa1d426c0a9e0869b6374edba009f7208a1926/rknpu2/runtime/Android/librknn_api/arm64-v8a/librknnrt.so)
  与[根目录版权声明](https://github.com/airockchip/rknn-toolkit2/blob/42aa1d426c0a9e0869b6374edba009f7208a1926/LICENSE)。
  查到的声明以免责声明为主，不能仅凭“GitHub 能下载”认定二进制再分发条件已经满足；
  也不能把头文件或旧版本的 BSD 许可直接套到这个版本的运行库。
- 现行模型源自 Ultralytics YOLOv8，由已固定的 NCNN 权重恢复 ONNX，再用 RKNN Toolkit2 2.3.2 转换。
  转换格式不消除上游模型许可要求。Ultralytics 官方提供 AGPL-3.0 和 Enterprise 两条授权路线，
  [官方说明](https://www.ultralytics.com/license)。具体项目的许可选择需结合完整使用方式确认。
- 用户已明确最终用于闭源商业项目、没有 Enterprise 授权，优先评估替换模型。
  不能把“仅内部测试”标记视为许可豁免。当前 Vulkan/NPU 使用同源 YOLOv8 权重，
  Android CPU 配置则按人数使用 OpenMMLab TopDown/RTMO，须按具体模型分别核实。
  如计划闭源商用，应核实已有授权/取得适用许可，或另立模型替换与精度性能验证任务。
  此处不是法律意见，也没有把现有完整测试资源重新定性为“禁止使用”。
