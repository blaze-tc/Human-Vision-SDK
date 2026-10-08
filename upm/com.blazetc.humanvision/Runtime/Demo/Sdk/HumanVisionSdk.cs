using System;
using System.Collections;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.UI;

namespace HumanVision
{
    /// <summary>
    /// 直接挂载即可使用的 SDK 总控。所有 API 在 Unity 主线程调用。
    /// 人员 index 为可能有空位的稳定槽位；世界坐标是配置平面的映射，不是测量深度。
    /// </summary>
    [DisallowMultipleComponent, AddComponentMenu("Human Vision/Human Vision SDK")]
    public sealed class HumanVisionSdk : MonoBehaviour
    {
        /// <summary>托管 SDK 版本，便于区分实机日志对应的发布包。</summary>
        public const string Version = "0.4.0-preview.6";
        [Header("启动与输入"), SerializeField] private HumanVisionSdkOptions options = new HumanVisionSdkOptions();
        [SerializeField, Tooltip("进入 Play Mode 后自动准备 Runtime 并打开配置输入。")] private bool initializeOnStart = true;
        [Header("屏幕与世界坐标"), SerializeField, Tooltip("可选实际视频 RawImage 的 RectTransform；留空为整个屏幕。")] private RectTransform screenTarget;
        [SerializeField, Tooltip("世界平面中心与朝向；留空使用总控自身 Transform。")] private Transform worldPlane;
        [SerializeField, Tooltip("世界平面的宽高，单位为 Unity 世界单位。")] private Vector2 worldPlaneSize = Vector2.one;

        private HumanVisionManager manager;
        private VideoPlayerFrameSource bridge;
        private IHumanVisionFrameSource source;
        private HumanVisionSdkLifetime host, retirement;
        private HumanVisionSdkOptions active;
        private HumanVisionSkeletonQueries raw, sampled;
        private long revision, minimumSequence;
        private int operation;
        private bool acceptResults, sourceFresh;
        private ulong sourceId, sourceGeneration;
        private readonly long[] previousIds = new long[8];
        private readonly HumanVisionRegionOccupancy[] previousOccupancy = new HumanVisionRegionOccupancy[8];
        private readonly Vector3[] corners = new Vector3[4];
        private string preparedRoot;
        private AnalysisContract activeContract;
        private HumanVisionSdkQualityCapabilities qualityCapabilities;
        private string qualityRoot;

