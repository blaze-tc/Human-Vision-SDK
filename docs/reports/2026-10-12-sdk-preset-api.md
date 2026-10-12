# SDK 配置资产、统一 API 与 Settings 分发整理

日期：2026-10-12。工作分支：`codex/unity-sdk-api-settings`。
SDK 0.4.0-preview.7 / Input 0.1.0-preview.5 为当前开发版本，未发布新的 GitHub Release。

## 行为与兼容性

- 新增 HumanVisionSdkPreset：深复制默认配置，独立于运行中的人数/区域/输入地址。
- 代码通过 InitializeFromPreset(asset, InputKind, address) 指定 Camera、RTSP 或视频；
  CreateOptionsForInput 提供可继续修改的启动副本。配置中含图像请求尺寸、模型等级、
  CPU/GPU/NPU、人数/区域、镜像、结果有效期、置信度、传输方式、超时和资源根目录。
- 新增启动/重启入口、统一借用预览帧、2D/世界平面骨骼数组复制与有效标志。
  实际来源帧/身份随骨骼保存，SupportsMetric3D=false，没有新增真实深度推理。
- 新建 SDK 自动挂配置资产；新建 Settings 场景同时生成配置资产。旧场景保留内嵌设置。
  Settings 首次读取默认值，已保存用户设置优先；三种输入各自拥有配置副本。
- 仅分发 Settings 样例，移除 UnifiedInput/InputPreview 的样例资源与菜单；
  原始公开类型和共享组件保持兼容。测试项目旧 UnifiedDemo 已保留到
  DiagnosticsVerification/ArchivedSdkDemos-20261012，AzureKinectExamples 未修改。
- 包内文档 C# 示例使用 .cs.txt，避免 Unity 编译文档；Settings 内完整示例正常编译。
- 当前已实测 UPM C# 与 canonical Unity 目录逐文件对齐，未从旧 canonical 覆盖现行实现。
- 私有 librknnrt.so 与模型不进入公共 Git 包。已有部署工具/接口保留；普通公共包选择
  缺少资源的 NPU 路径会报错，不能承诺公共包开箱即用 NPU。私有实机 APK 保留既有资源。

## 自动验证

工作目录 `out/sdk-packaging-20261012` 保存完整本地记录，未将原始设备日志上传 Git。

- Preset 测试先 0/6（类型尚未实现）；增加 Settings 参数传递测试后出现预期失败，修复后通过。
- SDK 查询 18/18、生命周期 11/11、Settings 28/28 通过；新增配置/坐标/显式启动测试最终 13/13；四组共 70/70。
- Unity 2021.3.45f1 当前测试项目编译，Android IL2CPP APK 构建成功，0 errors / 8 warnings。
- 独立 2022.3.61t4（本机 Tuanjie 1.6.3 发行版）项目从双 UPM 导入，示例脚本编译，
  生成 Settings scene/prefab/preset 并检查 SDK/Controller/View/font 引用，import-check.txt PASS。
  不将此检查宣称为 Unity 2021.3.18 本机运行验收。
- V1 public surface、架构/文档边界、组件目录和独立 Input 包检查通过。

完整原生构建受迁移后的历史夹具缺失限制：使用新目录 build/windows-sdk-packaging
重新构建后 447 项中 414 通过、2 跳过、31 失败。失败项涉及缺失的 out/c3-local-runtime、
android-yolo 本地模型/输出黄金数据；不降低断言、不创建替代结果。
最初直接运行迁移前的缓存二进制还引用失效的 C:/Users 路径，该轮不作为验证结果。
此前记录的 447/447 仍是历史证据，本轮不能声称完整原生回归通过或发布 Release 达标。

## OnePlus 运行回归

测试设备 LE2120；测试 HumanVisionSettingsDemo 与项目自带真实七人视频。
SDK 0.4.0-preview.7 已正常播放视频并绘制多人骨骼，保存了截图和原始日志。
首轮视频 Running 采样 152 行均值：输入发布 24.92 FPS、新结果 24.21 FPS、
含人体新结果 21.70 FPS、Unity 58.43 FPS、结果年龄 94.04 ms。
这些是采样窗口均值，不是每人完整骨骼 FPS，也不是新的 RK3588 20–25 FPS 验收。
RTSP 已连接并出图；空场景椅子仍有既有模型误检，不能将它算作真实人体检出。
热机中档 RTSP 截图约 15 FPS 输入，thermal status=3；没有宣称解决模型误检或热降频。

## 最终构建与实机复核

- 最终 Android 构建于本地 19:39 完成：Succeeded，0 errors / 8 warnings，920309416 bytes。
- APK：HumanVisionSettingsDemo-SDK-Preset-20261012.apk；SHA256
  `be4cdc9f3223e90382b688a0c3a9fe838fdb8243b385b04bc68f38162e41479b`。
- 同一字节安装到 OnePlus，最终日志 session-20261012-114310-803-f0f867ed，
  正确记录 SDK 0.4.0-preview.7 / Input 0.1.0-preview.5。
- 最终视频、停止、再次应用已检查：视频和四个人体骨骼可见；停止后人数和 FPS 清零；
  再次启动后新来源帧和骨骼继续更新。截图 device-final-video/stopped/restarted.png。
- 拉取时 Video Running 共 190 行采样：输入 24.582、新结果 23.642、含人体新结果
  19.665 FPS，Unity 58.556 FPS，结果年龄 90.815 ms；五个错误字段均为空，Unity 日志无 Error/Exception。
  该数值包含热机和重启的本轮样本，不能等同于稳定逐人完整骨骼帧率。
- 独立 2022.3 项目最终重跑生成/引用检查 PASS，batch exit 0；最终 C# canonical/UPM 对齐。
- 包快照 Python 测试 15/15；使用原项目 .venv-reference 的 ONNX 图改写测试 3/3。
  系统 Python 3.13 缺 onnx 的首次启动不计入通过结果。
- 架构、V1 public surface、组件目录、Input 隔离、两包文件哈希检查通过。

复现入口：Unity Test Runner 运行 HumanVisionPresetTests / HumanVisionSdkQueryTests /
HumanVisionSdkLifecycleTests / HumanVisionSettingsDemoTests；
`py -3 tools/maintenance/check_architecture_boundaries.py`；
`py -3 tools/maintenance/generate_component_catalog.py --check`；
`py -3 tools/package/check_input_package.py --root upm/com.blazetc.humanvision.input`；
`py -3 tools/package/test_package_release_snapshot.py`。

最终 RTSP 复核也能连接、发布输入和无人结果；截图 device-final-rtsp.png 偏暗且有噪点。
电脑 ffmpeg 独立从同一 RTSP 采样 rtsp-source-final.png 同样为暗噪点画面，
因此该轮只确认连接/出图，不用于人体识别或图像质量验收。视频多人人体仍为骨骼回归来源。
结束时点击停止，完整日志另存 oneplus-final-complete；手机草稿未写入用户配置。

完整日志共 1208 行。除上述首段视频采样外，后续热机视频在 elapsed=641.393s 出现一次
GPU copy reservation -1，下一采样错误清除、结果继续更新；没有足够证据认定具体根因，
保留为原有 GPU 路径的待调查瞬时异常，不宣称长时间零错误。
另一个非空错误字段是切换 RTSP 时 Opening 的等待首帧状态。Unity Error/Exception 为 0，
最终采样为 Stopped。
