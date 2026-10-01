# Windows PC Unity Demo 安装与运行

这是 `0.4.0-pc.1` Windows x64 评估包，支持 Unity 2021.3 及以上兼容版本。
使用现有 Runtime Host、ONNX Runtime / DirectML、RTMO-t-416 人体图，人数容量可选 1–8。
运行机器需安装 Microsoft Visual C++ 2015–2022 x64 Runtime；DirectML 需要支持 D3D12 的 Windows 环境。
这是身体骨架 Demo，不包含真实 Hand、Handtip、Thumb 推理，也不提供 Android 插件。
近期 Android YOLO 路线使用不同模型图，不能把本 PC Demo 当作其性能或效果验收。

## 干净导入

1. 新建或使用已清理的 Unity 项目。移除旧的 HumanVision UPM 或 `Assets/HumanVision`
   与旧 HumanVision 原生插件；避免两个 SDK 同时定义程序集。保留自己的视频文件。
   若曾运行旧 SDK，请停止 Play 并关闭 Unity，清理后重新打开，让旧原生 DLL 完整卸载。
2. 打开 **Window → Package Manager → + → Add package from git URL**，输入：

   ```text
   https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.pc-demo#codex/unity-pc-demo
   ```

   此 URL 对应专用分支。发布交付时应以最终提供的完整提交 SHA 替换分支名，固定包内容。
   仓库为公开仓库。目录名为 `.pc-demo`，包名称仍为 `com.blazetc.humanvision`，与其他版本互斥。
3. 等待导入、编译及模型安装完成。自动安装器将 `RuntimeData` 中经过索引校验的文件复制到
   `Assets/StreamingAssets/HumanVision/Runtime`。也可运行 **HumanVision → Install Packaged Models**。
   不要手动将 UPM 的 `.meta` 复制到 StreamingAssets；两处必须拥有各自 GUID。
4. 确认 Console 无编译或安装错误。若报模型哈希不匹配，重新安装完整包；不要跳过校验。

## 创建场景并播放

先在 Build Settings 选择 PC, Mac & Linux Standalone → Windows → x86_64，切换平台后再运行。

1. 运行 **HumanVision → Create PC Demo**。菜单生成并保存独立场景，加入 Build Settings，
   并以叠加方式打开。保存自己的场景后，双击新生成的 `Assets/Scenes/HumanVisionPcDemo*.unity`
   单独打开它，避免其他场景相机和 UI 干扰。
2. 选中 `HumanVision PC Demo`，运行 **HumanVision → Choose PC Demo Video File** 选择本地 MP4。
   也可进入 Play 后在 Demo UI 输入绝对路径；本包不捆绑视频。
3. 进入 Play，选择 Video、后端、人数容量（1–8），点击 **Start**。
   若使用项目测试素材 `video1`，设置起始时间 **37 秒**再 Start；不是所有视频都含该片段。
4. 默认 DirectML 为显式请求。初始化失败会显示错误，不自动切换 CPU。
   可 Stop，主动选择 CPU，再 Start。CPU 也是显式配置。
5. 查看视频上的检测框、ID 与身体骨架，同时读取 HUD 的请求后端、实际诊断后端、结果年龄、
   unique complete observation FPS、错误和源帧信息。需要切换输入或后端时先 Stop，选择后再 Start。

摄像头可选择设备后 Start；RTSP 可填自己的地址后 Start。此两项复用现有输入实现，实际设备、
网络连接与重连效果由使用者验证，本包生成过程不代表硬件验收。

## 构建与指标含义

在 Build Settings 切换到 **Windows x86_64**，仅选所需 PC Demo 场景构建。
模型安装器也在构建前执行；原生 DLL 的 Editor Windows x64 与 Standalone Win64 导入已启用。
视频选择路径需在构建机器或运行机器存在。

HUD 的唯一完整观测 FPS 统计新鲜且完整的推理观测，不等于渲染 FPS，也不等于视频源 FPS。
25 FPS 视频的不同源帧上限为 25，不能用它证明每秒 30 个新鲜完整观测帧；每个观测帧必须包括当前全部 Bodies。
容量 8 只是配置上限，不是八人完整观测帧达到 30 FPS 的性能承诺。物理设备效果和最终完整骨架验收仍需用户完成。

## 可核对的包内容

`PROVENANCE.json` 记录原生来源版本、构建开关、各 DLL 字节数 / SHA-256、PE 导入依赖和复制源哈希。
`asset-sha256.json` 覆盖包内资源与元数据；`RuntimeData/index.json` 固定模型与两个 Windows 配置。
`.gitattributes` 保持打包后的字节，文本统一为 LF，安装器保留原有严格索引哈希语义。
`Licenses` 和模型 `SOURCES.md` 保留上游来源及许可文本；不据此声明模型再分发许可已获批准。

维护者可在仓库根目录执行：

```powershell
py -3.13 tools/test/test_pc_demo_package.py
py -3.13 tools/package/package_pc_demo.py --native-inputs out/pc-demo/native-inputs.json
```

可选 `--tgz out/pc-demo/com.blazetc.humanvision-0.4.0-pc.1.tgz` 生成确定性本地包。
打包器不会构建原生代码、运行 Unity、访问网络或发布 Release；Unity 导入、视频和 Player 验证独立记录。
