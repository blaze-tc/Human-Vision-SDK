# API 调用与说明文档

基线：SDK `0.4.0-preview.4` / Input `0.1.0-preview.2`，提交 `a201e0f44aa68a3f831f248b67400bd5fd7358c9`；包最低 Unity `2021.3`。[文档首页](README.md)

本页逐项覆盖应用接入用的 Manager、输入源、帧数据、桥、关节、区域、质量/配置、显示门面；末尾列出基础C ABI调用与高级GPU/插件扩展入口。`internal`会话/Interop不是给游戏直接调用的公共API；自定义算法插件和底层Vulkan ABI以对应头文件为完整合同。

快速查阅：[初始化/配置](#3-运行数据与初始化配置) · [Manager](#4-humanvisionmanager逐项说明) · [身体/关节/统计](#5-身体关节点与统计数据) · [统一输入](#6-统一输入包api) · [显示桥](#7-提交与显示桥-videoplayerframesource) · [设置与质量](#8-sdk设置质量和实际合同api) · [C ABI](#10-c-abi调用与结果码)。

## 1. 调用前的六条规则

1. 在Unity主线程调用这些Unity接口。Manager自动Update轮询，应用不用另启线程读Bodies。
2. `SubmitFrame=true`代表提交被接受，**不代表推理完成**；等新的`ResultUpdated`。
3. `Bodies`和其中关节数组反复复用，只读`[0,BodyCount)`；历史记录复制数值。
4. 坐标是图像平面，归一化左上(0,0)、Y向下；不是深度传感器米制3D。
5. 读取点前检查`Valid`、`Confidence`和独立观察时间。语义手槽位存在不代表实际识别手点；当前随包Profile关闭真实手部推理。
6. Create、Shutdown、改变容量/重新初始化可能加载模型或等任务；不是无耗时的逐帧操作。显示采样/保持不能算新观察。

## 2. Start初始化与骨骼读取示例

先看[代码入门引导](FIRST_INSTALL.md)：在一个空物体上挂[SdkBasicUsage.cs](examples/SdkBasicUsage.cs)，无需配置Canvas/预览/Overlay。它使用当前包API，自动关联所需组件。

| 代码所在位置 | 调用与含义 | 引导片段 |
| --- | --- | --- |
| `void Start()` | GetComponent、Configure(manager,null,null)、StartCoroutine(Initialize) | [Start](FIRST_INSTALL.md#3-void-start中写什么) |
| `IEnumerator Initialize()` | Prepare成功取得root，TryInitialize配置会话，订阅ResultUpdated | [初始化](FIRST_INSTALL.md#4-怎样初始化sdk) |
| 初始化成功后 | source.Open相机，BindUnifiedSource自动提交图像 | [输入图像](FIRST_INSTALL.md#5-怎样让sdk得到摄像头图像) |
| `OnResult(long sequence)` | BodyCount遍历、StableTrackId、WristLeft、Valid/Confidence/坐标 | [骨骼读取](FIRST_INSTALL.md#6-怎样读取人数人物id和骨骼关节) |
| `StopSdk()` | 取消订阅、Detach并等退休、Close、Shutdown | [关闭](FIRST_INSTALL.md#8-不再使用时怎样关闭) |

可选[Hierarchy创建菜单](FIRST_INSTALL.md#9-可选hierarchy右键创建启动物体)由示例编辑器文件提供，用户需复制到Assets/Editor；不是当前发布包已经提供的菜单。显示组件示例见[可选显示教程](DISPLAY_AND_BUILD.md)。

## 3. 运行数据与初始化配置

来源：[HumanVisionRuntimeData](../../upm/com.blazetc.humanvision/Runtime/HumanVisionRuntimeData.cs)、[HumanVisionConfig](../../upm/com.blazetc.humanvision/Runtime/HumanVisionConfig.cs)。

### 3.1 `HumanVisionRuntimeData.Prepare(Action<string> ready, Action<string> failed)`

返回`IEnumerator`，用`yield return`或`StartCoroutine`执行。它按StreamingAssets运行索引提取/校验文件到persistentDataPath，所有文件匹配后发布缓存索引。

- `ready`：成功回调，参数是准备好的RuntimeRoot路径。初始化必须使用这个路径。
- `failed`：失败回调，参数是可定位的错误，如缺索引、下载/提取失败、hash不符。回调失败后不要继续初始化。
- 网络式读取也用于APK资源，不代表必须从互联网下载模型。

### 3.2 `HumanVisionConfig`全部配置字段

| 字段 | 类型/默认 | 含义与使用范围 |
| --- | --- | --- |
| `RuntimeRoot` | string/空 | 准备好的运行数据目录；非空进入Runtime Profile模式，空进入V1兼容模式 |
| `Profile` | string/`auto` | Runtime组合ID；Android解析还受APK构建metadata/allowlist约束 |
| `MaxBodies` | int/4 | Runtime允许1～8；最大身体容量，不是当前人数 |
| `DetectionThreshold` | float/0.35 | V1检测置信阈值0～1；Profile模式不从该字段覆盖模型合同 |
| `PoseThreshold` | float/0.30 | V1姿态阈值0～1；Profile模式由运行配置控制 |
| `DetectionInterval` | int/1 | V1检测间隔，至少1；不是相机FPS或Profile质量等级 |
| `EnableTracking` | bool/true | V1跟踪开关；Runtime公共服务由Profile/Host控制 |
| `UseHardwareAcceleration` | bool/true | V1兼容后端选择；Runtime使用Profile后端，不能凭true断言GPU已工作 |
| `DetectorModelPath` | string/空 | 旧V1检测模型路径；新接入用RuntimeRoot/Profile |
| `PoseModelPath` | string/空 | 旧V1姿态模型路径；同上 |

`Validate()`：无返回值，非法配置抛`ArgumentException`；Runtime模式检查人数/Profile，V1额外检查阈值、间隔与路径。`Clone()`：返回配置字段的浅复制；初始化时Manager自身也复制请求，后续改调用方配置不会自动更新会话。

## 4. `HumanVisionManager`逐项说明

来源：[HumanVisionManager.cs](../../upm/com.blazetc.humanvision/Runtime/HumanVisionManager.cs)。新接入由自己的启动脚本持有Manager并管理生命周期，示例见第2节。

### 4.1 初始化、提交、容量与关闭

| API | 参数 | 返回/错误 | 调用含义 |
| --- | --- | --- | --- |
| `TryInitialize(HumanVisionConfig requestedConfig)` | 非null配置 | bool；失败看`LastError` | 先Shutdown旧会话，再按RuntimeRoot创建新会话；失败时旧会话不会自动恢复，调用方应处理失败状态 |
| `TrySetMaxBodies(int maxBodies)` | Runtime范围1～8 | bool；失败看LastError | 变更最大容量；Runtime可能重建本机会话并清空结果，源/GPU租约应协调；重建前应停止提交并等待资源退休 |
| `Shutdown()` | 无 | void | Dispose会话；IsInitialized=false；不要继续提交 |
| `SubmitFrame(...)` | 见下表 | bool；失败看LastError | CPU像素异步提交；有界队列最新帧优先，接受不等于完成；NCNN GPU路线不要走CPU帧接口 |

完整CPU签名：

```csharp
bool SubmitFrame(IntPtr data, int width, int height, int strideBytes,
    HumanVisionPixelFormat pixelFormat, long frameId,
    long timestampUs, int dataBytes);
```

| 参数 | 内容/单位 |
| --- | --- |
| `data` | 可读CPU像素内存指针，至少含声明的字节；调用期间保持有效，不能传Texture对象地址 |
| `width`,`height` | 正像素尺寸，描述实际提交图像 |
| `strideBytes` | 每行跨度（字节），至少宽×每像素字节；不等于宽度 |
| `pixelFormat` | `Rgba32=1`、`Bgra32=2`（4字节/像素），`Rgb24=3`、`Bgr24=4`（3字节/像素） |
| `frameId` | 源帧身份；应用应递增并与输入世代关联，不能重复编号冒充新帧 |
| `timestampUs` | 源观察时间，微秒；必须用与该路径采样/年龄计算匹配的单调时钟，不能用DateTime UTC或媒体PTS代替 |
| `dataBytes` | 指针缓冲有效字节数，需满足跨度与高度；格式/尺寸非法会被拒绝 |

### 4.2 区域API

`TrySetRegions(Rect[] regions, long revision)`：归一化左上坐标区域数组；空数组表示关闭区域。revision标识配置版本，修改时递增。返回true表示设置成功；null、未初始化或原生拒绝返回false。共享示例设置要求每人一框、框不重叠且宽高至少0.01。

`TryCopyRegionAssignments(int[] indices, out long revision)`：把当前身体结果对应的区域下标写入调用方数组；容量至少BodyCount。返回false表示无会话、容量/快照不匹配或错误。只读写入的前BodyCount项，并检查返回revision等于已应用的区域版本；未匹配区域可能是-1。第i项对应该次Bodies[i]，不是固定角色i。

### 4.3 结果、统计与状态属性

| API | 类型 | 含义/未初始化行为 |
| --- | --- | --- |
| `IsInitialized` | bool | 会话存在；不保证源已Streaming、也不保证识别人 |
| `Bodies` | HumanVisionBody[] | 最新原始结果复用数组；未初始化null；只用BodyCount范围 |
| `BodyCount` | int | 当前原始快照身体数；未初始化0，可能小于MaxBodies |
| `ResultSequence` | long | 原始身体结果序号；用于判新，不是视频帧序号；未初始化0 |
| `SourceFrameId` | long | 这次身体结果来自哪一源帧；未初始化-1 |
| `SourceTimestampUs` | long | 结果所对应源时间，微秒；未初始化0 |
| `SampledBodies` | HumanVisionBody[] | 当前显示采样数组；Runtime有平滑/保持，V1退回Bodies |
| `SampledBodyCount` | int | 显示采样有效数量；不是原始完成数 |
| `MaxBodies` | int | 会话容量；没有会话时为配置值 |
| `Stats` | HumanVisionStats | 最新周期统计；未初始化default；可能滞后于最新事件 |
| `UsesRuntimeProfile` | bool | 是否为语义Runtime会话 |
| `UsesAndroidGpuFrames` | bool | 当前Runtime是否使用Android GPU输入路线 |
| `ActiveRuntimeProfile` | string | 实际Profile ID；非Runtime为空，不要只显示用户请求值 |
| `HandInferenceFps` | float | Runtime手部任务统计，非Runtime为0；不能证明每只手有效完整速率 |
| `RuntimeDiagnostics` | string | 实际流水线/后端等诊断描述；不要解析成稳定业务协议 |
| `LastError` | string | 最近失败原因；不是持续源状态，输入源错误还要读Source.LastError |
| `ResultUpdated` | `event Action<long>` | Update轮询发现新观察时主线程触发，参数是ResultSequence；不承诺每个原生中间结果都被消费 |

事件使用：初始化后订阅；销毁/切服务时取消。只做有限工作；不要把显示刷新当成ResultUpdated，也不要在回调里阻塞等待下一结果。

Runtime轮询也会检测独立手序号变化，因此启用真实手任务的其他Profile可能在身体ResultSequence相同的情况下再次发事件。严格身体FPS按新的身体序号去重；手部更新按自己的关节观察时间判断。当前随包Profile关闭真实手任务；第2节Reader演示身体序号去重。

### 4.4 高级Android GPU生命周期API

通常由统一输入桥调用；游戏只需管理源和等待退休。以下也属于Manager公开方法：

| API | 参数与返回 | 含义/限制 |
| --- | --- | --- |
| `BeginAndroidGpuSourceLease(RenderTexture texture)` | 活跃GPU源纹理；void | 为Runtime GPU源开始资源租约；错误记录LastError并抛异常；不能用临时销毁纹理 |
| `EndAndroidGpuSourceLease()` | void | 结束租约请求，不代表所有GPU拷贝已结束 |
| `TryRetireAndroidGpuSourceCopies(out HumanVisionAndroidSourceRetirement retirement)` | bool及退休对象 | false表示尚不能取得退休token，应以后重试；true可返回退休对象，仍须主线程轮询`retirement.IsComplete`才算完成。无Runtime/无活跃租约时true且对象可null；错误记录并重新抛异常 |
| `SubmitAndroidGpuFrame(RenderTexture texture, int rotationDegrees, bool mirrored, long frameId, long timestampUs)` | bool | GPU帧提交是否接受；metadata描述输入方向；错误可能抛异常；不等于新骨骼完成 |

这些接口要求正确路线/纹理/时钟。统一源已应用旋转/镜像的帧不能再变换一遍。更底层内容见[Android Runtime代码](../../upm/com.blazetc.humanvision/Runtime/Android)。

`HumanVisionAndroidSourceRetirement.IsComplete`：主线程轮询该旧源GPU拷贝是否完成；未完成false，完成true并释放托管纹理强引用；轮询失败抛HumanVisionException，不能据此销毁资源。`HumanVisionException`是SDK原生错误异常类型；Manager多数Try方法转成false/LastError，但上述GPU方法可能重新抛出，调用者需处理。

异常的`Operation`为失败操作名，`ResultCode`为原生返回码，`Message`含操作、返回码及原生错误细节。

## 5. 身体、关节点与统计数据

来源：[HumanVisionBody](../../upm/com.blazetc.humanvision/Runtime/HumanVisionBody.cs)、[CanonicalJoint](../../upm/com.blazetc.humanvision/Runtime/HumanVisionCanonicalJoint.cs)、[Stats](../../upm/com.blazetc.humanvision/Runtime/HumanVisionStats.cs)。这些是只读结果，不能由游戏写入伪造观察。

### 5.1 `HumanVisionBody`每项字段

| 字段 | 含义 |
| --- | --- |
| `StableTrackId` | long语义身份；新代码优先用它关联观察历史 |
| `TrackId` | int旧兼容身份；不是区域下标，旧门面GetUserIdByIndex使用该值 |
| `RegionIndex` | 所属区域/角色槽，未分配可能-1 |
| `BoundingBoxPixels` | 实际结果图像中的像素包围框，Rect x/y/width/height |
| `DetectionConfidence` | 身体检测/观察置信度，不是每个关节点置信度 |
| `ObservationTimestampUs` | 身体观察时间，微秒 |
| `Joints` | 固定17点旧身体数组，保留COCO17顺序；新业务使用Canonical枚举 |
| `HandJoints` | 6个旧手点槽：左Hand/Handtip/Thumb，然后右；无真实手推理可全无效 |
| `CanonicalJoints` | 32语义槽，按HumanVisionCanonicalJointId索引；派生/缺失点需检查标志 |

### 5.2 `HumanVisionJoint`与`HumanVisionCanonicalJoint`

| 属性 | 含义 |
| --- | --- |
| `HumanVisionJoint.Count` | 旧身体数组固定17，不是总语义点数/人数 |
| `Pixel` | Vector2像素坐标，左上原点、Y向下 |
| `Normalized` | Vector2相对图像尺寸坐标，通常0～1；先检查有效性/有限数，不盲目当UV |
| `Confidence` | float关节点置信度；应用阈值应按动作场景设置 |
| `Valid` | 点是否被当前数据标记有效；无效时坐标不能当观测 |
| `IsDerived` | 是否由其他有效观测派生，非独立模型直接点；派生点不能据此推断真实手端点 |
| `CanonicalJoint.Position` | 上述HumanVisionJoint内容 |
| `CanonicalJoint.ObservationTimestampUs` | 此关节独立观察时间，手/身体可以不同 |
| `CanonicalJoint.PredictionMilliseconds` | 显示预测跨度（毫秒）；不增加原始观测数量，源时间保留 |

### 5.3 32语义关节点逐项

左右是人体自身的左右；镜像显示不要用屏幕左右重新定义人体侧别。

| 枚举（值） | 中文含义 | 枚举（值） | 中文含义 |
| --- | --- | --- | --- |
| Pelvis (0) | 骨盆中心 | SpineNavel (1) | 腹部脊柱 |
| SpineChest (2) | 胸部脊柱 | Neck (3) | 颈部 |
| ClavicleLeft (4) | 左锁骨 | ShoulderLeft (5) | 左肩 |
| ElbowLeft (6) | 左肘 | WristLeft (7) | 左腕 |
| HandLeft (8) | 左手掌 | HandtipLeft (9) | 左食指指尖 |
| ThumbLeft (10) | 左拇指尖 | ClavicleRight (11) | 右锁骨 |
| ShoulderRight (12) | 右肩 | ElbowRight (13) | 右肘 |
| WristRight (14) | 右腕 | HandRight (15) | 右手掌 |
| HandtipRight (16) | 右食指指尖 | ThumbRight (17) | 右拇指尖 |
| HipLeft (18) | 左髋 | KneeLeft (19) | 左膝 |
| AnkleLeft (20) | 左踝 | FootLeft (21) | 左足 |
| HipRight (22) | 右髋 | KneeRight (23) | 右膝 |
| AnkleRight (24) | 右踝 | FootRight (25) | 右足 |
| Head (26) | 头部 | Nose (27) | 鼻 |
| EyeLeft (28) | 左眼 | EarLeft (29) | 左耳 |
| EyeRight (30) | 右眼 | EarRight (31) | 右耳 |

32槽不是每帧32有效点的保证。当前身体模型部分点由规则派生，真实手点关闭；不要用腕部固定偏移造出有效手指。

### 5.4 `HumanVisionStats`全部属性

| 属性 | 单位/意义 | Runtime模式注意 |
| --- | --- | --- |
| `InputFps` | 帧/秒输入统计 | 当前Runtime映射可能为0，不能直接作实际源FPS |
| `InferenceFps` | 帧/秒推理统计 | 映射BodyFps，历史GPU运行可能0；以独立新结果计数交叉核对 |
| `DetectionMs` | 毫秒 | Runtime映射PreprocessMs；名字来自V1 |
| `PoseMs` | 毫秒 | Runtime映射InferenceMs |
| `TrackingMs` | 毫秒 | Runtime映射PostprocessMs |
| `TotalMs` | 毫秒 | Runtime为以上三阶段之和；不等于拍摄到显示延迟 |
| `SubmittedFrames` | 累计提交计数 | 托管会话提交统计，注意具体CPU/GPU路径 |
| `ProcessedFrames` | 累计处理计数 | Runtime映射BodySequence，不能累计Bodies数量当帧数 |
| `DroppedFrames` | 累计输入丢帧 | 最新帧策略下的队列丢弃；另查Bridge.ReadbackDrops与源发布丢弃 |

## 6. 统一输入包API

来源：[IHumanVisionFrameSource](../../upm/com.blazetc.humanvision.input/Runtime/IHumanVisionFrameSource.cs)、[SourceSettings](../../upm/com.blazetc.humanvision.input/Runtime/HumanVisionSourceSettings.cs)、[TextureFrame](../../upm/com.blazetc.humanvision.input/Runtime/HumanVisionTextureFrame.cs)。

### 6.1 输入实现和状态

| 类型/API | 意义 |
| --- | --- |
| `WebCameraFrameSource` | 相机输入；按DeviceName/采集请求打开；真实输出由设备决定 |
| `WebCameraFrameSource.Pause()/Resume()` | 暂停/恢复相机；暂停不能把保留的画面计为新源帧 |
| `VideoFrameSource` | Unity视频输入；从Location打开，媒体与平台编码能力有关 |
| `RtspFrameSource` | RTSP输入；Windows/Android路径由包实现，默认TCP |
| `VideoFrameSource.Pause()` | 暂停视频；源仍由组件管理，旧帧不能当新发布 |
| `VideoFrameSource.Resume()` | 恢复视频播放 |
| `UnityTextureFrameSource.PendingRetirementCount` | 旧纹理资源等待退休数量，用于源生命周期诊断 |
| `InputKind` | 枚举：Video=0、WebCamera=1、Rtsp=2；不要与其他相机枚举数值混用 |
| `InputSourceState` | Stopped、Opening、Streaming、Reconnecting、Error、Closing；打开返回后要持续检查Streaming/Error |

### 6.2 `IHumanVisionFrameSource`全部成员

| 成员 | 参数/返回 | 行为 |
| --- | --- | --- |
| `Open(HumanVisionSourceSettings settings)` | void | 请求打开，可能异步等权限/媒体/网络；读State/LastError确认 |
| `Close()` | void | 请求关源，旧资源按退休机制释放，不等于所有GPU拷贝立即结束 |
| `State` | InputSourceState | 当前源状态 |
| `LastError` | string | 采集/解码/网络错误，区别于Manager.LastError |
| `CurrentTexture` | Texture或null | 当前借用的预览纹理；不归游戏销毁，跨世代保留必须持有合法租约 |
| `TryGetLatestFrame(long afterFrameId, out HumanVisionTextureFrame frame)` | bool | 只返回比afterFrameId新的当前世代合法帧；false表示没有可用新帧，不必视为错误 |
| `TryAcquireSourceCopyLease(in HumanVisionTextureFrame frame, out SourceCopyLease lease)` | bool | 为精确源/世代/资源帧申请GPU拷贝租约；旧世代或容量压力可false |

### 6.3 `HumanVisionSourceSettings`及`RtspSourceSettings`

| 字段 | 默认/内容 |
| --- | --- |
| `Kind` | 指明Video/WebCamera/Rtsp，必须匹配实现；RTSP子类构造器设为Rtsp |
| `Location` | 视频路径/URL或完整RTSP地址；Camera一般不用 |
| `DeviceName` | 精确相机名称，空通常由源选默认设备 |
| `RequestedWidth/RequestedHeight` | 1280/720；采集请求，不保证实际输出 |
| `RequestedFramesPerSecond` | 30；源请求，不保证实际源率/推理率 |
| `DisplayMirror` | false；让源输出执行显示镜像并报告metadata，消费者不要再镜像 |
| `RtspSourceSettings.Transport` | `RtspTransport.Tcp`；枚举Tcp/Udp |
| `OpenTimeoutMs` | 5000；RTSP打开超时毫秒 |
| `ReconnectDelayMs` | 500；重连等待毫秒 |

### 6.4 `HumanVisionTextureFrame`每个字段

| 字段 | 意义 |
| --- | --- |
| `SourceId` | 源实例身份，ulong |
| `Generation` | 当前打开/重开的世代，ulong；换代清理旧结果与动作 |
| `FrameId` | 严格递增发布帧号，long |
| `Texture` | 完成发布的借用纹理 |
| `Width/Height` | 实际纹理像素尺寸，不是Requested值 |
| `PublishedTimestampUs` | Unity主线程完成发布的InputMonotonic时间，微秒 |
| `PublishedClockDomain` | 固定InputMonotonic，表示上述发布时间时钟 |
| `SourceTimestampUs` | Unity纹理观察或本地解码完成时间；不是自动验证过的传感器曝光时间 |
| `SourceClockDomain` | Unspecified/InputMonotonic/SourceLocalMonotonic；不同域不可直接相减 |
| `SourceClockId` | 0用于InputMonotonic；本地源时钟要求非0并随原点重置更新身份 |
| `PresentationTimestampUs` | 流PTS；没有时-1；可重置/回退，不能拿它直接算本地年龄 |
| `AppliedRotationDegrees` | 已施加到像素的旋转角度 |
| `AppliedMirrorX` | 已施加水平镜像，消费者不可再施加一次 |
| `RowOrigin` | UnityBottomLeft或NativeTopLeft；描述像素行原点 |
| `ColorSpace` | Srgb、Linear、Unknown |
| `TimestampKind` | UnityObserved或LocalDecode，解释SourceTimestampUs事件 |
| `ResourceToken` | 源资源登记令牌，用于精确租约/退休，不当成帧号 |

两个公开构造器：简化构造器以发布时间作UnityObserved源时间，只接受UnityObserved；完整构造器额外提供sourceTimestampUs/sourceClockDomain/sourceClockId，本地解码必须显式提供。自定义输入生产者要遵守帧身份/时钟/方向合同，不只塞一张Texture。

### 6.5 自定义源的资源工具

来源：[SourceRetirement.cs](../../upm/com.blazetc.humanvision.input/Runtime/SourceRetirement.cs)。标准组件/SDK桥已经使用，普通游戏无需自己建一套。

| API | 参数/返回和含义 |
| --- | --- |
| `ISourceCopyFence.IsComplete` | bool；精确GPU快照拷贝完成，非模型完成 |
| `SourceCopyLease.IsRetired` | bool；该租约身份已消费/取消 |
| `SourceCopyLease.RetireAfter(ISourceCopyFence fence)` | void；已排队拷贝提交精确完成fence；每租约只能提交一次，不能给推理完成fence冒充 |
| `SourceCopyLease.Dispose()` | void；仅取消未排队租约；已经排队不能直接取消 |
| `SourceRetirement(int resourceCapacity=16, int copyCapacity=64)` | 创建有限容量资源/拷贝管理器，绑定创建线程 |
| `PendingResourceCount` | 未释放的登记资源总数，含当前资源，不都是旧资源 |
| `Register(ulong generation, Texture texture, Action destroy)` | 返回资源token；登记借用纹理及最终释放回调 |
| `RetireGeneration(ulong generation)` | 将该世代资源标为退休；等所有拷贝结束才destroy |
| `Poll()` | 主线程轮询完成fence并释放可退休资源 |
| `SourceGeneration(SourceRetirement retirement)` | 创建源世代/发布管理器 |
| `SourceGeneration.SourceId/Generation/CurrentTexture` | 源ID、当前世代、当前借用纹理 |
| `BeginGeneration()` | 返回新世代ID并开始新发布时期 |
| `SourceGeneration.Close()` | 结束当前世代/退役资源 |
| `TryPublish(in HumanVisionTextureFrame frame)` | bool；验证身份、时钟、方向等并发布完成纹理 |
| `TryGetLatestFrame(afterFrameId,out frame)` | bool；仅取新合法发布 |
| `TryAcquireSourceCopyLease(in frame,out lease)` | bool；申请精确帧的资源租约 |
| `InputMonotonicClock.NowUs` | 输入包单调时间微秒；与源本地原生时钟需映射后再比较 |

`InputMonotonicClock.OriginStopwatchTicks`是本运行域初始化原点，`StopwatchFrequency`是计时器频率，`Domain`固定InputMonotonic。进程/Domain Reload会重建原点，不能比较跨运行保存的时间戳。

输入预览组件`FramePreview.Bind(IHumanVisionFrameSource value)`关联RawImage与借用源，`Refresh()`更新当前纹理；LateUpdate自动刷新，OnDisable清空显示纹理。它不提交骨骼推理，输入预览独立成功不表示识别成功。[源码](../../upm/com.blazetc.humanvision.input/Runtime/FramePreview.cs)

## 7. 提交与显示桥 `VideoPlayerFrameSource`

来源：[VideoPlayerFrameSource.cs](../../upm/com.blazetc.humanvision/Runtime/Demo/VideoPlayerFrameSource.cs)。名称保留历史用法，它当前也桥接统一Camera/Video/RTSP。

| API | 参数/返回与含义 |
| --- | --- |
| `Configure(HumanVisionManager visionManager, RawImage display, AspectRatioFitter fitter)` | void；关联识别器、可选预览/比例组件；项目自己管理预览时可传null |
| `BindUnifiedSource(IHumanVisionFrameSource source)` | void；绑定统一源，并在更新中提交合法新帧 |
| `DetachUnifiedSource()` | void；停止绑定并发起源拷贝退休；不代替Source.Close |
| `UnifiedRetirementPending` | bool；旧源快照还没退休时true；切源等到false再释放 |
| `CanPresentResult(long resultFrameId)` | bool；按当前源世代、提交/预览关系和策略判断结果能否呈现；true不代表有身体 |
| `ConfigureLiveInput(bool smoothPreview,int analysisWidth=1280,int analysisHeight=720)` | void；旧实时预览/分析目标策略；不重写Profile模型尺寸 |
| `StopFrames()` | void；停止桥自己的输入/显示工作；统一外部源生命期仍由调用方协调 |
| `PlayRelativeVideo(string relativeStreamingAssetsPath)` | bool；旧便捷接口，用StreamingAssets相对路径开始播放，失败看LastError |
| `PlayUrl(string path)` | bool；旧便捷媒体接口，当前视频分支要求存在本地文件，不能因为名字含Url就当万能网络播放器；统一VideoFrameSource按其输入设置定位资源 |
| `SubmitExternalTexture(Texture texture,long timestampUs)` | bool；旧实时纹理提交，默认方向 |
| `SubmitExternalTexture(Texture texture,long timestampUs,int rotationDegrees=0,bool mirrored=false)` | bool；额外提供方向 |
| `SubmitExternalTexture(Texture texture,long timestampUs,int rotationDegrees,bool mirrored,Texture previewTexture)` | bool；可独立预览纹理，提交受路线/背压约束；新源优先BindUnifiedSource |

| 属性/事件 | 意义 |
| --- | --- |
| `SourceWidth/SourceHeight` | 当前源实际尺寸 |
| `LatestSubmittedFrameId` | 最新接受提交的源帧ID |
| `PresentationFrameId` | 当前呈现/预览帧ID，可能领先于推理结果 |
| `PresentationTexture` | 预览借用纹理，不归应用销毁 |
| `ResultAgeMilliseconds` | 结果源年龄，毫秒；桥按路线转换时钟；无有效源时间时可0，不能把0当零延迟 |
| `ReadbackDrops/ReadbackErrors/FullFrameReadbackRequests` | CPU读回的丢弃、错误、整帧请求累计数，非所有原生GPU计数 |
| `MaxOverlayLagFrames/PresentationDelayFrames` | 旧显示允许落后/延迟帧策略；不是推理性能承诺 |
| `maxLiveResultAgeMilliseconds` | 公开float实时显示年龄阈值，默认3000；增加它只允许更旧骨骼显示 |
| `LivePreview` | 是否实时预览模式 |
| `CurrentVideoPath/LastError` | 当前旧媒体路径/桥错误 |
| `IsPlaying/IsStillImage/VideoFrameRate` | 输入播放状态/静态图/源报告率；不当作识别率 |
| `VideoLayoutChanged` | 无参事件，预览布局改变 |
| `PresentationFrameChanged` | 无参事件，呈现帧改变；不是新骨骼事件 |

统一输入路径的`CanPresentResult`检查Streaming、已提交帧范围与会话新身体序号；它不独立保证年龄、身体数和必需关节点全部有效。消费层还应检查这些条件。

### 7.1 `HumanVisionInputAdapter`高级接口

来源：[HumanVisionInputAdapter.cs](../../upm/com.blazetc.humanvision/Runtime/Demo/Input/HumanVisionInputAdapter.cs)。`BindUnifiedSource`会自动创建并关联它；通常使用桥即可，不要重复创建第二套提交器。

| API | 参数/返回与含义 |
| --- | --- |
| `manager` | HumanVisionManager字段；提交的目标会话 |
| `Bind(IHumanVisionFrameSource newSource)` | void；源不能为null，否则抛ArgumentNullException；解绑旧源并绑定新源，重置帧/会话门限 |
| `Detach()` | void；取消输入绑定，清空预览标识，存在复制/GPU工作时发起退休；不关闭外部源 |
| `Tick()` | void；自动Update调用；轮询退休、获取新帧、申请租约并按路线复制/提交。自定义调度时不要和自动Update重复驱动 |
| `CanPresentResult(long frameId)` | bool；当前源Streaming、帧在提交范围内、Manager新序号超过绑定门限才true |
| `PreviewTexture` | Texture或null；借用的最新预览，不归调用方销毁 |
| `LatestPreviewFrameId` | long；最新获取的输入发布帧ID，初始-1 |
| `LatestSubmittedFrameId` | long；最新接受的提交ID，初始-1；与输入发布计数分开 |
| `SourceId/SourceGeneration` | ulong；当前提交所绑定的源身份/世代 |
| `LastError` | string；提交/资源/能力错误，与源错误和Manager错误分开 |
| `CopiedFrames` | long；GPU槽位退休时Outcome=1计数，不是完成推理帧数；当前CPU路径不递增此计数 |
| `DroppedUnsubmittedFrames` | long；GPU槽位退休时Outcome非1计数；当前CPU路径不递增，不是所有源丢帧的总和 |
| `RetirementPending` | bool；旧复制/GPU生命周期尚待退休 |
| `PendingSourceCopies` | int；当前活跃源复制槽位数 |

## 8. SDK设置、质量和实际合同API

来源：[SharedRecognitionSettings](../../upm/com.blazetc.humanvision/Runtime/Demo/Input/SharedRecognitionSettings.cs)、[DemoModeSettings](../../upm/com.blazetc.humanvision/Runtime/Demo/Input/DemoModeSettings.cs)、[AnalysisContract](../../upm/com.blazetc.humanvision/Runtime/Demo/Input/AnalysisContract.cs)、[质量目录](../../upm/com.blazetc.humanvision/Runtime/HumanVisionModelInputQualities.cs)。

| 类型/字段或方法 | 内容 |
| --- | --- |
| `SharedRecognitionSettings.Version/MaxBodies/UseRegions/Regions` | 格式1、默认容量4、区域开关与归一化区域数组 |
| `AnalysisProfileId/ModelPackId` | 保存的实际合同身份；不是游戏自行选择模型权重的依据 |
| `DetectorWidth/DetectorHeight/PoseWidth/PoseHeight/DetectionCadence` | 实际合同信息；无独立检测器可为0；应由AnalysisContract.ApplyTo回填 |
| `UseWindowsCpu` | 默认false；PC选择cpu/directml Profile，Android构建路线另定 |
| `InputQuality` | 默认Medium；枚举Medium=0、High=1、Low=2，不能按数值大小排序质量 |
| `SharedRecognitionSettings.Clone()` | 复制设置并复制Regions数组 |
| `ResizeRegions(int count)` | 1～8，更新容量；数量改变会均分区域，相同数量保留原框 |
| `Validate()` | 非法版本/人数/区域/质量/合同字段抛异常 |
| `RuntimeProfileFor(RuntimePlatform platform)` | Android返回auto；其他平台按CPU开关返回PC Profile，不代表其他平台已支持 |
| `ResolveContract(string root,string baseProfile)` | 返回实际AnalysisContract；仅支持的Android质量族按等级选Profile；缺失/非法抛异常；PC保留实际尺寸 |
| `DemoModeSettings.Version` | 格式1 |
| `CameraDevice/VideoPath/RtspUrl/RtspComputerHost` | 相机名、视频路径、RTSP完整地址和推流电脑主机 |
| `Mirror/RequestedWidth/RequestedHeight/RequestedFramesPerSecond` | 显示镜像及采集请求，默认false/1280/720/30 |
| `LineWidth/PointDiameter` | 默认4.5/13.5，显示大小；允许线1～64、点1～128；不改模型 |
| `DemoModeSettings.Validate()` | 请求尺寸64～4096、FPS1～120及有限合法样式等；不等于设备保证支持 |
| `ToSourceSettings(InputKind kind)` | 返回匹配输入设置，RTSP返回RtspSourceSettings；继承默认TCP/超时 |
| `AnalysisContract.Load(root,profileId,capacity)` | 从实际Profile/ModelPack读合同；容量1～8；返回对象，缺文件/身份/尺寸冲突抛异常 |
| `AnalysisContract.ProfileId/ModelPackId/DetectorWidth/DetectorHeight/PoseWidth/PoseHeight/DetectionCadence` | 只读实际合同，不能通过UI任意改模型尺寸 |
| `AnalysisContract.ApplyTo(SharedRecognitionSettings settings)` | 回填实际合同字段 |
| `AnalysisContract.Validate(SharedRecognitionSettings settings)` | 要求保存设置与该合同一致，不一致抛异常 |
| `HumanVisionModelInputQualities.Load(string runtimeRoot)` | 加载并校验已索引质量目录；没有声明质量族可返回空选项，不从旧缓存猜出支持 |
| `ChoicesForMode(string mode)` | 返回该模式ModelInputQualityChoice数组副本；不支持模式为空 |
| `ProfilesForMode(string mode)` | 返回该模式允许质量Profile ID数组 |
| `ResolveQuality(string mode,ModelInputQuality quality)` | 返回支持的质量Profile；不可用抛异常 |
| `ResolveQuality(string mode,string quality)` | 字符串只接受low/medium/high；同上 |
| `CatalogFilename/AdmittedRuntimeMode` | 常量model-input-qualities.json / android-ncnn-vulkan |
| `ModelInputQualityChoice.Quality/ProfileId/Width/Height` | 选项等级、实际Profile和真实模型输入尺寸 |

RTSP预设工具：[HumanVisionRtspComputerHost](../../upm/com.blazetc.humanvision/Runtime/Demo/Input/HumanVisionRtspComputerHost.cs)。`Configure(host)`登记电脑IP；`ComputerHost`读取；`Resolve(Scene scene,string manualHost)`优先手填，其次场景记录，无合法值抛异常；`IsPrivateIpv4(host)`检查标准私有LAN IPv4；`BuildUrl(host,bool camera)`返回端口554、camera=true为`/videodevice`，false为`/video-1.mp4`。主机字段不能含端口/路径；其他主机用完整RTSP URL。工具不会启动服务。

统一Demo配置存储：[HumanVisionSettingsStore](../../upm/com.blazetc.humanvision/Runtime/Demo/Input/HumanVisionSettingsStore.cs)。构造器`HumanVisionSettingsStore(string directory=null)`默认persistentDataPath/HumanVisionUnifiedInput；`LoadShared()`返回SharedRecognitionSettings，缺文件默认，非法配置抛异常；`SaveShared(value)`验证并保存shared.json；`LoadMode(InputKind kind)`读取该模式DemoModeSettings，缺文件默认；`SaveMode(kind,value)`验证并保存Video/WebCamera/Rtsp.json，使用临时文件及.bak。这是SDK统一示例的配置存储，应用可自行定义其他配置格式。

Android构建选择：[HumanVisionAndroidRuntimeSelection](../../upm/com.blazetc.humanvision/Runtime/Android/HumanVisionAndroidRuntimeSelection.cs)。

| API | 内容 |
| --- | --- |
| `RuntimeModeMetadataKey/ProfileIdMetadataKey/QualityProfilesMetadataKey` | APK application metadata键：com.blazetc.humanvision.runtime_mode / profile_id / quality_profiles（后两项同前缀） |
| `ResolveConfiguredProfile(string configuredProfile,string bakedProfile)` | 空/auto请求返回bakedProfile；明确请求须与构建Profile相同；metadata缺失/冲突抛InvalidOperationException |
| `ResolveConfiguredProfile(string configuredProfile,string bakedProfile,string runtimeMode,string[] admittedProfiles)` | 额外校验同模式质量allowlist；空allowlist退回严格基Profile；支持的三个质量必须和NCNN模式/基Profile一致；请求只能在allowlist内 |
| `ParseBakedQualityProfiles(string metadata)` | 从JSON字符串数组返回string[]，空值返回空数组，格式错抛InvalidOperationException |

普通Manager初始化已调用内部解析；自定义设置界面也应按metadata验证Profile，避免请求APK不允许的组合。这些API不能把改过的配置字符串变成已安装的模型。

## 9. 骨骼显示和兼容相机门面

### 9.1 `HumanVisionOverlay`

来源：[HumanVisionOverlay.cs](../../upm/com.blazetc.humanvision/Runtime/Demo/HumanVisionOverlay.cs)。

- `Configure(HumanVisionManager visionManager,VideoPlayerFrameSource videoFrameSource)`：关联结果和呈现桥。
- `ConfigureStyle(float lineWidth,float pointDiameter)`：改变显示线宽和点直径；不改识别阈值/模型。
- `mainTexture`：Graphic使用的白色纹理，通常无需应用调用。
- 这是UGUI MaskableGraphic，需要RectTransform、CanvasRenderer和正确Canvas层级。`enabled=true`仅表示组件开启；零身体或显示门禁失败仍会无骨骼。

### 9.2 `HumanVisionCameraManager`

命名空间`HumanVision`；来源：[CameraManager](../../upm/com.blazetc.humanvision/Runtime/Demo/Live/HumanVisionCameraManager.cs)。旧接入/官方Demo可使用。使用第2节底层Manager例子时，不要同时再创建自动启动的相机门面，以免重复打开输入/会话。

| API | 参数/返回和含义 |
| --- | --- |
| `Instance` | 当前门面单例；可能null |
| `Settings` | HumanVisionCameraSettings；旧相机配置，修改后需ApplySettings |
| `startAutomatically` | 自动启动开关 |
| `targetDisplayFrameRate` | 默认60的显示目标，非识别率 |
| `forceCpu` | 请求CPU对照；runtimeProfileOverride优先 |
| `runtimeProfileOverride` | 显式Profile覆盖，空时按默认选择 |
| `Status/IsReady/ActiveRuntimeProfile/InputStatus/ResultSequence` | 状态、是否已初始化、实际Profile、输入状态、新身体序号 |
| `SkeletonUpdated` | Action<long>新骨骼事件；读取后不要保留复用数组引用 |
| `ApplySettings()` | bool；校验并应用相机/区域/Runtime设置，控制操作 |
| `StartCamera()/StopCamera()` | void；启动/停止门面自有源；统一绑定源应由其拥有者关闭 |
| `SaveSettings()/LoadSettings()` | void；保存/加载旧相机配置；与统一Demo共享设置文件不同 |
| `GetColorImageTex()` | Texture/null；借用预览纹理 |
| `GetColorImageWidth()/GetColorImageHeight()` | 像素尺寸，无纹理0 |
| `GetJointCount()` | 23，即旧17+6槽位，不保证23点有效 |
| `GetUsersCount()` | 当前新鲜区域身体数，结果不新鲜时0 |
| `GetRegionCount()` | 角色槽数量，不是当前人数 |
| `TryGetBodyByRegionIndex(int index,out HumanVisionBody body)` | bool；有效新鲜区域原始身体，失败body=null |
| `IsUserDetected(int index)` | bool；该区域是否存在可用原始身体 |
| `TryGetSampledBodyByRegionIndex(int index,out HumanVisionBody body)` | bool；当前源可呈现的采样身体，用于显示 |
| `GetUserIdByIndex(int index)` | ulong兼容TrackId，失败0；新代码Body.StableTrackId |
| `GetUserIndexById(ulong id)` | 区域下标，找不到/0返回-1 |
| `TryGetJointByRegionIndex(int index,HumanVisionJointType type,out HumanVisionJoint joint)` | bool；按旧23点枚举取关节点，返回值就是是否有效 |
| `IsJointTracked(ulong userId,HumanVisionJointType joint)` | bool；兼容ID的点有效性 |
| `GetJointPosition2D(ulong userId,HumanVisionJointType joint)` | Vector2预览像素坐标，失败Vector2.zero；先查有效性避免把真原点与失败混淆 |
| `GetJointPosition(ulong userId,HumanVisionJointType joint)` | Vector3(x_norm-0.5,0.5-y_norm,0)，失败zero；中心原点Y向上、z恒0，非米制深度 |

`HumanVisionJointType`旧枚举顺序为Nose、LeftEye、RightEye、LeftEar、RightEar、LeftShoulder、RightShoulder、LeftElbow、RightElbow、LeftWrist、RightWrist、LeftHip、RightHip、LeftKnee、RightKnee、LeftAnkle、RightAnkle、LeftHand、LeftHandtip、LeftThumb、RightHand、RightHandtip、RightThumb。与32语义枚举顺序不同，不能相互强转下标。

旧`HumanVisionCameraSettings`：version/source/deviceName/rtspUrl/rtspTcp/width/height/framesPerSecond/mirror/people/useRegions/regions是旧单源相机配置；`ResizeRegions(count)`1～8、改变数目时均分；`Validate()`校验；`Save()`写`FilePath`（persistentDataPath/HumanVisionCamera.json）；`Load()`读取、缺文件默认，坏配置抛异常。这份旧配置与统一Demo共享/模式配置使用不同文件路径，不能混用。

### 9.3 对象骨骼渲染组件

[`HumanVisionSkeletonOverlayer`](../../upm/com.blazetc.humanvision/Runtime/Demo/Live/HumanVisionSkeletonOverlayer.cs)用于官方对象点/LineRenderer显示：`manager/preview/foregroundCamera`绑定门面、预览和前景相机；`renderLayer`是对象层（其他游戏相机也要排除该层）；`jointPrefab/linePrefab`为可选对象模板；`drawSkeleton/drawJoints/drawBones`控制显示；`lineWidthPixels/jointDiameterPixels`调大小；`planeDistance`定义显示平面位置。类默认9/27，统一设置的4.5/13.5会覆盖实际样式。它把图像点映射到显示平面，不增加3D深度。第2节使用UGUI Overlay；官方统一示例协调对象骨骼与Overlay，避免重复绘制。叠加绘制组件不会增强识别。

### 9.4 举手示例组件

[`HumanVisionRaisedHandDetector`](../../upm/com.blazetc.humanvision/Runtime/Demo/Live/HumanVisionRaisedHandDetector.cs)通过**腕与肩的高度**判断举手，不需要真实Handtip/Thumb，也不能证明手指识别已启用。

| 字段/属性 | 内容 |
| --- | --- |
| `manager` | 使用的HumanVisionCameraManager |
| `regionIndex` | 要检测的区域0～7 |
| `heightMargin` | 默认0.06，腕需高于肩的归一化图像高度差；图像Y向下 |
| `minimumConfidence` | 默认0.3，腕和肩都须达到该置信度且有效 |
| `maximumPoseAgeMilliseconds` | 默认1500ms，超过后即使骨骼还显示也不当作当前举手 |
| `LeftHandRaised/RightHandRaised` | 只读bool，每帧重新检查对应侧 |
| `IsDetected` | 任一侧举手为true |
| `Status` | 等待/无骨骼/过旧/左/右/双手举起等描述 |

## 10. C ABI调用与结果码

Unity应用优先用上述托管API。原生应用/自建封装需包含[humanvision_types.h](../../native/include/humanvision/humanvision_types.h)、[humanvision_c.h](../../native/include/humanvision/humanvision_c.h)、[humanvision_v2.h](../../native/include/humanvision/humanvision_v2.h)。这些头文件包含精确参数类型和内存布局，不能把C++类/STL跨边界传递。

### 10.1 共同参数与返回

- `handle`：Create返回的不透明句柄；只能由对应Destroy释放，不混用V1/Runtime/Input句柄。
- `struct_size`/`api_version`：按该头文件结构和版本填写；不要改变旧V1布局。RuntimeV1的API版本为`HV_API_VERSION_040`，StatsV2独立版本为2。
- 字符串：UTF-8；out字符串缓冲由调用方给出容量（字节），即时复制错误描述。
- `capacity/written`：容量是元素数（除错误/字节缓冲明确字节），written为实际写入数；不要遍历未写入部分。
- `HV_OK=0`成功；`HV_NO_NEW_RESULT=1`无新结果；负值为错误。-1非法参数、-2未初始化、-3模型加载失败、-4不支持格式、-5内部错误。不是所有非0都表示崩溃。
- `HV_VideoFrame`内容与CPU提交参数对应；`HV_Rect` x/y/width/height；V1`HV_Body`17点。语义`HV_CanonicalBodyV1`含64位track_id、区域revision、源frame/time及32个独立有效性/时间/预测的关节。

### 10.2 语义Runtime API每项

| API | 参数及返回内容 |
| --- | --- |
| `HV_RuntimeClockUs()` | 返回Runtime原生steady时钟微秒；与Unity/Input时钟须显式换算 |
| `HV_RuntimeCreate(config,out_handle,error,error_capacity)` | config含UTF8 root/profile、max_people；返回HV_Result，失败写错误；可能耗时 |
| `HV_RuntimeSubmit(handle,frame)` | 复制CPU输入并异步处理；HV_Result；GPU Profile需专用GPU入口 |
| `HV_RuntimeSetRegions(handle,rects,count,revision)` | count0禁用，归一化不重叠区域；HV_Result |
| `HV_RuntimeCopy(handle,sample_timestamp_us,bodies,capacity,written,stats)` | 一次一致快照；时间0取原始观察，非0取同输入时钟的显示采样；输出HV_CanonicalBodyV1和RuntimeStatsV1；HV_Result |
| `HV_RuntimeGetError(handle,error,capacity)` | 把最近错误复制到调用方UTF8缓冲；HV_Result |
| `HV_RuntimeGetDiagnostics(handle,text,capacity)` | 复制实际组件/后端诊断文本；HV_Result，不当成稳定业务协议 |
| `HV_RuntimeGetStatsV2(handle,stats)` | 独立版本2统计：fresh/output/source/GPU/detector/验证/年龄等；不可用传感器年龄为NaN，不能写成0 |
| `HV_RuntimeRecordSourceFrameV2(handle,rate_limited)` | 记录真实源到达及限速丢弃；调用方不能为重复帧重复加计数 |
| `HV_RuntimeSetCaptureProvenanceV2(handle,provenance)` | UNKNOWN/UNITY_OBSERVED/SENSOR_VERIFIED声明；只有验证同一图像传感器时间才可用SENSOR_VERIFIED |
| `HV_RuntimeDestroy(handle)` | void；释放Host及任务/模型；之后句柄失效 |

`HV_RuntimeStatsV1`每项：body_sequence/hand_sequence独立序号；source_frame_id/source_timestamp_us结果源身份/时间；region_revision区域版本；dropped_frames丢弃计数；body_fps/hand_fps观察/手任务率；preprocess_ms/inference_ms/postprocess_ms阶段毫秒。StatsV2更细字段以头文件注释为准，禁止把output_samples当fresh_observation_frames。

### 10.3 旧V1 API每项

| API | 参数及返回内容 |
| --- | --- |
| `HV_GetVersionString()` | SDK版本字符串指针，调用方不释放 |
| `HV_Create(config,out_handle)` | HV_Config含容量、阈值、间隔、跟踪、后端、两模型UTF8路径；HV_Result |
| `HV_Reconfigure(handle,config)` | 修改V1配置；HV_Result；控制操作，不逐帧使用 |
| `HV_SubmitFrame(handle,frame)` | CPU帧异步提交；HV_Result |
| `HV_GetLatestResultMeta(handle,out_meta)` | 序号、源frame/time、身体数；HV_Result，可无新结果 |
| `HV_GetBodyCount(handle)` | int32当前身体数；数量查询不能替代一致快照 |
| `HV_GetBodies(handle,out_bodies,capacity,written)` | 复制身体数组；容量单位身体；HV_Result |
| `HV_GetHandJoints(handle,expected_sequence,joints,capacity)` | 同身体快照顺序的每体6点，capacity单位关节点；序号不匹配拒绝；17点旧模型手无效 |
| `HV_GetStats(handle,out_stats)` | HV_Stats FPS、阶段毫秒、累计提交/处理/丢帧；HV_Result |
| `HV_SetRegions(handle,regions,count,revision)` | 归一化区域、count0关闭；HV_Result |
| `HV_GetRegionAssignments(handle,expected_sequence,indices,capacity,revision)` | 取同次Bodies对应区域；指定序号过期拒绝；HV_Result |
| `HV_GetLastError(handle)` | 最近错误UTF8借用指针，应即时复制，不由调用方free |
| `HV_Destroy(handle)` | void；释放V1句柄；之后不再用 |

V1 Meta/Bodies/区域/手分别查询可能跨快照，需序号核对并处理被更新情况；不能拼接不同结果冒充同一帧。新代码优先语义RuntimeCopy。

### 10.4 独立Input C ABI与旧RTSP入口

来源：[humanvision_input.h](../../native/input/include/humanvision_input.h)。Options的size/version按ABI1填写，含url、最大尺寸、打开超时、重连等待和TCP开关。

| API | 内容 |
| --- | --- |
| `HV_Input_QueryClock(info)` | 返回Input插件本地时钟ID、now、原点/频率；不是SDK/Unity/UTC/传感器时钟 |
| `HV_Input_Open(options,out_handle)` | 创建独立输入会话；随后查状态 |
| `HV_Input_Close(handle)` | 请求取消；仍可Poll/GetState/GetLastError，尚未释放句柄 |
| `HV_Input_Release(handle)` | 重复调用至OK；BUSY时句柄仍有效；成功后全部API禁止再调用；Release与其他调用需串行 |
| `HV_Input_GetState(handle,out_state)` | 输出STOPPED/OPENING/STREAMING/RECONNECTING/FAILED/CLOSING |
| `HV_Input_GetLastError(handle,utf8,capacity)` | 复制输入错误文本 |
| `HV_Input_PollFrame(handle,after_sequence,info)` | 查询新帧尺寸/跨度/序号/世代/解码时间/PTS/时钟信息 |
| `HV_Input_CopyRgba(handle,sequence,rgba,capacity,info)` | 原子复制精确序号图像与metadata；capacity字节，输出调用方top-left RGBA，被覆盖/退休返回NO_FRAME |

Input返回码独立：OK=0、NO_FRAME=1、BUSY=2、INVALID=-1、BUFFER_TOO_SMALL=-2、ERROR=-3，不能按HV_Result负值含义解释。帧info的received/decoded时间来自本地时钟，presentation是PTS并有pts_valid，源时间要按clock_domain/clock_id映射。

旧可选[humanvision_rtsp.h](../../native/include/humanvision/humanvision_rtsp.h)：`HV_RtspOpen(url,width,height,tcp,timeout_ms)`返回句柄；`HV_RtspCopyFrame(handle,after_sequence,rgba,capacity,out width/height/sequence/timestamp_us)`返回1有帧、0无新帧、-1参数/缓冲错误；`HV_RtspState(handle)`1连接/2流式/3重连/4停止；`HV_RtspClose(handle)`立即使句柄失效并异步释放worker。它和新的Input_Close/Release生命周期不同。

## 11. 扩展与维护接口入口

底层GPU接入/算法插件开发不是游戏逐帧调用面，不能仅按本页表格自行实现同步协议。完整入口：

| 扩展 | 必须阅读 |
| --- | --- |
| 自定义Android GPU提交、资源池、copy ticket/fence | [humanvision_android_gpu.h](../../native/include/humanvision/humanvision_android_gpu.h)、[Android托管适配](../../upm/com.blazetc.humanvision/Runtime/Android) |
| Android Input AHB/Vulkan来源 | [android_input_gpu.h](../../native/input/include/android_input_gpu.h)、[android_input_vulkan.h](../../native/input/include/android_input_vulkan.h) |
| 流水线/后端插件 | [插件开发指南](../maintenance/PLUGIN_DEVELOPMENT.md)、[插件ABI1](../../runtime/include/humanvision_plugin.h)、[ABI2](../../runtime/include/humanvision_plugin_v2.h)、[ABI3](../../runtime/include/humanvision_plugin_v3.h) |
| 模型、归一化与输出schema替换 | [MODEL_PACK_GUIDE](../maintenance/MODEL_PACK_GUIDE.md)、[PROFILE_GUIDE](../maintenance/PROFILE_GUIDE.md) |
| 维护路径/兼容性 | [START_HERE](../maintenance/START_HERE.md)、[UNITY_STABLE_API](../maintenance/UNITY_STABLE_API.md) |

对应用开发者，算法替换应主要发生在Profile/ModelPack/插件，不把模型张量下标或具体后端类带进游戏API。新增公开接口时同步维护参数、错误、线程、所有权、坐标/时钟和验证说明。