        /// <summary>Runtime 已初始化后通知；输入是否流送请读取 IsRunning。</summary>
        public event Action Initialized;
        /// <summary>新原始识别结果通知，包括明确无人结果；不因插值/轮询重复触发。</summary>
        public event Action<long> ResultUpdated;
        /// <summary>有效参与者进入槽位，参数为 index、稳定 ID。</summary>
        public event Action<int, long> UserEntered;
        /// <summary>参与者离开或结果过期，参数为原 index、稳定 ID。</summary>
        public event Action<int, long> UserExited;
        /// <summary>区域的未知/无人/有人状态变化。</summary>
        public event Action<int, HumanVisionRegionOccupancy> RegionOccupancyChanged;
        /// <summary>输入和 Runtime 已安全释放后通知。</summary>
        public event Action Stopped;
        /// <summary>可处理的错误文本；调用 Try 方法失败时也可读取 LastError。</summary>
        public event Action<string> ErrorOccurred;
        /// <summary>当前生命周期状态。</summary>
        public HumanVisionSdkState State { get; private set; } = HumanVisionSdkState.Stopped;
        /// <summary>正在初始化、打开输入或安全停止；此时不能重复提交应用。</summary>
        public bool Busy => State == HumanVisionSdkState.Preparing || State == HumanVisionSdkState.Opening || State == HumanVisionSdkState.Stopping;
        /// <summary>Runtime 会话存在。</summary>
        public bool IsInitialized => manager != null && manager.IsInitialized;
        /// <summary>输入实际处于 Streaming，不表示本帧一定识别到人。</summary>
        public bool IsRunning => State == HumanVisionSdkState.Running && source != null && source.State == InputSourceState.Streaming;
        /// <summary>当前输入来源匹配且原始结果仍在时效范围内。</summary>
        public bool HasFreshResult => acceptResults && sourceFresh && raw != null && raw.HasFreshResult(NowUs);
        /// <summary>最近错误；成功应用后清除。</summary>
        public string LastError { get; private set; } = "";
        /// <summary>当前可编辑配置的副本。</summary>
        public HumanVisionSdkOptions Configuration => (active ?? options).Clone();
        /// <summary>已经成功应用的配置副本；尚未应用时为 null。</summary>
        public HumanVisionSdkOptions ActiveConfiguration => active?.Clone();
        /// <summary>输入当前状态。</summary>
        public InputSourceState InputState => source?.State ?? InputSourceState.Stopped;
        /// <summary>最近统计信息；统计与输入/渲染 FPS 不等价。</summary>
        public HumanVisionStats Stats => manager != null ? manager.Stats : default;
        /// <summary>当前实际 Runtime 配置和诊断字符串，用于实机日志；尚未初始化时为空。</summary>
        public string RuntimeProfile => manager != null && manager.IsInitialized ? manager.ActiveRuntimeProfile : "";
        public string RuntimeDiagnostics => manager != null && manager.IsInitialized ? manager.RuntimeDiagnostics : "";
        /// <summary>当前使用的资源目录与实际模型包；输入采集尺寸与模型尺寸分别记录。</summary>
        public string RuntimeRootPath => preparedRoot ?? "";
        public string ActiveModelPack => IsInitialized ? activeContract?.ModelPackId ?? "" : "";
        public Vector2Int AnalysisInputSize => IsInitialized && activeContract != null ? new Vector2Int(activeContract.PoseWidth, activeContract.PoseHeight) : Vector2Int.zero;
        /// <summary>原始结果序号。</summary>
        public long ResultSequence => manager != null ? manager.ResultSequence : 0;
        /// <summary>来源帧 ID；未初始化为 -1。</summary>
        public long SourceFrameId => manager != null ? manager.SourceFrameId : -1;
        /// <summary>来源观测时间，单位微秒；CPU 使用 Runtime 单调时钟，GPU 使用 Unity 单调时钟。请用 ResultAgeMilliseconds 获取年龄。</summary>
        public long SourceTimestampUs => manager != null ? manager.SourceTimestampUs : 0;
        /// <summary>当前结果年龄，毫秒；没有有效当前结果时为正无穷。</summary>
        public double ResultAgeMilliseconds => HasFreshResult ? (NowUs - SourceTimestampUs) / 1000d : double.PositiveInfinity;
        /// <summary>修改屏幕坐标使用的实际图像矩形。</summary>
        public RectTransform ScreenTarget { get => screenTarget; set => screenTarget = value; }
        /// <summary>修改世界映射中心与朝向。</summary>
        public Transform WorldPlane { get => worldPlane; set => worldPlane = value; }
        /// <summary>修改世界映射宽高；无效尺寸使世界位置查询失败。</summary>
        public Vector2 WorldPlaneSize { get => worldPlaneSize; set => worldPlaneSize = value; }
        /// <summary>是否在 Start 自动启动；自行调用 Initialize 时可关闭。</summary>
        public bool InitializeOnStart { get => initializeOnStart; set => initializeOnStart = value; }
        internal HumanVisionManager RuntimeManager => manager;
        internal VideoPlayerFrameSource FrameBridge => bridge;
        // 必须与当前会话返回的身体/关节观测时间使用同一时钟域。
        private long NowUs => manager != null && manager.UsesRuntimeProfile && !manager.UsesAndroidGpuFrames
            ? HumanVision.Interop.RuntimeBindings.HV_RuntimeClockUs()
            : (long)(Time.realtimeSinceStartupAsDouble * 1000000);

