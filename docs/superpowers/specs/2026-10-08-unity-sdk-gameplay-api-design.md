# Unity SDK 总控组件与常用骨骼 API 设计

日期：2026-10-08。状态：待用户评审的设计草案；本文中的新增 API 尚未实现。

## 1. 用户需求与验收结果

为 Unity 应用提供一份可直接挂载的总控脚本，也提供在当前场景创建 SDK 的菜单。
使用者配置输入与人数后，就能初始化、查询区域是否有人、按 index 读取骨骼和关节、
获取屏幕/世界坐标，并安全停止和释放资源。公共函数、参数、坐标约定、错误条件和
生命周期均有中文说明及可复制的示例。参考 KinectManager 的常用能力，保持
HumanVision 自身的语义 API 和现有 V1 API 兼容。

2026-10-08 用户追加：增加独立 UGUI 设置场景 Demo，参考
`E:/UnityProject/YS-Sensory game project/Sensory-Game-2021.3.45/Assets/Scenes/Setting.unity`，
可以沿用界面布局，通过本设计的新 API 实现设置能力。

这次验收针对 API、组件配置、生命周期和使用方法。实际识别准确率、双手端点质量、
Android 性能及 1–8 人的 30 FPS 仍按原有独立设备验收记录判断。

## 2. 已核对的工程事实

- `http://localhost:8092` 实际连接 Human-Vision-SDK-Test，Unity 2021.3.45f1、
  Built-in；当前场景为 HumanVisionRtspDemo，检查时未运行、未处于编译状态。
- 根目录 checkout 为 `codex/android-ncnn-vulkan-production`，HEAD `d9f2f93`。
  Unity manifest 实际引用 `.worktrees/android-ncnn-vulkan/out/input/production-correction/
  quality-q4-package-20261005-v2/` 下的 Input/SDK 两个本地包。
- Android 实现工作树 HEAD 为 `b523f13`，另有大量未提交的输入、GPU、测试及文档修改。
  实现前必须固定相关源码/包的基线，保留这些现有工作，不能以根目录旧源码直接覆盖。
- 权威 Runtime 源码是 `unity/HumanVisionDemo/Assets/HumanVision/Runtime/`；UPM 为生成物。
  HumanVisionManager 已提供初始化、Shutdown、MaxBodies、区域提交、原始与采样结果。
- HumanVisionCameraManager 位于 Demo 程序集，包含输入设置、旧 23 点枚举、区域槽位查询。
  其初始化会加载持久化设置；现有 GetJointPosition 返回中心原点的单位图像平面。
- HumanVisionCanonicalJointId 已有 32 个语义关节。新接口复用这个枚举。
  旧 HumanVisionJoint.Count=17、旧枚举和现有函数签名保持兼容。
- 原生区域按人体包围盒中心归属；一个区域最多选取一名参与者。原始区域坐标为
  图像左上角原点的归一化 Rect。公共 API 不改变这个判定规则。
- 输入桥有 DetachUnifiedSource 和 UnifiedRetirementPending；关闭流程必须等待
  未完成的读取/复制退役，不能只调用 Close 就认为全部释放。
- 最近的 code-first 指南位于 sensory-sdk-docs 工作树；保留初始化、输入、读取、关闭
  的教学顺序，将新总控方式作为更简单的入口。
- 参考 Setting 是 `.unity` 场景，使用 UGUI、1600×900 CanvasScaler；左侧约 72%
  是预览及工具栏，右侧约 28% 是设置滚动栏，控件高度约 58 像素。
  场景序列化保存了 VisionSettingsView 的预览、骨骼、控件与区域编辑引用。
  相关源文件为 VisionSettingsView.cs、VisionSettingsLayout.cs、VisionRegionHandle.cs、
  VisionRegionBorder.cs、VisionSettingsStore.cs 和 SettingManager.cs。
  参考项目依赖 SensoryGame 游戏运行服务与返回游戏逻辑；SDK Demo 使用独立控制器。

## 3. 方案比较与选择

