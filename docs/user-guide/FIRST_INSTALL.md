# 第一次安装使用引导

适用 SDK `0.4.0-preview.4`、Input `0.1.0-preview.2`；正式项目实查 Unity `2021.3.45f1`。本页按“安装 → 看到输入 → 看到骨骼 → 接入游戏 → 构建”的顺序操作。[文档首页](README.md)

## 1. 先分清两种使用情形

**已有正式项目**：打开 `Sensory-Game-2021.3.45`，先完成第 2、5 节的版本检查。它已经装了双包，也已经有 `Init`、`Setting` 和跨栏游戏接入，通常从第 7 节开始使用。不要再次导入离线包，也不要为了第一次使用重建已调整好的设置 UI。

**新 Unity 项目**：按第 2～6 节安装并跑官方 Demo。第 7～11 节解释正式项目如何使用，里面的 `Sensory Game` 菜单属于正式项目；新项目只有 SDK 时不会出现这些菜单。需要自行建立游戏适配层，见第 12 节。

## 2. 准备环境和备份

1. 关闭 Play Mode，保存场景。
2. 使用版本控制或复制项目，备份 `Assets`、`Packages`、`ProjectSettings`；保存已有配置与自定义素材。配置文件位置见第 11 节。
3. 用 Unity Hub 安装或选择 Unity 2021.3 或更高版本。正式项目优先保持 `2021.3.45f1`，避免同时升级引擎和 SDK。
4. 准备 Windows x64 电脑。Git UPM 安装还需要 Git；在 PowerShell 运行 `git --version`，应显示版本号。没有 Git 时使用离线安装。
5. 要构建 Android，在 Unity Hub 的该编辑器 **Add modules** 中安装 **Android Build Support、Android SDK & NDK Tools、OpenJDK**。
6. 允许 Windows 的相机访问，并确认 USB/内置相机能被系统使用。Android 第一次打开 Camera 时要允许相机权限。

**检查结果**：Unity 打开项目后 Console 没有脚本编译错误；Android 目标用户已安装上述模块。

## 3. 选择一种安装方式

| 方式 | 适用情况 | 安装内容 |
| --- | --- | --- |
| Git UPM | 能访问 GitHub；希望依赖固定版本 | 两个 Git URL |
| 本地 UPM `.tgz` | 离线安装，但希望包留在 Package Manager 管理 | Input tgz、SDK tgz |
| `.unitypackage` | 通过 Assets 导入离线资源 | Input unitypackage、SDK unitypackage |

一个项目选一种方式。已有 Assets 方式安装时，先备份并确认哪些目录属于旧 SDK，再迁移；不要把项目业务脚本当成 SDK 删除。UPM 与 Assets 同时存在容易产生重复类型、GUID 和原生库。无需常规清除 `Library` 或修改 `Library/PackageCache`。

### 3.1 Git UPM：推荐的逐步操作

1. 打开 Unity，选择 **Window → Package Manager**。
2. 点击左上角 **+ → Add package from git URL...**。
3. 粘贴 Input 的完整 URL，点击 **Add**：

   ```text
   https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.4
   ```

4. 等待包解析与编译完成。不要在仍编译时安装第二个包。
5. 再选择 **+ → Add package from git URL...**，粘贴 SDK URL，点击 **Add**：

   ```text
   https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.4
   ```

6. 等待编译和运行数据安装。需要 GitHub 仓库访问权限时，先在本机 Git 凭据管理器完成登录；不要把访问令牌写进 URL 或项目配置。
7. 在 Package Manager 的 **In Project** 列表确认两个包都存在。Input 显示 `0.1.0-preview.2`，SDK 显示 `0.4.0-preview.4`；Input 的包版本与仓库标签不同是正常的。

如果习惯编辑 manifest，也可以在原有 `dependencies` 对象中加下面两行，保留其他依赖并注意逗号：

```json
"com.blazetc.humanvision.input": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.input#v0.4.0-preview.4",
"com.blazetc.humanvision": "https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#v0.4.0-preview.4"
```