        private void Start() { if (initializeOnStart) StartCoroutine(Initialize()); }
        private void OnDisable() { StopAllCoroutines(); BeginStop(); }
        private void OnDestroy() { BeginStop(); }
        /// <summary>等待默认 Inspector 配置完成启动：yield return sdk.Initialize();</summary>
        public IEnumerator Initialize() => Initialize(options);
        /// <summary>等待指定配置启动；预检失败保留当前运行输入。完成后检查 IsRunning/LastError。</summary>
        public IEnumerator Initialize(HumanVisionSdkOptions requested)
        {
            if (Busy) { Error("SDK 正忙，请等待当前操作结束。"); yield break; }
            HumanVisionSdkOptions candidate = null;
            try { if (requested == null) throw new ArgumentNullException(nameof(requested)); requested.Validate(); candidate = requested.Clone(); }
            catch (Exception e) { Error(e.Message); }
            if (candidate == null) yield break;
            var previousState = State; State = HumanVisionSdkState.Preparing; int token = ++operation;
            string root = string.IsNullOrWhiteSpace(candidate.RuntimeRoot) ? preparedRoot : candidate.RuntimeRoot;
            string preparationError = null;
            if (string.IsNullOrEmpty(root))
                yield return HumanVisionRuntimeData.Prepare(value => root = value, value => preparationError = value);
            if (token != operation || !isActiveAndEnabled) yield break;
            AnalysisContract contract = null;
            try {
                if (string.IsNullOrEmpty(root)) throw new InvalidOperationException(preparationError ?? "运行资源准备失败。");
                var shared = new SharedRecognitionSettings {
                    MaxBodies = candidate.Recognition.MaxBodies, UseRegions = candidate.Recognition.UseRegions,
                    Regions = candidate.Recognition.Regions, InputQuality = candidate.InputQuality, UseWindowsCpu = candidate.UseWindowsCpu
                };
                string baseProfile = Application.platform == RuntimePlatform.Android
                    ? HumanVisionAndroidRuntimeSelection.ResolveProfile("auto") : shared.RuntimeProfileFor(Application.platform);
                contract = shared.ResolveContract(root, baseProfile);
            } catch (Exception e) { Error(e.Message); State = previousState; }
            if (contract == null) yield break;
            // 资源与配置验证结束后才能退役当前输入。取消旧操作后重新领取 token。
            BeginStop(); token = ++operation;
            while (retirement != null && !retirement.Complete) yield return null;
            if (token != operation || !isActiveAndEnabled) yield break;
            State = HumanVisionSdkState.Preparing; preparedRoot = root;
            var go = new GameObject(name + " Runtime Host");
            DontDestroyOnLoad(go); host = go.AddComponent<HumanVisionSdkLifetime>();
            manager = go.AddComponent<HumanVisionManager>();
            bridge = go.AddComponent<VideoPlayerFrameSource>(); bridge.Configure(manager, null, null);
            if (!manager.TryInitialize(new HumanVisionConfig { RuntimeRoot = root, Profile = contract.ProfileId, MaxBodies = candidate.Recognition.MaxBodies })) {
                string error = manager.LastError; yield return StopSdk(); State = HumanVisionSdkState.Error; Error(error); yield break;
            }
            activeContract = contract;
            if (!ApplyRecognition(candidate.Recognition)) {
                string error = LastError; yield return StopSdk(); State = HumanVisionSdkState.Error; Error(error); yield break;
            }
            LastError = ""; State = HumanVisionSdkState.Ready;
            manager.ResultUpdated += OnResult;
            Initialized?.Invoke();
            if (token != operation || !isActiveAndEnabled) yield break;
            if (!candidate.OpenInputOnInitialize) { active = candidate.Clone(); options = candidate.Clone(); yield break; }
            try {
                switch (candidate.SourceKind) {
                    case InputKind.WebCamera: source = go.AddComponent<WebCameraFrameSource>(); break;
                    case InputKind.Video: source = go.AddComponent<VideoFrameSource>(); break;
                    case InputKind.Rtsp: source = go.AddComponent<RtspFrameSource>(); break;
                }
                source.Open(candidate.ToSourceSettings()); bridge.BindUnifiedSource(source);
                acceptResults = true; State = HumanVisionSdkState.Opening;
            } catch (Exception e) { Error(e.Message); }
            if (State != HumanVisionSdkState.Opening) {
                string error = LastError; yield return StopSdk(); State = HumanVisionSdkState.Error; Error(error); yield break;
            }
            double deadline = Time.realtimeSinceStartupAsDouble + candidate.InputOpenTimeoutSeconds;
            while (token == operation && source != null && source.State != InputSourceState.Streaming &&
                source.State != InputSourceState.Error && Time.realtimeSinceStartupAsDouble < deadline) yield return null;
            if (token != operation || !isActiveAndEnabled) yield break;
            if (source != null && source.State == InputSourceState.Streaming) { active = candidate.Clone(); options = candidate.Clone(); State = HumanVisionSdkState.Running; LastError = ""; }
            else { string error = string.IsNullOrEmpty(source?.LastError) ? "输入未在规定时间内进入流送状态。" : source.LastError;
                yield return StopSdk(); State = HumanVisionSdkState.Error; Error(error); }
        }
        /// <summary>异步提交完整配置；true 表示请求已接受，不表示输入已成功启动。等待 Busy=false 后检查 IsRunning。</summary>
        public bool TryApplyConfiguration(HumanVisionSdkOptions requested)
        {
            if (!Application.isPlaying || !isActiveAndEnabled || Busy) { Error("完整输入配置需要在启用的 Play Mode 总控上应用，且 SDK 不能处于忙碌状态。"); return false; }
            try { if (requested == null) throw new ArgumentNullException(nameof(requested)); requested.Validate(); }
            catch (Exception e) { Error(e.Message); return false; }
            StartCoroutine(Initialize(requested)); return true;
        }
        /// <summary>应用共享识别设置；初始化前只更新草稿。成功后等待新区域版本的原始结果。</summary>
        public bool TryApplyConfiguration(HumanVisionSdkConfiguration requested)
        {
            if (Busy) { Error("SDK 正忙，不能应用识别设置。"); return false; }
            try {
                if (requested == null) throw new ArgumentNullException(nameof(requested)); requested.Validate();
                var candidate = requested.Clone();
                if (IsInitialized && !ApplyRecognition(candidate)) return false;
                options.Recognition = candidate;
                if (active != null) active.Recognition = candidate.Clone();
                LastError = ""; return true;
            } catch (Exception e) { Error(e.Message); return false; }
        }
        private bool ApplyRecognition(HumanVisionSdkConfiguration candidate)
        {
            long nextRevision = revision + 1;
            if ((manager.MaxBodies != candidate.MaxBodies && !manager.TrySetMaxBodies(candidate.MaxBodies)) ||
                !manager.TrySetRegions(candidate.UseRegions ? candidate.Regions : Array.Empty<Rect>(), nextRevision)) {
                // 底层两个调用不是事务；失败后拒绝查询并停止，避免报告部分应用为成功。
                Error(manager.LastError); BeginStop(); return false;
            }
            revision = nextRevision; minimumSequence = manager.ResultSequence;
            raw = new HumanVisionSkeletonQueries(candidate); sampled = new HumanVisionSkeletonQueries(candidate);
            regionIndices = new int[candidate.MaxBodies];
            sourceFresh = false; RefreshIdentityEvents(); return true;
        }
        /// <summary>安全关闭；可重复调用。场景切换前 yield return sdk.StopSdk();</summary>
        public IEnumerator StopSdk()
        {
            BeginStop();
            while (retirement != null && !retirement.Complete) yield return null;
        }
        /// <summary>销毁 Runtime 资源的可等待入口，与 StopSdk 相同；不会销毁用户的总控物体。</summary>
        public IEnumerator Shutdown() => StopSdk();
        private void BeginStop()
        {
            ++operation; acceptResults = sourceFresh = false; sourceId = sourceGeneration = 0;
            raw?.Clear(); sampled?.Clear(); RefreshIdentityEvents();
            if (manager != null) manager.ResultUpdated -= OnResult;
            if (host != null) {
                State = HumanVisionSdkState.Stopping;
                retirement = host; var owner = host; var ownedManager = manager; var ownedBridge = bridge; var ownedSource = source;
                host = null; manager = null; bridge = null; source = null;
                owner.Retire(ownedManager, ownedBridge, ownedSource, () => {
                    if (this != null && host == null) { State = HumanVisionSdkState.Stopped; Stopped?.Invoke(); }
                });
            } else if (retirement == null || retirement.Complete) State = HumanVisionSdkState.Stopped;
        }
        private void Update()
        {
            bool freshInput = false;
            if (acceptResults && source != null && source.State == InputSourceState.Streaming && source.TryGetLatestFrame(-1, out var frame)) {
                long age = InputMonotonicClock.NowUs - frame.PublishedTimestampUs;
                freshInput = age >= 0 && age <= (active?.Recognition.MaximumResultAgeMilliseconds ?? 1000) * 1000d;
                if (sourceId != 0 && (sourceId != frame.SourceId || sourceGeneration != frame.Generation)) {
                    raw?.Clear(); sampled?.Clear(); minimumSequence = ResultSequence;
                }
                sourceId = frame.SourceId; sourceGeneration = frame.Generation;
            }
            sourceFresh = freshInput;
            if (HasFreshResult && manager != null) {
                sampled.Clear();
                sampled.Observe(manager.SampledBodies, manager.SampledBodyCount, manager.ResultSequence,
                    manager.SourceFrameId, manager.SourceTimestampUs, true);
            } else sampled?.Clear();
            RefreshIdentityEvents();
            if (State == HumanVisionSdkState.Running && source?.State == InputSourceState.Error) {
                string message = source.LastError; BeginStop(); State = HumanVisionSdkState.Error; Error(message);
            }
        }
        private void OnResult(long sequence)
        {
            if (!acceptResults || !sourceFresh || manager == null || sequence <= minimumSequence ||
                !bridge.CanPresentResult(manager.SourceFrameId)) return;
            // 帧像素已应用镜像/旋转。来源代次改变时拒绝上一代晚到结果。
            if (!source.TryGetLatestFrame(-1, out var input) || input.SourceId != sourceId || input.Generation != sourceGeneration) {
                raw.Clear(); sampled.Clear(); sourceFresh = false; minimumSequence = sequence; RefreshIdentityEvents(); return;
            }
            // 无区域模式也需要版本隔离，防止重新配置前的晚到结果被当成当前观测。
            if (!manager.TryCopyRegionAssignments(regionIndices, out long currentRevision) || currentRevision != revision) return;
            if (!raw.Observe(manager.Bodies, manager.BodyCount, sequence, manager.SourceFrameId, manager.SourceTimestampUs, true)) return;
            RefreshIdentityEvents(); ResultUpdated?.Invoke(sequence);
        }
        private int[] regionIndices = Array.Empty<int>();
        private void RefreshIdentityEvents()
        {
            for (int i = 0; i < 8; i++) {
                long current = GetUserIdByIndex(i);
                if (previousIds[i] != current) {
                    long old = previousIds[i]; previousIds[i] = current;
                    if (old != 0) UserExited?.Invoke(i, old);
                    if (current != 0) UserEntered?.Invoke(i, current);
                }
                var value = TryGetRegionOccupancy(i, out bool occupied)
                    ? occupied ? HumanVisionRegionOccupancy.Occupied : HumanVisionRegionOccupancy.Empty : HumanVisionRegionOccupancy.Unknown;
                if (value != previousOccupancy[i]) { previousOccupancy[i] = value; RegionOccupancyChanged?.Invoke(i, value); }
            }
        }
        private void Error(string message) { LastError = message ?? "未知 SDK 错误。"; ErrorOccurred?.Invoke(LastError); }

