# preview.7 NPU 部署缺口与本地补充包

用户指出公开包缺少 NPU 运行库/模型，并明确最终用于闭源商业项目。本轮分开处理部署与许可，
不把测试资源存在、资源导入成功或“local evaluation”标记当成商业许可。

## 已完成

- 核对固定提交 Rockchip Toolkit2 的官方 Android ARM64 库来源及 SHA256，与本地已测库一致。
  根版权声明不是明确的二进制再分发条款；未把旧版 BSD 或头文件许可套用到库。
- 识别当前 RKNN 模型由 Ultralytics 来源的 YOLOv8 权重转换；格式转换不改变授权要求。
  官方 AGPL/Enterprise 路线与出处见 [部署说明](../user-guide/LOCAL_ANDROID_RESOURCES.md)。
- 新增导出工具，仅选取当前项目索引内 47 项 runtime、索引/保留回执、固定库和显式 Editor 菜单。
  不复制 APK、视频、日志、场景、输入地址。公共 Git 不新增第三方模型和库。
- 本地生成 `E:/UnityProject/Human-Vision-SDK-Test/Builds/LocalAndroidResources-preview7/`，
  包含 unitypackage、README、资源 SHA 清单和 SHA256SUMS。
  实际包 SHA256：`87cdc1c24ce7216fea5106f7079412af27e5fe9037a01928980bdf5b03193b54`。
  包内说明是导出时快照；最新商用边界以仓库部署文档为准。
- Editor 菜单校验 47 个 runtime 文件、SDK/Input/RKNN 三个 ARM64 库的版本哈希与导入平台，
  防止重复插件；确认模型安装器保留显式资源，然后启用 CPU/Vulkan/NPU 构建模式。
  不修改已有算法、模型、推理调度或接口。

## 新鲜验证

1. `py -3 tools/package/test_local_android_addon.py`：首次因模块未实现失败；实现后 **5/5 通过**。
   覆盖哈希损坏、索引越界、重复条目、回执失配及忽略索引外用户视频。
2. 真实项目导出、unitypackage asset/meta 字节闭包回读验证通过。
3. Unity **2021.3.45f1** 离线 Input/SDK 隔离项目：实际 ImportPackage、脚本编译、执行
   `HumanVisionLocalAndroidSetup.VerifyAndEnable` 通过，47 文件及三库核验通过。
4. **2022.3.61t4（Tuanjie）** Git UPM 隔离项目：同样的导入和验证通过。
   两项目 `ProjectSettings/HumanVisionAndroidRuntimeSettings.asset` 均记录 `android-dual-vulkan-npu`。
5. `py -3 tools/maintenance/check_architecture_boundaries.py`：V1/架构边界通过。

日志：`out/local-android-addon-{import,verify,upm-import,upm-verify}.log`。
未连接 RK3588，本轮不宣称 NPU 运行/性能/商业授权验收。已有实际测试项目未导入补充包，避免重复库。

## 仍需决定

公开完整 NPU 二进制/模型前，分别确认 Rockchip 库分发条件及模型许可路线。用户已确认没有
Enterprise 授权并选择优先评估替换模型；[首轮评估](2026-10-13-commercial-model-replacement.md)
已完成结构与来源检查，后续仍需权重许可、转换、精度与真机性能验证。
不自动购买授权、不改变整个项目许可证，也不把“单独下载资源”当成规避上游许可的办法。
