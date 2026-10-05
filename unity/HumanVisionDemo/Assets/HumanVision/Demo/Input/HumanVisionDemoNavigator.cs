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
        public SharedRecognitionSettings ActiveShared { get; private set; }
        public string QualityAvailability { get; private set; } = "Model Input Quality availability is preparing.";
        public HumanVisionModeSettingsPanel ModePanel { get; set; }
        public HumanVisionSharedSettingsPanel SharedPanel { get; set; }
        private HumanVisionSettingsStore store;
        private HumanVisionCameraManager regionFacade;
        private string runtimeRoot;
        private bool initializing, leaving;
        private int operation;
        private long regionRevision;
        private void Start()
        {
            store = new HumanVisionSettingsStore();
            try { Shared = store.LoadShared(); }
            catch (Exception error) { Shared = new SharedRecognitionSettings(); Status = error.Message; }
            try { Mode = store.LoadMode(Kind); }
            catch (Exception error) { Mode = new DemoModeSettings(); Status = error.Message; }
            ActiveShared = Shared.Clone();
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
            if (!initializing && !leaving && Manager.IsInitialized) { Bridge.BindUnifiedSource(source); regionFacade.BindUnifiedInput(regionFacade.Settings, regionRevision); }
        }
        public void OpenSource()
        {
            try { ModePanel?.ReadInto(Mode); Mode.Validate(); Overlay.ConfigureStyle(Mode.LineWidth, Mode.PointDiameter); Input.Open(Mode.ToSourceSettings(Kind)); }
            catch (Exception error) { Status = error.Message; }
        }
        private SharedRecognitionSettings ReadDraft()
        {
            var candidate = Shared.Clone();
            // Region handles edit the active facade's rectangles. Keep those edits when
            // the saved draft still has the same people count; pending resized drafts stay separate.
            if (ActiveShared != null && Shared.MaxBodies == ActiveShared.MaxBodies && regionFacade?.Settings != null)
                candidate.Regions = (Rect[])regionFacade.Settings.regions.Clone();
            SharedPanel?.ReadInto(candidate); candidate.Validate(); return candidate;
        }
        private string BaseProfile(SharedRecognitionSettings candidate)
        {
            return Application.platform == RuntimePlatform.Android
                ? HumanVisionAndroidRuntimeSelection.ResolveProfile("auto")
                : candidate.RuntimeProfileFor(Application.platform);
        }
        private AnalysisContract Preflight(SharedRecognitionSettings candidate)
        {
            if (string.IsNullOrEmpty(runtimeRoot)) throw new InvalidOperationException("Runtime data is unavailable; retry recognition preparation before Apply.");
            string baseProfile = BaseProfile(candidate);
            var choices = baseProfile == HumanVisionModelInputQualities.AdmittedRuntimeMode
                ? HumanVisionModelInputQualities.Load(runtimeRoot).ChoicesForMode(baseProfile) : Array.Empty<ModelInputQualityChoice>();
            QualityAvailability = choices.Length == 0 ? "Model Input Quality unavailable for this profile; actual contract remains fixed." : "Model Input Quality available: High / Medium / Low. Apply activates the draft.";
            SharedPanel?.RefreshQualityChoices(choices);
            return candidate.ResolveContract(runtimeRoot, baseProfile);
        }
        private IEnumerator PrepareRecognition()
        {
            if (initializing || leaving) yield break;
            initializing = true; int token = ++operation;
            string prepared = null, preparationError = null;
            yield return HumanVisionRuntimeData.Prepare(value => prepared = value, error => preparationError = error);
            if (token != operation || leaving) yield break;
            if (string.IsNullOrEmpty(prepared)) {
                Status = preparationError ?? "Runtime data preparation failed; independent preview remains available.";
                initializing = false; yield break;
            }
            runtimeRoot = prepared;
            SharedRecognitionSettings candidate = null; AnalysisContract selected = null;
            try { candidate = ReadDraft(); selected = Preflight(candidate); }
            catch (Exception error) { Status = error.Message; }
            if (selected != null) yield return InitializeRecognition(candidate, selected, token);
            if (token == operation) initializing = false;
        }
        private IEnumerator InitializeRecognition(SharedRecognitionSettings candidate, AnalysisContract selected, int token)
        {
            // All requested catalog/Profile/ModelPack validation completed before detach/Shutdown.
            CloseRecognitionSource();
            double deadline = Time.realtimeSinceStartupAsDouble + 5;
            while (Bridge.UnifiedRetirementPending) {
                if (token != operation || leaving) yield break;
                if (Time.realtimeSinceStartupAsDouble >= deadline) {
                    Status = "Recognition source-copy retirement is pending; preview continues. Retry after the copies retire.";
                    yield break;
                }
                yield return null;
            }
            if (token != operation || leaving) yield break;
            Contract = null;
            if (!Manager.TryInitialize(new HumanVisionConfig { MaxBodies = candidate.MaxBodies, RuntimeRoot = runtimeRoot, Profile = selected.ProfileId })) {
                Status = Manager.LastError; yield break;
            }
            try {
                if (Manager.ActiveRuntimeProfile != selected.ProfileId) throw new InvalidOperationException("Initialized runtime profile differs from the validated requested profile.");
                selected.ApplyTo(candidate); selected.Validate(candidate);
                Shared = candidate;
                if (ActiveShared == null) ActiveShared = candidate.Clone();
                ActiveShared.MaxBodies = candidate.MaxBodies; ActiveShared.UseRegions = candidate.UseRegions; ActiveShared.Regions = candidate.Regions;
                ActiveShared.InputQuality = candidate.InputQuality; ActiveShared.UseWindowsCpu = candidate.UseWindowsCpu; selected.ApplyTo(ActiveShared);
                Contract = selected; SyncRegions(); ApplyRegions();
                // Rebind the current independently playing source, including a reopen during retirement.
                initializing = false;
                if (Input.Source != null) BindRecognition(Input.Source);
                Status = "Recognition active: " + Contract.ProfileId + " / " + Contract.PoseWidth + "x" + Contract.PoseHeight + ". " + QualityAvailability;
            } catch (Exception error) { Manager.Shutdown(); Contract = null; Status = error.Message; }
        }
        public void ApplyShared()
        {
            if (initializing || leaving) { Status = "Recognition is initializing; wait before applying settings or switching scenes."; return; }
            try {
                var candidate = ReadDraft();
                var selected = Preflight(candidate);
                if (!Manager.IsInitialized || Contract == null || Manager.ActiveRuntimeProfile != selected.ProfileId || selected.ModelPackId != Contract.ModelPackId) {
                    initializing = true; int token = ++operation;
                    StartCoroutine(ReinitializeRecognition(candidate, selected, token)); return;
                }
                selected.ApplyTo(candidate); selected.Validate(candidate);
                Shared = candidate; ActiveShared.MaxBodies = candidate.MaxBodies; ActiveShared.UseRegions = candidate.UseRegions; ActiveShared.Regions = candidate.Regions;
                ActiveShared.InputQuality = candidate.InputQuality; SyncRegions(); ApplyRegions();
                Status = "Shared people and numbered regions applied. Active: " + Contract.ProfileId + " / " + Contract.PoseWidth + "x" + Contract.PoseHeight;
            } catch (Exception error) { Status = error.Message; }
        }
        private IEnumerator ReinitializeRecognition(SharedRecognitionSettings candidate, AnalysisContract selected, int token)
        {
            yield return InitializeRecognition(candidate, selected, token);
            if (token == operation) initializing = false;
        }
        private void ApplyRegions()
        {
            if (!Manager.IsInitialized) return;
            if (!Manager.TrySetMaxBodies(ActiveShared.MaxBodies) || !Manager.TrySetRegions(ActiveShared.UseRegions ? ActiveShared.Regions : Array.Empty<Rect>(), ++regionRevision))
                throw new InvalidOperationException(Manager.LastError);
            regionFacade.BindUnifiedInput(regionFacade.Settings, regionRevision);
        }
        private void SyncRegions()
        {
            var settings = ActiveShared ?? Shared;
            regionFacade.Settings = new HumanVisionCameraSettings { people = settings.MaxBodies, regions = settings.Regions, useRegions = settings.UseRegions };
        }
        public void Save()
        {
            TrySave();
        }
        private bool TrySave()
        {
            try {
                var candidate = ReadDraft(); ModePanel?.ReadInto(Mode);
                store.SaveShared(candidate); store.SaveMode(Kind, Mode); Shared = candidate;
                Status = "Saved recognition draft for next startup and " + Kind + " source settings. Apply activates recognition changes."; return true;
            } catch (Exception error) { Status = error.Message; return false; }
        }
        public void SwitchTo(InputKind kind)
        {
            if (!Enum.IsDefined(typeof(InputKind), kind)) throw new ArgumentOutOfRangeException(nameof(kind));
            if (initializing || leaving) { Status = "Recognition is initializing; wait before switching scenes."; return; }
            if (!TrySave()) return;
            leaving = true; ++operation;
            Input.Close();
            SceneManager.LoadScene(kind == InputKind.WebCamera ? "HumanVisionCameraDemo" : kind == InputKind.Video ? "HumanVisionVideoDemo" : "HumanVisionRtspDemo");
        }
        public void RetryRecognition() { if (!initializing && !leaving) StartCoroutine(PrepareRecognition()); else Status = "Recognition is initializing; wait before retrying."; }
        private void OnDestroy()
        {
            leaving = true; ++operation;
            if (Bridge != null && regionFacade != null) CloseRecognitionSource();
            if (Input != null) { Input.SourceClosing -= CloseRecognitionSource; Input.SourceOpened -= BindRecognition; Input.Close(); }
        }
    }
}