| 方案 | 收益 | 代价 |
|---|---|---|
| 新增 HumanVisionSdk 总控，复用现有 Runtime 与 Input | 挂载一个脚本；接口职责清晰；旧 Demo 可继续使用 | 需要一个轻量输入/生命周期组合层 |
| 将全部能力继续增加到 HumanVisionCameraManager | 改动入口较少 | 使用者依赖 Demo；设置加载与启动行为继续耦合 |
| 实现 KinectManager 兼容适配器 | 既有 Kinect 应用迁移较方便 | 包含设备特有能力；超出独立 SDK 封装范围 |

推荐第一种。项目类型是长期维护的 SDK。职责分为 Runtime 结果查询与几何计算、
Input/SDK 组合与生命周期、Editor 创建与配置、文档/示例四部分；使用现有
程序集与输入抽象，避免引入新的第三方依赖。

## 4. 组件与数据职责

- `HumanVisionSdk`：用户挂载的唯一入口，负责运行资源准备、配置、输入绑定、
  状态与事件，以及安全启动/停止。内部明确持有 HumanVisionManager 和现有输入桥。
- Runtime 查询辅助类：纯 C# 查询、关节校验、槽位与 ID 映射、坐标和几何计算；
  只依赖语义结果及 Unity 数学类型，方便 EditMode 测试。
- 可序列化配置：启动方式、人数、区域、输入类型/地址、有效性阈值、结果最大年龄、
  屏幕目标矩形与世界映射平面。默认使用 Inspector；持久化配置只在显式启用时加载。
- Editor 创建入口和 Inspector：组合必要组件，展示配置与当前状态，支持 Undo；
  创建仅添加当前 SDK 对象，识别所需 UI 不作为必需项。
- 保留现有 Demo/CameraManager、原有 GUID 和公开接口；新组件共享输入基础设施，
  同一输入与 Runtime 只能有一个启动控制者。

总控放在能够引用现有输入桥的组合程序集，避免 Runtime 反向依赖 Demo。
具体目录/程序集名称在实施计划中按现有边界确定，不为命名而迁移整个旧 Demo。

## 5. 公共 API 草案

以下名称是设计目标，实施前写入新增 API 合同；允许通过评审调整名称。
默认查询原始识别结果；采样结果通过名称明确的独立接口获得。

| 类别 | 计划入口 | 语义 |
|---|---|---|
| 初始化 | `Initialize()`、`Initialize(configuration)` | 返回可等待的 IEnumerator；异步准备资源与输入 |
| 状态 | `State`、`IsInitialized`、`IsRunning`、`LastError`、`HasFreshResult` | 分清 Runtime 就绪、输入运行与有效识别结果 |
| 停止/销毁 | `StopSdk()`、`Shutdown()` | 可等待的停止/释放流程；重复调用安全 |
| 人数设置 | `TrySetMaxBodies(count)`、`GetMaxBodies()` | 配置上限，目前支持 1–8；零值不代表无限制 |
| 实际人数 | `GetUsersCount()`、`IsUserDetected(index)` | 当前有效参与者数；index 槽位是否占用 |
| ID | `GetUserIdByIndex(index)`、`GetUserIndexById(id)`、`IsUserTracked(id)` | 新接口使用 StableTrackId；无效 ID 为 0、无效 index 为 -1 |
| 区域 | `TrySetRegions(regions)`、`TryGetRegion(index, out region)`、`CopyRegions(buffer)`、`GetRegionCount()` | 读取/提交归一化 Rect；不返回可修改的内部配置数组 |
| 区域开关 | `TrySetRegionsEnabled(enabled)`、`RegionsEnabled` | 未启用区域时进行全画面识别 |
| 区域占用 | `IsRegionOccupied(index)`、`TryGetRegionOccupancy(index, out occupied)` | 未收到有效当前结果时 Try 返回 false；不把未知当成已观测的无人 |
| 整体骨骼 | `TryGetBodyByIndex(index, out body)`、`TryGetBodyById(id, out body)`、`CopySkeletonByIndex(index, joints, out metadata)` | 明确借用结果与可保存的复制结果 |
| 关节数据 | `TryGetJointByIndex(index, jointId, out joint)`、`TryGetJointById(id, jointId, out joint)` | 32 个语义关节；有效性、置信度、派生标志、时间戳 |
| 图像坐标 | `TryGetJointImagePosition(...)`、`TryGetJointNormalizedPosition(...)` | 源图像像素、归一化图像坐标 |
| 屏幕坐标 | `TryGetJointScreenPosition(...)` | Unity 屏幕像素；遵循目标显示矩形与输入方向 |
| 世界坐标 | `TryGetJointWorldPosition(...)` | 映射至配置的 Unity 世界平面 |
| 常用几何 | `TryGetDirectionBetweenJoints(...)`、`TryGetAngleAtJoint(...)`、`TryGetUserWorldPosition(...)` | 平面中的方向、三点夹角、骨盆位置；任一所需点无效则失败 |
| 图像与来源 | `GetColorImageTex()`、图像宽高、SourceFrameId、SourceTimestampUs、ResultSequence、ResultAgeMilliseconds | 便于显示与判断时效 |
| 设置 Demo 支持 | `Stats`、输入状态、可用输入质量档位与当前选择、输入配置读写 | 沿用实际支持的能力，区分采集尺寸和识别输入质量；通过总控配置应用 |
| 事件 | `Initialized`、`ResultUpdated`、`UserEntered`、`UserExited`、`RegionOccupancyChanged`、`Stopped`、`ErrorOccurred` | Unity 主线程通知；结果事件仅在新原始结果到达时触发 |