        /// <summary>获取人数上限，不是当前实际人数。</summary>
        public int GetMaxBodies() => (active ?? options).Recognition.MaxBodies;
        /// <summary>设置人数上限；区域开启且长度不匹配时失败，请用组合配置一起提交。</summary>
        public bool TrySetMaxBodies(int count) { var value = Configuration.Recognition; value.MaxBodies = count; return TryApplyConfiguration(value); }
        /// <summary>获取实际有效人数；遍历槽位时仍应遍历 GetMaxBodies。</summary>
        public int GetUsersCount() => HasFreshResult ? raw.GetUsersCount(NowUs) : 0;
        /// <summary>槽位是否有当前有效参与者。</summary>
        public bool IsUserDetected(int index) => TryGetBodyByIndex(index, out _);
        /// <summary>稳定 ID 是否仍被跟踪。</summary>
        public bool IsUserTracked(long id) => GetUserIndexById(id) >= 0;
        /// <summary>获取稳定 ID；未检测到为 0。</summary>
        public long GetUserIdByIndex(int index) => TryGetBodyByIndex(index, out var body) ? body.StableTrackId : 0;
        /// <summary>通过稳定 ID 获取槽位；不存在为 -1。</summary>
        public int GetUserIndexById(long id) => HasFreshResult ? raw.GetUserIndexById(id, NowUs) : -1;
        /// <summary>借用当前身体结果；跨帧保存请使用 CopySkeletonByIndex。</summary>
        public bool TryGetBodyByIndex(int index, out HumanVisionBody body) { body = null; return HasFreshResult && raw.TryGetBody(index, NowUs, out body); }
        /// <summary>通过稳定 ID 借用当前身体结果。</summary>
        public bool TryGetBodyById(long id, out HumanVisionBody body) => TryGetBodyByIndex(GetUserIndexById(id), out body);
        /// <summary>显式借用渲染采样结果；不代表新的推理观测。</summary>
        public bool TryGetSampledBodyByIndex(int index, out HumanVisionBody body) { body = null; return HasFreshResult && sampled.TryGetBody(index, NowUs, out body); }
        /// <summary>读取 32 点语义关节数量，保持旧 17 点 API 的常量不变。</summary>
        public int GetJointCount() => 32;
        /// <summary>获取有效关节及其置信度、派生标志和独立时间戳。</summary>
        public bool TryGetJointByIndex(int index, HumanVisionCanonicalJointId joint, out HumanVisionCanonicalJoint value) { value = default; return HasFreshResult && raw.TryGetJoint(index, joint, NowUs, out value); }
        /// <summary>通过稳定 ID 获取有效语义关节。</summary>
        public bool TryGetJointById(long id, HumanVisionCanonicalJointId joint, out HumanVisionCanonicalJoint value) => TryGetJointByIndex(GetUserIndexById(id), joint, out value);
        /// <summary>检查关节是否满足时效/置信度要求。</summary>
        public bool IsJointTracked(int index, HumanVisionCanonicalJointId joint) => TryGetJointByIndex(index, joint, out _);
        /// <summary>源图像像素坐标，左上角原点。</summary>
        public bool TryGetJointImagePosition(int index, HumanVisionCanonicalJointId joint, out Vector2 position) { position = default; if (!TryGetJointByIndex(index, joint, out var value)) return false; position = value.Position.Pixel; return true; }
        /// <summary>源图像归一化坐标，左上角原点；输入显示变换已应用。</summary>
        public bool TryGetJointNormalizedPosition(int index, HumanVisionCanonicalJointId joint, out Vector2 position) { position = default; if (!TryGetJointByIndex(index, joint, out var value)) return false; position = value.Position.Normalized; return true; }
        /// <summary>Unity 屏幕像素坐标，左下角原点；使用 ScreenTarget 实际图像矩形。</summary>
        public bool TryGetJointScreenPosition(int index, HumanVisionCanonicalJointId joint, out Vector2 position) { position = default; return HasFreshResult && raw.TryGetScreenPosition(index, joint, NowUs, ScreenRect(), out position); }
        /// <summary>Unity 世界平面坐标，不包含人体真实距离。</summary>
        public bool TryGetJointWorldPosition(int index, HumanVisionCanonicalJointId joint, out Vector3 position) { position = default; return HasFreshResult && raw.TryGetWorldPosition(index, joint, NowUs, worldPlane != null ? worldPlane : transform, worldPlaneSize, out position); }
        /// <summary>参与者骨盆的世界平面位置；骨盆无效时失败。</summary>
        public bool TryGetUserWorldPosition(int index, out Vector3 position) => TryGetJointWorldPosition(index, HumanVisionCanonicalJointId.Pelvis, out position);
        /// <summary>两个关节在配置世界平面上的单位方向；重合或无效关节返回 false。</summary>
        public bool TryGetDirectionBetweenJoints(int index, HumanVisionCanonicalJointId first, HumanVisionCanonicalJointId second, out Vector3 direction) {
            direction = default; if (!TryGetJointWorldPosition(index, first, out var a) || !TryGetJointWorldPosition(index, second, out var b) || (b-a).sqrMagnitude < 1e-12f) return false;
            direction = (b-a).normalized; return true;
        }
        /// <summary>三个有效关节在配置世界平面上的夹角，单位度。</summary>
        public bool TryGetAngleAtJoint(int index, HumanVisionCanonicalJointId first, HumanVisionCanonicalJointId center, HumanVisionCanonicalJointId last, out float degrees) {
            degrees = 0; if (!TryGetJointWorldPosition(index, first, out var a) || !TryGetJointWorldPosition(index, center, out var b) || !TryGetJointWorldPosition(index, last, out var c) ||
                (a-b).sqrMagnitude < 1e-12f || (c-b).sqrMagnitude < 1e-12f) return false; degrees = Vector3.Angle(a-b, c-b); return true;
        }
        /// <summary>将 32 个关节及来源信息复制至用户缓冲区；无效点写 default。</summary>
        public bool CopySkeletonByIndex(int index, HumanVisionCanonicalJoint[] buffer, out HumanVisionSkeletonMetadata metadata) { metadata = default; return HasFreshResult && raw.CopySkeleton(index, NowUs, buffer, out metadata); }
        /// <summary>是否开启区域绑定。</summary>
        public bool RegionsEnabled => (active ?? options).Recognition.UseRegions;
        /// <summary>设置区域开关；启用前必须配置与人数匹配的有效区域。</summary>
        public bool TrySetRegionsEnabled(bool enabled) { var value = Configuration.Recognition; value.UseRegions = enabled; return TryApplyConfiguration(value); }
        /// <summary>设置区域草稿/运行区域，不隐式启用区域；复制输入数组。</summary>
        public bool TrySetRegions(Rect[] regions) { var value = Configuration.Recognition; value.Regions = regions == null ? null : (Rect[])regions.Clone(); return TryApplyConfiguration(value); }
        /// <summary>配置的区域数量。</summary>
        public int GetRegionCount() => (active ?? options).Recognition.Regions?.Length ?? 0;
        /// <summary>获取一个配置区域，返回值为 Rect 值类型。</summary>
        public bool TryGetRegion(int index, out Rect region) { region = default; var regions = (active ?? options).Recognition.Regions; if (regions == null || index < 0 || index >= regions.Length) return false; region = regions[index]; return true; }
        /// <summary>复制区域到用户缓冲；不足返回 0，成功返回复制数量。</summary>
        public int CopyRegions(Rect[] buffer) { var regions = (active ?? options).Recognition.Regions; if (regions == null || buffer == null || buffer.Length < regions.Length) return 0; Array.Copy(regions, buffer, regions.Length); return regions.Length; }
        /// <summary>占用状态是否已知；无当前结果返回 false，而不是确认无人。</summary>
        public bool TryGetRegionOccupancy(int index, out bool occupied) { occupied = false; return HasFreshResult && raw.TryGetOccupancy(index, NowUs, out occupied); }
        /// <summary>是否可确认区域有人；未知时返回 false，可用 TryGetRegionOccupancy 区分未知。</summary>
        public bool IsRegionOccupied(int index) => TryGetRegionOccupancy(index, out var occupied) && occupied;
        /// <summary>当前输入预览纹理，借用至输入切换/关闭；SDK 负责纹理生命周期。</summary>
        public Texture GetColorImageTex() => source?.CurrentTexture;
        /// <summary>当前实际图像宽度，尚无纹理为 0。</summary>
        public int GetColorImageWidth() => GetColorImageTex() != null ? GetColorImageTex().width : 0;
        /// <summary>当前实际图像高度，尚无纹理为 0。</summary>
        public int GetColorImageHeight() => GetColorImageTex() != null ? GetColorImageTex().height : 0;
        /// <summary>在识别启动前准备 Android 模型等级目录；不创建 Native 会话，不打开输入，不改变运行配置。</summary>
        public IEnumerator PrepareInputQualities(string runtimeRoot = "")
        {
            string profile = null;
            try { profile = QualityProfile(options); }
            catch (Exception e) { qualityCapabilities = HumanVisionSdkQualityCapabilities.Failed("", e.Message); }
            if (profile == null) yield break;
            if (profile != HumanVisionModelInputQualities.AdmittedRuntimeMode) yield break;
            string root = string.IsNullOrWhiteSpace(runtimeRoot) ? preparedRoot : runtimeRoot;
            string failure = "";
            if (string.IsNullOrEmpty(root)) yield return HumanVisionRuntimeData.Prepare(value => root = value, value => failure = value);
            qualityRoot = root; qualityCapabilities = HumanVisionSdkQualityCapabilities.Load(root, profile);
            if (string.IsNullOrEmpty(root) && !string.IsNullOrEmpty(failure))
                qualityCapabilities = HumanVisionSdkQualityCapabilities.Failed(profile, failure);
            // 只有当前安装资源校验成功，初始化才能复用该目录；错误不会覆盖正在运行的会话。
            if (!string.IsNullOrEmpty(root) && string.IsNullOrEmpty(qualityCapabilities.Error)) preparedRoot = root;
        }
        private static string QualityProfile(HumanVisionSdkOptions candidate) => Application.platform == RuntimePlatform.Android
            ? HumanVisionAndroidRuntimeSelection.ResolveProfile("auto")
            : new SharedRecognitionSettings { UseWindowsCpu = candidate.UseWindowsCpu }.RuntimeProfileFor(Application.platform);
        /// <summary>读取当前或草稿平台的能力说明；结果区分固定模型、准备中与校验失败。</summary>
        public HumanVisionSdkQualityCapabilities GetInputQualityCapabilities(HumanVisionSdkOptions requested = null)
        {
            HumanVisionSdkOptions candidate; string profile;
            try { candidate = requested ?? Configuration; profile = QualityProfile(candidate); }
            catch (Exception e) { return HumanVisionSdkQualityCapabilities.Failed("", e.Message); }
            string root = string.IsNullOrWhiteSpace(candidate.RuntimeRoot) ? preparedRoot : candidate.RuntimeRoot;
            if (profile != HumanVisionModelInputQualities.AdmittedRuntimeMode) return HumanVisionSdkQualityCapabilities.Load(root, profile);
            if (qualityCapabilities == null || qualityRoot != root || qualityCapabilities.RuntimeProfile != profile) {
                qualityRoot = root; qualityCapabilities = HumanVisionSdkQualityCapabilities.Load(root, profile);
            }
            return qualityCapabilities;
        }
        /// <summary>当前平台/构建真实可用的质量档位；未准备时先等待 PrepareInputQualities。</summary>
        public ModelInputQualityChoice[] GetAvailableInputQualities() => GetInputQualityCapabilities().Choices;
        private Rect ScreenRect()
        {
            if (screenTarget == null) return new Rect(0, 0, Screen.width, Screen.height);
            screenTarget.GetWorldCorners(corners);
            var canvas = screenTarget.GetComponentInParent<Canvas>();
            var camera = canvas != null && canvas.renderMode != RenderMode.ScreenSpaceOverlay ? canvas.worldCamera : null;
            var a = RectTransformUtility.WorldToScreenPoint(camera, corners[0]); var b = RectTransformUtility.WorldToScreenPoint(camera, corners[2]);
            return Rect.MinMaxRect(Mathf.Min(a.x,b.x), Mathf.Min(a.y,b.y), Mathf.Max(a.x,b.x), Mathf.Max(a.y,b.y));
        }
    }
}
