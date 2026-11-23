# MediaPipeUnityPlugin 异步取帧与线程机制核查

日期：2026-11-23。范围：源码研究，未安装插件、未运行样例、未测性能，不更换本SDK模型或后端。

固定 homuler 源码版本：[`eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6`](https://github.com/homuler/MediaPipeUnityPlugin/commit/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6)。其 [WORKSPACE](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/WORKSPACE#L28-L44) 使用 MediaPipe v0.10.22；相关上游实现按 `c54c06dd8c4314a316c14da31493bcc38ed302e2` 核查。

## 1. 结论

它确有值得借鉴的异步推理、有界背压、结果快照交接和GPU图像传递优化。但**当前样例没有把持续供帧完全移出Unity帧循环，也没有隔离游戏和识别的物理GPU资源**。

因此不应将“VideoPlayer／WebCamTexture可异步准备或产帧”与“SDK可在Unity主线程停顿时持续收到新图像”混为一谈，也不应反过来说所有异步输入都必须同步阻塞。需要逐段测量生产、交接、执行和展示。

## 2. 当前 Tasks 路径

```text
Unity ImageSource 纹理
  → Unity coroutine 取图／准备 TextureFrame
  → GPU image 或 CPU readback
  → LIVE_STREAM DetectAsync（非阻塞提交）
  → MediaPipe native graph 执行
  → native 结果 callback
  → 复制最新 annotation 结果
  → Unity LateUpdate 绘制
```

[VisionTaskApiRunner](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Common/Scripts/VisionTaskApiRunner.cs#L21-L28) 仍通过 StartCoroutine 启动；[Pose runner](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Scenes/Pose%20Landmark%20Detection/PoseLandmarkerRunner.cs#L82-L126) 在循环中取当前纹理、等待帧末／readback，再提交。由此可推断：已经提交的任务可继续完成，但仅照搬样例不能保证主线程长停顿期间持续供新帧。此为代码结构判断，本轮未做停顿实验。

WebCamSource 的 Play 是 coroutine，内部创建／启动 WebCamTexture，并用 WaitUntil 等尺寸有效；GetCurrentTexture 返回该Unity纹理。[WebCamSource](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Common/Scripts/ImageSource/WebCamSource.cs)

VideoSource 内部创建 VideoPlayer，调用 Prepare 并等待 isPrepared，随后Play；GetCurrentTexture 返回 player.texture。这支持异步准备，却不是独立原生文件取帧接口。[VideoSource](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Common/Scripts/ImageSource/VideoSource.cs)

## 3. 非阻塞提交和有界背压

LIVE_STREAM 的 DetectAsync 发送带时间戳的 image／rect packet 后返回；IMAGE／VIDEO 模式使用同步Process。不能把方法名中的Video和Unity VideoPlayer入口视为相同的线程合同。[PoseLandmarker](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Tasks/Vision/PoseLandmarker/PoseLandmarker.cs#L209-L231)、[BaseVisionTaskApi](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Tasks/Vision/Core/BaseVisionTaskApi.cs#L70-L116)

LIVE_STREAM 配置 FlowLimiter：MaxInFlight=1、MaxInQueue=1，并由下游完成信号释放额度。上游实现会丢弃超容量队列中的旧packet，避免推理速度不足时持续增加延迟；提交成功不保证每帧都有输出。[TaskInfo](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Tasks/Core/TaskInfo.cs#L51-L78)、[FlowLimiter实现](https://github.com/google-ai-edge/mediapipe/blob/c54c06dd8c4314a316c14da31493bcc38ed302e2/mediapipe/calculators/core/flow_limiter_calculator.cc#L140-L156)

本SDK已经有 latest-frame slot、admission和原生worker。应复用并完善完成驱动，不能把借鉴FlowLimiter解释为再增加一套并行调度系统。

## 4. Native callback 与 Unity 展示交接

C++ TaskRunner 的packet callback由native执行上下文进入wrapper，托管CallbackTable直接调用登记的callback，没有自动切到Unity主线程。默认graph executor使用线程池；自定义executor可能不同，不能保证所有callback都在同一后台线程。[native wrapper](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/mediapipe_api/tasks/cc/core/task_runner.cc#L14-L21)、[callback table](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Tasks/Core/PacketsCallbackTable.cs#L33-L57)、[上游executor选择](https://github.com/google-ai-edge/mediapipe/blob/c54c06dd8c4314a316c14da31493bcc38ed302e2/mediapipe/framework/calculator_graph.cc#L438-L475)

Pose样例在callback中调用DrawLater，锁内CloneTo保存结果，之后由LateUpdate真正绘制。这是清楚的“完成与展示分离”。但Pose wrapper还在锁内复用result并执行用户callback；把慢业务直接放进去仍可能拖慢回调链，保存借用引用也可能读到被重用的内容。[runner callback](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Scenes/Pose%20Landmark%20Detection/PoseLandmarkerRunner.cs#L162-L165)、[DrawLater](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/Annotation/PoseLandmarkerResultAnnotationController.cs#L34-L59)、[LateUpdate](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/Annotation/AnnotationController.cs#L70-L76)、[复用result的callback](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Tasks/Vision/PoseLandmarker/PoseLandmarker.cs#L240-L265)

本SDK采用短完成通知→现有coordinator语义发布→不可变快照／有界日志；Unity ResultUpdated保持现有主线程语义。无需向任意managed业务直接暴露native回调。

## 5. GPU 图像与异步回读的边界

当前Pose runner用OpenGLES3与GpuResources判断GPU图像支持，再走ReadTextureOnGPU→BuildGPUImage；其他路径可选CPU同步或异步readback。这不证明Vulkan可直接套用，也不意味着选择GPU delegate就必然消除回读。[分支及GPU交接](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Scenes/Pose%20Landmark%20Detection/PoseLandmarkerRunner.cs#L78-L126)

该样例GPU交接仍等待帧末，源码TODO明确说明拷贝完成保证尚不充分。**不采纳“等一帧就认为GPU拷贝完成”的做法。** 本SDK保留真实fence／sync-fd、generation和源纹理租约，不能以减少等待为由移除同步。

纹理池和异步API能减少分配／显式同步等待，但不能推出每次取槽都无阻塞，或推理与游戏不再共享GPU。CPU输入仍需真实像素搬运；GPU图像路径仍有图像复制、同步和持有期。这里的目标是减少CPU中转，不是物理算力隔离。

进一步核对TextureFrame实现：

| 路径 | 实际数据操作 |
| --- | --- |
| ReadTextureOnGPU | Graphics.Blit→Graphics.CopyTexture；无显式CPU像素读回，但仍有GPU复制 |
| ReadTextureOnCPU | ReadPixels→Apply；同步回读 |
| ReadTextureAsync | AsyncGPUReadback.Request；回调GetData→LoadRawTextureData→Apply，仍有CPU数据与Texture2D上传 |

[三种读取实现](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/Experimental/TextureFrame.cs#L116-L156)、[异步完成回调](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/Experimental/TextureFrame.cs#L183-L192)。该异步读取对象在完成前不能被其他操作并发重用，不能把调用移到任意线程就当线程安全。

BuildGPUImage包装GL纹理与producer context，native shared_ptr最后消费者回调归还TextureFrame并附带consumer同步token；池取出时还调用WaitUntilReleased，其同步token等待会阻塞CPU。**归还池与GPU消费结束是两个事件**，不能照搬成“池中可取即可无等待重用”。[GPU包装](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/Experimental/TextureFrame.cs#L244-L308)、[pool等待](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/Experimental/TextureFramePool.cs#L122-L143)

Android共享GL context也经GL.IssuePluginEvent从Unity渲染回调取得，再用于GpuResources初始化。这是互操作优化，不能作为无Unity graphics context初始化的证据。[AsyncGlContext](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/AsyncGlContext.cs#L33-L60)、[GpuManager](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/GpuManager.cs#L46-L62)

## 6. Legacy 与 Tasks 分开

旧Holistic的异步模式通过Graph listener回调，同步模式才等待WaitNextAsync后DrawNow；外层仍是Unity coroutine供帧。旧OutputStream的WaitNextAsync通过Task包裹poller等待，不能把这一实现描述成当前Tasks的DetectAsync。[Legacy runner](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Common/Scripts/LegacySolutionRunner.cs#L27-L35)、[Holistic](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Assets/MediaPipeUnity/Samples/Scenes/Legacy/Holistic/HolisticTrackingSolution.cs#L102-L178)、[OutputStream](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Unity/OutputStream.cs#L335-L410)

## 7. 对改造方案的具体影响

| 借鉴项 | 采用方式 | 不作的推论 |
| --- | --- | --- |
| 异步任务提交 | 保留兼容图像入口、安全交接即返回，原生Host处理 | 不把coroutine称为独立采集线程 |
| FlowLimiter | 每流有界在途＋latest pending＋各阶段drop原因 | 不通过无限加队列提高表面FPS |
| Native结果监听 | 完成事件自主更新canonical结果，无需Unity轮询来触发 | 不在worker／持锁callback里运行游戏业务 |
| DrawLater／快照 | caller buffer复制／有界journal，Unity只显示最新 | 不保存会被复用的借用result引用 |
| GPU image／纹理池 | 按平台能力减少CPU读回，复用已验证的buffer与fence | 不复制GLES3指针代码到Vulkan，也不以等待一帧替代fence |
| 输入抽象 | RTSP／视频／相机共用协议；兼容入口与原生自主入口并存 | 不宣称必须一次替换全部Unity source才能有任何收益 |

平台说明：其README基线为Unity>=2022.3；当前Tasks的BaseOptions公开CPU／GPU delegate，没有现成RKNN／RGA通用接入。本SDK还需维护2021.3兼容，故本次采纳机制，不直接安装或移植整套插件。[README](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/README.md)、[BaseOptions](https://github.com/homuler/MediaPipeUnityPlugin/blob/eeeb7c9666ce5db71665eb9d46bd2b25a76fe8a6/Packages/com.github.homuler.mediapipe/Runtime/Scripts/Tasks/Core/BaseOptions.cs)

完整设计与分阶段实施见[独立运行设计](../superpowers/specs/2026-11-23-sdk-independent-runtime-design.md)和[实施计划](../superpowers/plans/2026-11-23-sdk-independent-runtime-plan.md)。CPU／GPU／NPU应先共用这些异步机制及原生图像服务。RGA是同一服务协议的可选实现，可进一步减少GPU准备／回读成本；不要求NPU另建一套调度，也不以RGA可用作为RKNN独立运行的前提。