首次范围不加入没有来源的深度、关节 3D 姿态、抓握状态、分割或动作分类。
关节间方向和夹角均明确为现有平面姿态数据的几何计算。

## 6. index、人数和区域合同

- `index` 为 SDK 参与者槽位，范围 `[0, GetMaxBodies())`；可能有空位。
  使用区域时，与 regionIndex 一致；未使用区域时保持原生稳定跟踪槽位语义。
- `GetUsersCount()` 是有效占用槽位数，不是有效 index 的连续上限。提供示例说明：
  遍历全部槽位并调用 TryGet；身份关联使用 StableTrackId。
- 区域启用时采用目前一人一区域的配置规则，区域数组长度等于人数上限；
  Rect 必须有限、位于图像范围、尺寸有效、互不重叠。
- 变更人数与区域的组合提供一次验证/应用入口 `TryApplyConfiguration`。
  单独变更人数造成区域长度不匹配时返回明确错误，不静默覆盖用户区域。
- 验证失败不能修改生效设置。原生应用失败时保持失败状态和明确错误，禁止把
  未完全应用的草稿设置当成生效设置返回；不声称底层多调用具有天然原子性。
- 区域版本号由 SDK 管理；成功更新后清理旧槽位，等待匹配新版本的结果。
  停止、切换输入、重启、超时后不能继续返回旧区域占用或旧骨骼。
- 无结果、旧结果、无效区域与新结果中的无人有不同状态。提供 HasFreshResult
  及 TryGetRegionOccupancy，简单 IsRegionOccupied 仅表示当前可确认的占用。

## 7. 坐标与结果有效性合同

- 原始 normalized/image position：左上角为原点，X 向右、Y 向下；直接复用 SDK
  的语义坐标，不暴露模型索引。
- screen position：左下角为原点，单位是 Unity 屏幕像素；目标矩形默认全屏，
  可指定实际视频矩形/RectTransform。视频留黑、缩放、旋转、镜像均沿用输入显示
  变换，不能在两个层重复镜像或把纹理像素误标为屏幕像素。
- world position：配置一个 Transform 表示平面中心与朝向，并配置平面宽高；
  将显示坐标映射到该平面，再 TransformPoint 得到 Unity 世界坐标。
  默认使用 SDK 根对象的 XY 平面，宽高为 1 Unity 单位，可配置。
  这是图像驱动的虚拟世界平面，不能据此获取人体距离或真实深度。
- Try 查询同时检查槽位、关节枚举、Position.Valid、有限数值、配置置信度与结果
  时效。手关节使用自身时间戳；不得因为身体结果新就把过期手点作为当前有效点。
- 原始与采样结果明确区分。预测元数据保留；渲染采样和重复查询不递增原始结果序号。
- 现有 body/关节数组会复用，借用查询只适合当帧主线程消费；CopySkeleton 写入
  用户提供的数组并附带 ID、来源帧、时间戳，支持历史保存而不在每次查询分配数组。

## 8. 生命周期与使用体验

