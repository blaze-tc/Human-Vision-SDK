using System;
using System.Collections;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace HumanVision.Demo
{
    public sealed class HumanVisionDemoNavigator : MonoBehaviour
    {
        public InputKind Kind;
        public GameObject SharedSettingsPrefab;
        public SharedRecognitionSettings Shared { get; private set; }
        public DemoModeSettings Mode { get; private set; }
        public HumanVisionManager Manager { get; private set; }
        public VideoPlayerFrameSource Bridge { get; private set; }
        public HumanVisionRegionSettingsUI RegionEditor { get; private set; }
        public InputPreviewControls Input { get; private set; }
        public HumanVisionOverlay Overlay { get; private set; }
        public string Status { get; private set; } = "Preparing recognition. Independent preview is available.";
        public AnalysisContract Contract { get; private set; }
        public HumanVisionModeSettingsPanel ModePanel { get; set; }
        public HumanVisionSharedSettingsPanel SharedPanel { get; set; }
        private HumanVisionSettingsStore store;
        private HumanVisionCameraManager regionFacade;
        private string runtimeRoot;
        private bool initializing;
        private long regionRevision;
        private void Start()
        {
            store = new HumanVisionSettingsStore();
            try { Shared = store.LoadShared(); Mode = store.LoadMode(Kind); }
            catch (Exception error) { Shared = new SharedRecognitionSettings(); Mode = new DemoModeSettings(); Status = error.Message; }
            Manager = GetComponent<HumanVisionManager>(); Bridge = GetComponent<VideoPlayerFrameSource>();
            regionFacade = GetComponent<HumanVisionCameraManager>(); regionFacade.enabled = false;
            RegionEditor = GetComponent<HumanVisionRegionSettingsUI>(); RegionEditor.manager = regionFacade;
            Input = GetComponent<InputPreviewControls>();
            HumanVisionUnifiedDemoCanvas.Build(this, out var preview, out var overlay);
            Overlay = overlay; Input.Preview = preview; RegionEditor.preview = preview;
            Bridge.Configure(Manager, preview, preview.GetComponent<UnityEngine.UI.AspectRatioFitter>());
            Overlay.Configure(Manager, Bridge); SyncRegions();
            Input.SourceClosing += CloseRecognitionSource;
            Input.SourceOpened += BindRecognition;
            OpenSource(); StartCoroutine(PrepareRecognition());
        }
        private void CloseRecognitionSource() { regionFacade.UnbindUnifiedInput(); Bridge.DetachUnifiedSource(); }
        private void BindRecognition(IHumanVisionFrameSource source) {
            if (Manager.IsInitialized) { Bridge.BindUnifiedSource(source); regionFacade.BindUnifiedInput(regionFacade.Settings, regionRevision); }
        }
        public void OpenSource()
        {
            try { ModePanel?.ReadInto(Mode); Mode.Validate(); Overlay.ConfigureStyle(Mode.LineWidth, Mode.PointDiameter); Input.Open(Mode.ToSourceSettings(Kind)); }
            catch (Exception error) { Status = error.Message; }
        }
        private IEnumerator PrepareRecognition()
        {
            initializing = true;
            yield return HumanVisionRuntimeData.Prepare(value => runtimeRoot = value, error => Status = error);
            if (!string.IsNullOrEmpty(runtimeRoot)) yield return InitializeRecognition();
            initializing = false;
        }
        private IEnumerator InitializeRecognition()
        {
            CloseRecognitionSource();
            while (Bridge.UnifiedRetirementPending) yield return null;
            // Android resolves only baked Project Settings metadata; PC uses the shared explicit choice.
            string profile = Shared.RuntimeProfileFor(Application.platform);
            if (!Manager.TryInitialize(new HumanVisionConfig { MaxBodies = Shared.MaxBodies, RuntimeRoot = runtimeRoot, Profile = profile })) {
                Status = Manager.LastError; yield break;
            }
            try {
                Contract = AnalysisContract.Load(runtimeRoot, Manager.ActiveRuntimeProfile, Shared.MaxBodies);
                Contract.ApplyTo(Shared); Contract.Validate(Shared); ApplyRegions();
                if (Input.Source != null) BindRecognition(Input.Source);
                Status = "Recognition ready. Analysis contract: " + Contract.ModelPackId;
            } catch (Exception error) { Manager.Shutdown(); Status = error.Message; }
        }
        public void ApplyShared()
        {
            if (initializing) { Status = "Recognition is initializing; wait before applying settings."; return; }
            try {
                SharedPanel?.ReadInto(Shared); Shared.Validate(); SyncRegions();
                if (!Manager.IsInitialized || (Application.platform != RuntimePlatform.Android &&
                    Manager.ActiveRuntimeProfile != Shared.RuntimeProfileFor(Application.platform))) {
                    StartCoroutine(ReinitializeRecognition()); return;
                }
                if (Contract != null) {
                    var selected = AnalysisContract.Load(runtimeRoot, Manager.ActiveRuntimeProfile, Shared.MaxBodies);
                    if (selected.ModelPackId != Contract.ModelPackId) { StartCoroutine(ReinitializeRecognition()); return; }
                    Contract.Validate(Shared);
                }
                ApplyRegions(); Status = "Shared people and numbered regions applied.";
            } catch (Exception error) { Status = error.Message; }
        }
        private IEnumerator ReinitializeRecognition()
        {
            initializing = true; yield return InitializeRecognition(); initializing = false;
        }
        private void ApplyRegions()
        {
            if (!Manager.IsInitialized) return;
            if (!Manager.TrySetMaxBodies(Shared.MaxBodies) || !Manager.TrySetRegions(Shared.UseRegions ? Shared.Regions : Array.Empty<Rect>(), ++regionRevision))
                throw new InvalidOperationException(Manager.LastError);
            regionFacade.BindUnifiedInput(regionFacade.Settings, regionRevision);
        }
        private void SyncRegions()
        {
            regionFacade.Settings = new HumanVisionCameraSettings { people = Shared.MaxBodies, regions = Shared.Regions, useRegions = Shared.UseRegions };
        }
        public void Save()
        {
            TrySave();
        }
        private bool TrySave()
        {
            try {
                SharedPanel?.ReadInto(Shared); ModePanel?.ReadInto(Mode);
                SyncRegions();
                if (Contract != null) Contract.Validate(Shared);
                store.SaveShared(Shared); store.SaveMode(Kind, Mode); Status = "Saved shared and " + Kind + " settings."; return true;
            } catch (Exception error) { Status = error.Message; return false; }
        }
        public void SwitchTo(InputKind kind)
        {
            if (!Enum.IsDefined(typeof(InputKind), kind)) throw new ArgumentOutOfRangeException(nameof(kind));
            if (!TrySave()) return;
            Input.Close();
            SceneManager.LoadScene(kind == InputKind.WebCamera ? "HumanVisionCameraDemo" : kind == InputKind.Video ? "HumanVisionVideoDemo" : "HumanVisionRtspDemo");
        }
        public void RetryRecognition() { if (!initializing) StartCoroutine(PrepareRecognition()); }
        private void OnDestroy()
        {
            if (Bridge != null) CloseRecognitionSource();
            if (Input != null) { Input.SourceClosing -= CloseRecognitionSource; Input.SourceOpened -= BindRecognition; Input.Close(); }
        }
    }
}