必须显式安装双包：SDK 的数字版本依赖不能让 Unity 自动找到另一个 Git 子目录。

### 3.2 本地 UPM tgz

1. 打开 [preview.4 Release](https://github.com/blaze-tc/Human-Vision-SDK/releases/tag/v0.4.0-preview.4)，展开 **Assets**。
2. 下载 `com.blazetc.humanvision.input-0.1.0-preview.2.tgz` 和 `com.blazetc.humanvision-0.4.0-preview.4.tgz`。
3. 将文件存放在稳定的本地目录；不要导入后立即移动它们。
4. Unity **Window → Package Manager → + → Add package from tarball...**，先选择 Input tgz。
5. 等待 Input 编译结束，再用同一菜单选择 SDK tgz。
6. 检查两个包都在 **In Project** 且 Console 无编译错误。

### 3.3 离线 unitypackage

1. 从同一 Release 下载 `HumanVisionInput-0.1.0-preview.2.unitypackage`、`HumanVisionSDK-0.4.0-preview.4.unitypackage`。
2. Unity **Assets → Import Package → Custom Package...**，先选 Input 文件。
3. 在导入窗口保留该包完整文件集，点击 **Import**，等待编译结束。
4. 再导入 SDK 文件，等待编译和数据安装完成。
5. 检查 Assets 中存在 Input 与 SDK，且不存在第二套 UPM 安装。

不需要同时下载 `.tgz` 和 `.unitypackage`。GitHub 的 **Source code** 自动压缩包不等于上述 Unity 安装包。

### 3.4 可选：检查离线下载完整性

下载同一 Release 的 `SHA256SUMS.txt`，在 PowerShell 对下载文件执行：

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath 'D:\SDK\HumanVisionInput-0.1.0-preview.2.unitypackage'
Get-FileHash -Algorithm SHA256 -LiteralPath 'D:\SDK\HumanVisionSDK-0.4.0-preview.4.unitypackage'
```

将 `D:\SDK` 替换为自己的下载目录。输出哈希应与 `SHA256SUMS.txt` 中对应文件一致，不区分大小写；不一致先重新下载，勿修改模型文件绕过校验。

## 4. 安装运行数据

1. 等待编辑器安装器自动处理包内运行数据。
2. 在 Project 窗口查找 `Assets/StreamingAssets/HumanVision/Runtime/index.json`。
3. 展开同目录，确认有 `profiles` 和 `modelpacks`。运行索引声明需要的文件，不要求手动选择单个模型。
4. 若索引缺失，在 Unity 顶部执行 **HumanVision → Install Packaged Models**，等待完成，再看 Console 的具体错误。

首次运行 `HumanVisionRuntimeData.Prepare` 会把索引内文件按 SHA-256 提取到 `Application.persistentDataPath/HumanVisionRuntime`，以后复用匹配的文件。Android APK 中的 StreamingAssets 不能当普通目录直接读取，因此要用这个准备接口。

**检查结果**：没有 `Runtime data index missing`、`hash mismatch` 或模型文件缺失错误。安装器成功不代表已经识别到人，还要继续输入和骨骼检查。

## 5. 确认正式项目安装版本

1. 用文本编辑器打开项目的 `Packages/manifest.json`，查找 `com.blazetc.humanvision` 和 `com.blazetc.humanvision.input`，两个 URL 都应固定在 `#v0.4.0-preview.4`。
2. 打开 `Packages/packages-lock.json`，查看两个包的 `source` 都是 `git`，`hash` 都是：

   ```text
   a201e0f44aa68a3f831f248b67400bd5fd7358c9
   ```

3. 只读检查 lock；不要用手工篡改 lock 假装安装了某个版本。需要换版本时修改 manifest 或用 Package Manager，让 Unity 重解析。
4. `Library/PackageCache` 中的目录后缀通常是截短提交号；这是 Unity 缓存，不是维护源码的位置。

**检查结果**：正式项目当前核查版本就是上述两包/提交。后续升级时同时记录新的 tag、lock hash 和测试结果。

## 6. 新项目先运行官方 Demo

1. Git UPM 用户在 Package Manager 选中 Input 包，展开 **Samples**，导入 **InputPreview** 示例。
2. 保存当前场景。
3. 执行 **HumanVision → Create unified demos in dedicated folder**。
4. 在 `Assets/HumanVisionUnifiedDemo` 打开 `HumanVisionCameraDemo.unity`，点击 Play。
5. 选择可用相机并启动，允许权限，先确认画面不断更新。
6. 让一个人全身站入画面，检查骨骼与 `BodyCount`。只看到预览时仍不能算识别成功。
7. 分别打开同目录的 `HumanVisionVideoDemo`、`HumanVisionRtspDemo`，用自己的视频/地址测试。

“dedicated folder”菜单会把示例放入独立目录并保留其他目录的构建场景。另一个 **Create unified Camera, Video and RTSP demos** 菜单写入 `Assets/Scenes`；已有正式项目优先用独立目录，避免覆盖同名场景及改变正式入口。

只测试输入、不需要识别时可使用 **HumanVision → Input → Create standalone preview**。输入包单独运行不加载骨骼模型。

## 7. 在正式项目打开设置

1. 在 Project 窗口双击 `Assets/Scenes/Init.unity`，点击 Play。`GameLoading` 创建唯一的 `HumanVisionGameRuntime`，读取配置并按 `AutoStart` 启动。
2. Init 正常流程随后进入 `HurdleKing`。当前代码直接加载跨栏场景；它不保证先展示游戏选择页。
3. 在跨栏场景按 **Shift+C** 打开 `Setting`。也可以停止 Play 后直接打开 `Assets/Scenes/Setting.unity` 再 Play，用于独立调设置。
4. 左侧是当前输入预览和骨骼；右侧是人数、模型等级、源和区域设置；底部有“应用 / 重连”“应用并保存”“停止”“返回游戏”。
5. 已有场景 UI 正常时不用生成菜单。缺失 UI 才在退出 Play 后执行 **Sensory Game → Human Vision → Build Setting UI**。
6. 需要完全重新生成时用 **Rebuild Setting UI**。它先备份场景到 `Temp/HumanVisionGameBackups`，再重建生成器的 UI，手工布局会被重建；先保存自己的布局版本。

**检查结果**：能看到当前状态和源选项。状态 `Running` 表示源已启动；仍需检查人数和实际骨骼。

## 8. 按输入类型启动

### 8.1 Camera 相机

1. 点击顶部 **Camera 相机**。注意：正式项目点击模式按钮会提交该模式的应用请求。
2. 点击 **刷新摄像头**，在下拉框选正确设备。
3. 先选 `1280×720 / 30 FPS` 采集预设，必要时改为 `640×480`。这是采集请求，设备实际尺寸与速率可能不同。
4. 按需要切换 **镜像**，调整骨骼线宽、关节点大小。
5. 点击 **应用 / 重连**，等待源变成 `Streaming`，并显示“识别运行中”。权限等待、打开失败或约 15 秒超时会给出错误。
6. 让参与者全身入镜，观察身体数、骨骼和动作状态。

没有设备时先检查系统权限、设备连接与被其他程序占用情况；不能通过修改检测阈值解决相机打不开。

### 8.2 Video 视频

1. 准备一段包含清晰全身人物的视频；本正式项目当前没有内置测试视频。
2. 推荐先使用 H.264 编码的 MP4；把自己的文件放到 `Assets/StreamingAssets/Videos/demo.mp4` 等目录。
3. 退出 Play，执行 **Sensory Game → Human Vision → Refresh StreamingAssets Video Catalog**。构建前也会自动生成 `HumanVisionGame/video-catalog.json`。
4. 进入 Setting，点击 **Video 视频 → 刷新视频列表**，从下拉框选择视频。
5. Windows 也可填自定义绝对文件路径。Android 不能使用电脑上的 `E:\...` 路径，应用内视频应从已打包目录清单选择，或填写设备实际可访问路径。
6. 点击 **应用 / 重连**，检查播放、`BodyCount` 与骨骼。

目录清单收集多种扩展名；扩展名出现不代表每个平台支持该编码。打不开时先用设备播放器/Unity VideoPlayer 验证媒体解码，再排查 SDK。

### 8.3 RTSP 推流

1. 在推流电脑准备 RTSP 服务，并启动相机或视频发布。预设按钮只填 URL，**不会启动服务**。
2. 查明推流电脑的局域网 IPv4；手机和电脑接入可互通的网络。
3. 先用 VLC 等客户端验证真实 URL 能播放，确认流含 H.264 视频且服务支持 TCP。
4. 在 Setting 点击 **RTSP 推流**，填写 **推流电脑 IP**。
5. 服务路径符合预设时点击 **电脑摄像头**（`/videodevice`）或 **电脑视频**（`/video-1.mp4`）；默认端口 `554`。例如 `rtsp://192.168.1.100:554/videodevice`，请替换示例 IP。
6. 若服务端端口、路径或认证不同，直接填写完整 **RTSP 地址**，不要强行使用预设。
7. 点击 **应用 / 重连**，检查 `Opening → Streaming`；中断时可能进入 `Reconnecting`。
8. Android 的地址应指向推流电脑；`127.0.0.1` 通常指手机自身，不能当作电脑地址。自动填入的是构建/清单时记录的电脑 IP，网络变化后要重填。

若 VLC 也不能播放，先处理服务、地址和网络。若 VLC 正常而 SDK 失败，收集源错误、编码和日志；当前版本不是所有 RTSP 编码与摄像头的通用兼容保证。

## 9. 设置人数、模型等级和区域

1. **人数**选实际需要的容量 `1～8`。这个数是最大容量，实际身体数读 `BodyCount`；游戏角色数量还要与场景 `players` 配置一致。
2. **模型等级**先选“中”。Android NCNN Vulkan：低 `512×288`、中 `640×384`、高 `960×576`。选择高等级不保证更高 FPS。
3. Windows 当前 Profile 实际输入为 `416×416`；低/中/高选择保存给 Android，不改变 Windows 当前模型尺寸。以界面“实际模型合同”和 Profile 为准。
4. 开启 **按区域绑定角色**。区域 `0 → players[0]`，区域 `1 → players[1]`，依次类推；区域不是 TrackId。
5. 点击 **均分区域** 得到初始分区，或点 **拉框 / 编辑**：拖框内移动，拖右下角色块缩放；可先“放大画面 / 拉框”。
6. 数值编辑的 `X/Y/W/H` 为归一化坐标：左上 `(0,0)`、右下 `(1,1)`，Y 向下；区域必须在图像内、宽高为正且不能重叠。
7. 例如 2 人左右分区为 `(0,0,0.5,1)` 和 `(0.5,0,0.5,1)`。
8. 拖动和 **更新草稿区域** 只改草稿；点击 **应用 / 重连** 才交给 SDK。当前区域用于识别结果的分配，不是保证裁掉区域外输入像素的推理加速开关。

**检查结果**：区域框与人数一致，每个参与者在自己的区域中，动作控制目标角色正确。区域框出现只能证明 UI 绘制成功。

## 10. 应用、保存、停止和返回

| 按钮 | 实际行为 | 何时使用 |
| --- | --- | --- |
| 应用 / 重连 | 校验草稿，准备合同，退休旧输入，重新初始化并打开源；成功后替换运行配置 | 临时测试参数 |
| 应用并保存 | 同上；只有源实际启动成功才保存配置 | 确认有效后用于下次启动 |
| 高级设置：仅保存草稿 | 校验并写文件，当前运行会话保持原参数 | 预设下一次启动参数 |
| 重读保存配置 | 把文件读回 UI 草稿；点击应用才影响当前运行 | 放弃未保存编辑 |
| 停止 | 等待源拷贝退休后关闭输入；应用期间可能要求等待 | 释放输入 |
| 返回游戏 | 返回上一个游戏，缺少记录时返回 HurdleKing；常驻服务仍在 | 继续游戏 |

不要把“应用请求已提交”当成“应用成功”。检查最终 `State=Running`、`Source.State=Streaming` 和错误文字。启动失败时项目会尝试恢复旧配置；读最终状态。

## 11. 动作、配置和日志

1. 首次动作测试让玩家全身、髋、膝、肩、腕都入镜，先站立校准，再摆臂并抬腿。
2. 高级设置中查看 **动作状态**。跑步计步需要新抬腿事件和有效摆臂证据；只摆臂或只抬腿不应增加速度。
3. 起跳通过髋中心上移和向上速度同时判断，回落后解除锁定。先保持默认阈值，确认新骨骼持续到达后再调整。
4. 实际动作验收把 `HurdleKingManager` 的 `inputMode` 设成 **Skeleton**。默认 **KeyboardAndSkeleton** 会保留键盘控制，不能用键盘移动证明骨骼有效。
5. 参数单位与默认值见 [API 文档的动作参数](API_REFERENCE.md#10-动作输入与动作参数)。跟踪丢失、换人、源重开会清理动作历史。
6. 项目配置保存到 `Application.persistentDataPath/HumanVisionGame/config.json`，备份后缀为 `.bak`。Windows 的实际路径由 Company/Product 名称决定；通过 UI 或 `Store.ConfigPath` 获取，勿固定另一台机器用户名。
7. 展开高级设置，点击 **打开日志目录 / Android 导出**。Windows 打开目录；Android 调用系统选择器导出 ZIP。
8. **导出 ZIP**包含当前日志会话；**复制日志路径**用于定位。手机仅复制路径不等于文件已导出。
9. 日志包括 `events.jsonl`、`statistics.csv`、`skeleton.jsonl`，字段解释和统计限制见 [平台测试文档](PLATFORM_TEST_RESULTS.md)。详细骨骼日志主要在 Setting 开启，离开设置后不作为全速逐帧采样。
10. Editor 的 Play Mode 执行 **Sensory Game → Human Vision → Capture Runtime Evidence**，在 `Temp/HumanVisionGameEvidence` 取得运行 JSON 与 Game View 截图。

**检查结果**：保存后重启能读到配置；运行结果序号增长；游戏失去有效跟踪时停止外部跑步输入。详细日志默认节流，不能用日志行数证明 30 FPS。

## 12. 将 SDK 接到新游戏

官方包提供输入、识别、显示 API；正式项目另有自己的动作层。最小流程是：

1. 用协程 `HumanVisionRuntimeData.Prepare` 准备运行数据。
2. 创建 `HumanVisionManager`，用 `RuntimeRoot/Profile/MaxBodies` 初始化。
3. 创建一个 `IHumanVisionFrameSource` 实现，调用 `Open`。
4. 用 `VideoPlayerFrameSource.Configure` 关联 Manager，再 `BindUnifiedSource` 提交帧。
5. 用 `ResultUpdated` 读取新观察，按语义枚举读取关节；需要保留历史则复制数值。
6. 按区域映射到游戏角色；不要把 `Bodies[i]` 当永久角色身份。
7. 切源时先解绑，等 `UnifiedRetirementPending=false` 后关闭旧源；退出时取消事件并释放。

[API 文档](API_REFERENCE.md#2-最小可用示例windows-camera)提供完整的 Windows Camera 示例。正式项目使用 `IRunJumpInput` 隔离游戏和识别，便于以后换算法或输入设备。

## 13. 构建 Windows 正式项目

1. 退出 Play，执行 **Sensory Game → Human Vision → Configure Game Build Scenes**。
2. 打开 **File → Build Settings**，确认 `Init` 在第 0 个且启用，Setting、HurdleKing 等实际游戏场景在列表中。
3. 选择 **PC, Mac & Linux Standalone → Target Platform Windows → Architecture x86_64**，必要时 **Switch Platform**。
4. 确认运行数据与自备视频已放入 StreamingAssets，Console 无错误。
5. 点击 **Build** 或 **Build And Run**，输出到独立构建目录。
6. 保留生成的 EXE、`_Data` 和其他相邻运行文件，不能只复制 EXE。
7. 启动后打开 Setting，重选目标机器相机/RTSP 地址，检查画面、身体数、骨骼、动作、配置保存与日志。

## 14. 构建 Android 正式项目

1. **File → Build Settings → Android → Switch Platform**。
2. **Edit → Project Settings → Player → Android → Other Settings**：
   - `Scripting Backend = IL2CPP`。
   - `Target Architectures` 只选 `ARM64`。
   - `Minimum API Level` 设为 Android 8.0 / API 26 或更高。
   - 关闭 `Auto Graphics API`，`Graphics APIs` 把 `Vulkan` 放第一；目标包可只保留 Vulkan。
3. **Edit → Project Settings → Human Vision → Android Runtime**，选择 **NCNN Vulkan**。ORT CPU/XNNPACK 属于不同构建模式；不要把它们当本版已合格的自动兜底。
4. 确认所选 Profile、质量清单、模型、ARM64 库与 GPU 桥审计资料已安装。构建校验失败时按错误补齐，不要删除校验器。
5. 检查 `Init` 为构建入口，刷新视频目录；RTSP IP 应是手机能访问的推流电脑地址。
6. 用 USB 连接启用开发者选项的 Android 设备，选择 **Build And Run**，或生成 APK 后自行安装。
7. 第一次启动允许相机权限。等待运行数据准备完成，再进入 Setting 检查实际 Profile、输入尺寸和身体数。
8. 真机 Vulkan/GPU 能力不满足时会明确失败，本路线不自动降级成 CPU。记录错误与设备型号。
9. 切换低/中/高、镜像、相机/视频/RTSP，逐项应用；再测试前后台、断流、重连、设置保存和日志导出。
10. 按 [平台测试流程](PLATFORM_TEST_RESULTS.md#5-正式项目应怎样补测)收集持续性能与动作证据。打包成功和画面流畅不能代替验收。

## 15. 常见问题逐项检查

| 现象 | 先检查 | 下一步 |
| --- | --- | --- |
| 找不到菜单/类 | 双包是否装完、Console 是否有编译错误 | Input 先装，再 SDK；项目菜单还需项目脚本 |
| Git 安装失败 | 本机 Git、仓库权限、完整 URL/tag | 处理访问权限或改用离线双包 |
| Duplicate type/GUID/原生插件 | Assets 与 UPM 是否装了两套 | 备份后移除旧 SDK 安装副本，保留游戏适配代码 |
| Runtime index missing/hash mismatch | StreamingAssets 索引、包版本、安装器错误 | 重新执行 Install Packaged Models；不要篡改 hash |
| 有画面、无骨骼，BodyCount=0 | 人是否全身入镜、区域、模型/Profile、置信度 | 捕获人所在帧的运行记录，保留零身体结果 |
| BodyCount>0、看不见骨骼 | CanPresentResult、overlay 开启、CanvasRenderer、布局裁剪 | 对照运行 JSON 和截图，排查显示层 |
| 模型等级变了、PC 尺寸未变 | Windows 的实际 Profile | 本版 PC 固定416；等级保存供 Android |
| RTSP 一直 Opening/Reconnecting | 真实发布服务、H.264/TCP、电脑 IP、端口/路径 | 先在外部客户端播放同一 URL |
| Android 黑屏/初始化失败 | 权限、API26/ARM64/IL2CPP/Vulkan、GPU 能力和构建 metadata | 导出具体错误与设备日志 |
| 骨骼显示但角色不动 | 输入模式、区域下标、有效髋/膝/肩/腕、新结果序号 | 看动作状态，按摆臂+抬腿规则测试 |
| 跳跃重复/换人后触发旧动作 | Generation/TrackId/JumpSequence 消费逻辑 | 按动作 API 示例清理身份和序号 |
| 界面60FPS但识别慢 | 新 ResultSequence 到达率、结果年龄、阶段耗时 | 独立测识别吞吐，不能重复计显示骨骼 |

首次接入完成的标准：双包版本正确、运行数据可准备、输入持续更新、人物帧出现有效骨骼、角色绑定正确、应用保存和重启有效、目标平台错误和日志可定位。多人帧率、真实手点、长期动作准确性还要分别测量。