初始化顺序：验证配置 → Prepare → 初始化 Runtime → 应用人数/区域 → 订阅新结果
→ 打开输入 → 绑定输入。输入错误、资源缺失、原生初始化失败都有可读错误。
自动启动使用同一个初始化流程；初始化中停止要取消后续步骤，重复启动不重复订阅。

停止顺序：拒绝新输入/查询 → 取消初始化及事件订阅 → Detach 输入 → 等待退役
→ 关闭输入 → Shutdown Runtime → 清空当前状态。禁用/销毁和场景卸载的处理必须
利用可继续运行的退役机制，不能只在被销毁的 MonoBehaviour 上等待协程。
组件 OnDestroy 作为最后兜底；示例必须给出场景切换前等待关闭的调用方式。

挂载方式：空 GameObject 添加 HumanVisionSdk；自动补齐内部必需组件；Inspector
可选择摄像头、视频、RTSP，配置人数、区域、自动启动、屏幕/世界映射。
默认不加载旧 Demo 的持久化设置覆盖 Inspector。

菜单方式：`GameObject > Human Vision > Create SDK`，在当前场景创建并选中 SDK
对象；使用当前选中父对象作为可选父节点；支持 Undo；SDK 启动配置明确可见。
可另提供可选预览/骨骼显示，但读 API 不要求 Canvas、RawImage 或 Renderer。

## 9. UGUI 设置场景 Demo

交付 `HumanVisionSettingsDemo.unity`、可重复使用的设置面板 Prefab、独立场景控制器
和创建 Demo 的 Editor 菜单。界面保存为普通 UGUI 对象和显式序列化引用，导入后
可在 Unity 内直接修改布局、文字和样式，不要求修改生成代码。生成入口创建独立
场景/资源，源 Setting 场景作为参考。

### 布局与交互

沿用参考场景的大预览、深色控件、中文标签、顶部输入模式栏、底部操作栏和右侧
滚动设置栏。保留 FitInParent 视频比例与全屏预览切换；区域编辑限定在真实图像
矩形内，支持拖动、右下角缩放、选中区域与数值编辑；骨骼与区域线框独立绘制。
镜像/旋转下，显示区域与 SDK 原始区域必须通过同一变换转换。
常用输入选项仅显示当前模式的控件；高级设置默认折叠。

| 参考控件/区域 | SDK Demo 行为与 API |
|---|---|
| Camera / Video / RTSP | 修改输入配置并提交异步应用；分别保留三个模式的输入草稿 |
| 摄像头下拉与刷新 | 查询 Input 设备列表；通过配置选择设备 |
| 视频下拉/自定义路径 | 使用已有视频清单与输入支持；设备上的可用位置按 Input 实际能力处理 |
| RTSP 地址/IP/快捷填入 | 修改 RTSP 输入配置；保留明确可用的地址预设；连接由 SDK 管理 |
| 人数下拉 | 1–8 人；与区域一起通过 TryApplyConfiguration 提交 |
| 输入质量档位 | 从实际可用档位填充；通过总控配置切换；显示支持的尺寸信息 |
| 镜像/采集预设/宽高/FPS | 修改对应输入配置；实际来源状态与请求配置分开显示 |
| 区域开关/拉框/均分/数值 | 操作区域草稿；应用时提交 TrySetRegions/组合配置；读取生效区域用于核对 |
| 线宽/关节点大小 | 修改 Demo Renderer 设置；不改变推理配置或识别能力 |
| 应用/重连 | 校验草稿、等待旧输入退役、应用、等待真实输入状态；失败时显示原因 |
| 应用并保存 | 应用确认成功后保存完整设置；应用失败不替换已保存配置 |
| 仅保存/重读 | 保存经过校验的草稿、读取已保存配置；不隐式宣称配置已在 Runtime 生效 |
| 停止/启动 | 等待总控 StopSdk/Initialize；忙碌时防止重复操作 |
| 返回 | 可配置返回 Demo 场景或回调；等待必要应用/退出流程后返回 |
| 运行状态/高级信息 | SDK 状态、输入状态、实际人数、槽位 ID、区域占用、关节有效性、来源帧和结果年龄 |
| 日志开关/间隔 | Demo 诊断日志配置；只记录能够通过公共 API 获得的 SDK 数据 |

设置控制器只通过公共总控 API、Input 设备查询和 Renderer 显示配置工作，不访问
私有字段、原生 Bindings 或 SensoryGame 业务服务。参考场景中的游戏角色、跑步/
跳跃状态在本 Demo 中改为区域/槽位识别状态；返回目标由调用者配置。

### 设置状态与保存

草稿、已保存配置、实际运行配置分开持有。首次启动读 Demo 自己的设置文件，
无保存配置时使用场景 Inspector 默认值；不自动迁移或覆盖参考游戏的配置文件。
人数修改明确生成相应区域草稿，只有用户点击应用才改变 Runtime；均分按钮会
明确更新草稿区域。失败验证保持当前运行设置可查询，错误提示显示在操作栏。

配置保存包含版本号、三个输入模式、共享人数/区域和 Demo 显示选项。沿用参考
Store 的验证、临时文件、备份恢复行为，使用独立的 Demo 配置目录。
加载失败保留错误提示和可恢复备份，不用默认值静默覆盖损坏原件。

### Demo 验收

新增布局、控件绑定及行为测试：三模式可见性、人数/区域联动、非法区域不应用、
忙碌状态按钮、保存/重读语义、拖动缩放边界、全屏与留黑、镜像区域转换、空/过期
占用状态、CanvasRenderer 和 EventSystem、进入退出重复事件订阅。
在测试工程实际运行场景，观察真实视频预览与骨骼，手动操作人数、区域、应用、
保存、重读及停止，检查控制台和保存后重入行为。界面渲染证据与实际识别数据
分别记录；静态测试通过不能代替实际界面与输入验证。

## 10. 注释与文档要求

- 新 public 类型/成员使用中文 XML 文档，说明参数、返回值、index、单位、有效性、
  主线程要求、数据寿命及错误条件。Inspector 配置使用中文 Tooltip 和分组。
- 复杂逻辑说明为何这样处理：区域版本隔离、输入退役、手点年龄、坐标变换。
- 更新 first-use 指南，提供自动挂载、一键创建和代码控制三种入口。
  保留从初始化到结果读取再到关闭的最短流程。
- 增加完整 API 表、坐标图/约定、区域占用与稳定 ID 示例、关节世界/屏幕位置示例、
  场景切换关闭示例，以及 KinectManager 常用函数对应表。
- 增加设置 Demo 操作指南，说明如何创建/打开场景、每个设置对应的 API、何时生效、
  保存位置及如何替换返回目标；设置 UI、绑定脚本和持久化逻辑均提供中文注释。
- 更新 Runtime/组合组件 README、CHANGE_MAP、UNITY_STABLE_API 和当前里程碑状态。

## 11. 计划验收与实施边界

先写新增合同/行为测试并观察对应 RED，再最小实现，取得新 GREEN。
测试包括空槽位、边界 index、稳定 ID、32 点读取、无效/低置信度/过期手点、
没有结果与明确无人、区域验证/版本变更、人数组合变更、停止后清理、重复启动、
初始化时停止、等待退役、镜像/旋转/留黑屏幕映射、世界平面 Transform、几何退化、
借用与复制数据、一次事件订阅、菜单在当前场景创建及 Undo。

合成数据只用于这些确定性 API 测试，并明确属于测试 fixture；实际初始化/输入/
骨骼读取集成使用真实运行资源与已有视频，不在生产路径填充固定关节或强制成功。

构建所有受影响的 Runtime、组合层、Demo、Editor、测试程序集及 Android 条件分支。
运行相关 Unity EditMode/PlayMode 集成、公共表面兼容检查和架构检查。
生成 SDK 包后验证用户测试工程的实际导入、场景创建、初始化、配置、读取与关闭。
UnitySkills 刷新后等待编译完成并读取错误；异步测试必须轮询至实际结束。

实现采用独立工作树/分支，基于明确固定的现有输入/SDK 版本；保留已有 Android 工作。
仅在记录准确命令/结果并更新 DEVELOPMENT_STATUS 后提交经过验证的文件。
新发布、合并与设备性能验收不隐含在本次 API 完善的完成结论中。

下一步：用户评审设计后，编写细化实施计划；选定当前唯一里程碑及执行方法后实施。
