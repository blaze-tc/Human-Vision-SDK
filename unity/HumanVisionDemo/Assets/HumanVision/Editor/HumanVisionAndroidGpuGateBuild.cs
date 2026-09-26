using System;
using System.IO;
using System.Security.Cryptography;
using System.Collections.Generic;
using HumanVision.Demo;
using UnityEditor;
using UnityEditor.Android;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;

namespace HumanVision.Editor
{
    // Development-only gate entry points. The ordinary NCNN production validator stays strict.
    public static class HumanVisionAndroidGpuGateBuild
    {
        private static bool _active;
        private static bool _interactivePrepared;
        private static string _authorizedOutput;
        private const string PreparedManifestAsset = "Assets/StreamingAssets/HumanVisionPreparedGate/input-contract.json";
        private const string PreparedSceneBase = "Assets/HumanVision/GpuGateGenerated/HumanVisionPreparedGate.unity";
        private const string PreparedManifestCopyBase = "Assets/HumanVision/GpuGateGenerated/input-contract.json";
        public const string PreparedGateApplicationId = "com.blazetc.humanvision.preparedgate";
        private const string PreparedGateProductName = "HumanVision Prepared Detector Gate";
        private const string HumanVisionPluginAsset = "Assets/Plugins/Android/arm64-v8a/libhumanvision.so";
        private const string DetectorParamSha256 = "9a4a89da2de4298427255950e58943f670a9e18a6d69b720270070741978b2b3";
        private const string DetectorBinSha256 = "4329c052c86a53fd2f213b874f199a87755df6601e40bb7a45011b280dba42da";

        [Serializable]
        private sealed class PreparedManifest
        {
            public int schema_version;
            public string pack_id;
            public string profile_id;
            public string active_role;
            public string asset_root;
            public PreparedModel[] models;
        }

        [Serializable]
        private sealed class PreparedModel
        {
            public string role;
            public string param_path;
            public string bin_path;
            public string param_sha256;
            public string bin_sha256;
            public DetectorInputContract input_contract;
        }

        [Serializable]
        private sealed class DetectorInputContract
        {
            public string image_format;
            public string color_order;
            public string crop;
            public string resize_interpolation;
            public string tensor_dtype;
            public string input_blob;
            public int width;
            public int height;
            public int elempack;
            public int[] pad_rgb;
            public Normalization normalization;
        }

        [Serializable]
        private sealed class Normalization
        {
            public float[] mean;
            public float[] norm;
        }

        private sealed class InteractiveEditorState
        {
            private readonly SceneSetup[] _scenes = EditorSceneManager.GetSceneManagerSetup();
            private readonly string _runtimeMode = HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId;
            private readonly AndroidSdkVersions _minSdk = PlayerSettings.Android.minSdkVersion;
            private readonly AndroidArchitecture _architectures = PlayerSettings.Android.targetArchitectures;
            private readonly ScriptingImplementation _backend = PlayerSettings.GetScriptingBackend(BuildTargetGroup.Android);
            private readonly bool _automaticGraphics = PlayerSettings.GetUseDefaultGraphicsAPIs(BuildTarget.Android);
            private readonly GraphicsDeviceType[] _graphicsApis = PlayerSettings.GetGraphicsAPIs(BuildTarget.Android);
            private readonly string _defines = PlayerSettings.GetScriptingDefineSymbolsForGroup(BuildTargetGroup.Android);
            private readonly string _applicationId = PlayerSettings.GetApplicationIdentifier(BuildTargetGroup.Android);
            private readonly string _productName = PlayerSettings.productName;
            private readonly bool _pluginAnyPlatform;
            private readonly bool _pluginAndroid;
            private readonly string _pluginCpu;
            private readonly bool _pluginPreloaded;
            private readonly string _projectSettingsPath = Path.Combine(
                Directory.GetParent(Application.dataPath).FullName, "ProjectSettings/ProjectSettings.asset");
            private readonly string _serializedSettingsBefore;

            public InteractiveEditorState()
            {
                var plugin = PluginImporter.GetAtPath(HumanVisionPluginAsset) as PluginImporter;
                if (plugin == null) throw new InvalidOperationException("Gate native libhumanvision.so is missing");
                _pluginAnyPlatform = plugin.GetCompatibleWithAnyPlatform();
                _pluginAndroid = plugin.GetCompatibleWithPlatform(BuildTarget.Android);
                _pluginCpu = plugin.GetPlatformData("Android", "CPU") ?? string.Empty;
                _pluginPreloaded = plugin.isPreloaded;
                _serializedSettingsBefore = ReadSerializedAndroidSettings(_projectSettingsPath);
                Debug.Log("HV_PREPARED_GATE settings before: " + SettingsSummary());
            }

            public void Restore()
            {
                Exception failure = null;
                TryRestore(() => HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId = _runtimeMode, ref failure);
                TryRestore(() => PlayerSettings.Android.minSdkVersion = _minSdk, ref failure);
                TryRestore(() => PlayerSettings.Android.targetArchitectures = _architectures, ref failure);
                TryRestore(() => PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android, _backend), ref failure);
                TryRestore(() => PlayerSettings.SetGraphicsAPIs(BuildTarget.Android, _graphicsApis), ref failure);
                TryRestore(() => PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, _automaticGraphics), ref failure);
                TryRestore(() => PlayerSettings.SetScriptingDefineSymbolsForGroup(BuildTargetGroup.Android, _defines), ref failure);
                TryRestore(() => PlayerSettings.productName = _productName, ref failure);
                TryRestore(() => PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android, _applicationId), ref failure);
                TryRestore(() =>
                {
                    var plugin = PluginImporter.GetAtPath(HumanVisionPluginAsset) as PluginImporter;
                    if (plugin == null) throw new InvalidOperationException("Cannot restore gate plugin importer: libhumanvision.so disappeared.");
                    plugin.SetCompatibleWithAnyPlatform(_pluginAnyPlatform);
                    plugin.SetCompatibleWithPlatform(BuildTarget.Android, _pluginAndroid);
                    plugin.SetPlatformData("Android", "CPU", _pluginCpu);
                    plugin.isPreloaded = _pluginPreloaded;
                    plugin.SaveAndReimport();
                }, ref failure);
                TryRestore(() => EditorSceneManager.RestoreSceneManagerSetup(_scenes), ref failure);
                TryRestore(() =>
                {
                    if (!EditorApplication.ExecuteMenuItem("File/Save Project"))
                        throw new InvalidOperationException("Unity File/Save Project did not execute after gate settings restore.");
                }, ref failure);
                TryRestore(VerifyRestored, ref failure);
                TryRestore(() =>
                {
                    if (ReadSerializedAndroidSettings(_projectSettingsPath) != _serializedSettingsBefore)
                        throw new InvalidOperationException("PREPARED gate Android ProjectSettings.asset values were not restored on disk.");
                }, ref failure);
                Debug.Log("HV_PREPARED_GATE settings after: " + SettingsSummary());
                if (failure != null) throw new InvalidOperationException("PREPARED gate could not fully restore Unity Editor state.", failure);
            }

            private void VerifyRestored()
            {
                var plugin = PluginImporter.GetAtPath(HumanVisionPluginAsset) as PluginImporter;
                var graphics = PlayerSettings.GetGraphicsAPIs(BuildTarget.Android);
                bool graphicsMatch = graphics.Length == _graphicsApis.Length;
                for (int i = 0; graphicsMatch && i < graphics.Length; ++i)
                    graphicsMatch = graphics[i] == _graphicsApis[i];
                if (HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId != _runtimeMode ||
                    PlayerSettings.Android.minSdkVersion != _minSdk ||
                    PlayerSettings.Android.targetArchitectures != _architectures ||
                    PlayerSettings.GetScriptingBackend(BuildTargetGroup.Android) != _backend ||
                    PlayerSettings.GetUseDefaultGraphicsAPIs(BuildTarget.Android) != _automaticGraphics ||
                    !graphicsMatch ||
                    PlayerSettings.GetScriptingDefineSymbolsForGroup(BuildTargetGroup.Android) != _defines ||
                    PlayerSettings.productName != _productName ||
                    PlayerSettings.GetApplicationIdentifier(BuildTargetGroup.Android) != _applicationId ||
                    plugin == null || plugin.GetCompatibleWithAnyPlatform() != _pluginAnyPlatform ||
                    plugin.GetCompatibleWithPlatform(BuildTarget.Android) != _pluginAndroid ||
                    (plugin.GetPlatformData("Android", "CPU") ?? string.Empty) != _pluginCpu ||
                    plugin.isPreloaded != _pluginPreloaded)
                    throw new InvalidOperationException("PREPARED gate Android settings or native plugin importer were not restored.");
            }

            private static string SettingsSummary()
            {
                var plugin = PluginImporter.GetAtPath(HumanVisionPluginAsset) as PluginImporter;
                return "mode=" + HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId +
                    " minSdk=" + PlayerSettings.Android.minSdkVersion +
                    " architectures=" + PlayerSettings.Android.targetArchitectures +
                    " backend=" + PlayerSettings.GetScriptingBackend(BuildTargetGroup.Android) +
                    " autoGraphics=" + PlayerSettings.GetUseDefaultGraphicsAPIs(BuildTarget.Android) +
                    " graphics=" + string.Join(",", Array.ConvertAll(PlayerSettings.GetGraphicsAPIs(BuildTarget.Android), x => x.ToString())) +
                    " defines=" + PlayerSettings.GetScriptingDefineSymbolsForGroup(BuildTargetGroup.Android) +
                    " applicationId=" + PlayerSettings.GetApplicationIdentifier(BuildTargetGroup.Android) +
                    " productName=" + PlayerSettings.productName +
                    " pluginAny=" + (plugin == null ? "missing" : plugin.GetCompatibleWithAnyPlatform().ToString()) +
                    " pluginAndroid=" + (plugin == null ? "missing" : plugin.GetCompatibleWithPlatform(BuildTarget.Android).ToString()) +
                    " pluginCpu=" + (plugin == null ? "missing" : plugin.GetPlatformData("Android", "CPU")) +
                    " pluginPreloaded=" + (plugin == null ? "missing" : plugin.isPreloaded.ToString());
            }

            private static void TryRestore(Action action, ref Exception firstFailure)
            {
                try { action(); }
                catch (Exception ex)
                {
                    Debug.LogException(ex);
                    if (firstFailure == null) firstFailure = ex;
                }
            }
        }

        public static string ReadSerializedAndroidSettings(string path)
        {
            var fields = new Dictionary<string, string>();
            string section = string.Empty;
            bool androidGraphics = false;
            foreach (string line in File.ReadAllLines(path))
            {
                if (line.StartsWith("  ") && !line.StartsWith("    ") && !line.StartsWith("  - "))
                {
                    int colon = line.IndexOf(':');
                    section = colon < 0 ? string.Empty : line.Substring(2, colon - 2);
                }
                if (line.StartsWith("  productName:")) fields["productName"] = line;
                if (line.StartsWith("  AndroidMinSdkVersion:")) fields["minSdk"] = line;
                if (line.StartsWith("  AndroidTargetArchitectures:")) fields["architectures"] = line;
                if (section == "applicationIdentifier" && line.StartsWith("    Android:")) fields["applicationId"] = line;
                if (section == "scriptingDefineSymbols" && line.StartsWith("    Android:")) fields["defines"] = line;
                if (section == "scriptingBackend" && line.StartsWith("    Android:")) fields["backend"] = line;
                if (section == "m_BuildTargetGraphicsAPIs")
                {
                    if (line.StartsWith("  - m_BuildTarget:"))
                        androidGraphics = line.Trim() == "- m_BuildTarget: AndroidPlayer";
                    if (androidGraphics && line.StartsWith("    m_APIs:")) fields["graphicsApis"] = line;
                    if (androidGraphics && line.StartsWith("    m_Automatic:")) fields["automaticGraphics"] = line;
                }
            }
            string[] required = { "productName", "applicationId", "minSdk", "architectures",
                "defines", "backend", "graphicsApis", "automaticGraphics" };
            var values = new string[required.Length];
            for (int i = 0; i < required.Length; ++i)
                if (!fields.TryGetValue(required[i], out values[i]))
                    throw new InvalidOperationException("ProjectSettings.asset lacks Android field " + required[i] + ": " + path);
            return string.Join("\n", values);
        }
        private static bool HasGateFlag()
        {
            return Array.IndexOf(Environment.GetCommandLineArgs(), "-humanvisionGpuGate") >= 0 ||
                Array.IndexOf(Environment.GetCommandLineArgs(), "-humanvisionPreparedGate") >= 0;
        }
        private static bool IsPrepared() =>
            Array.IndexOf(Environment.GetCommandLineArgs(), "-humanvisionPreparedGate") >= 0;
        public static bool IsAuthorizedGateBuild(BuildReport report)
        {
            if (!_active || report == null || report.summary.platform != BuildTarget.Android ||
                !report.summary.options.HasFlag(BuildOptions.Development)) return false;
            if (_interactivePrepared)
                return !Application.isBatchMode &&
                    string.Equals(Path.GetFullPath(report.summary.outputPath), _authorizedOutput,
                        StringComparison.OrdinalIgnoreCase);
            return Application.isBatchMode && HasGateFlag() &&
                report.summary.options.HasFlag(BuildOptions.Development) &&
                report.summary.outputPath.EndsWith(IsPrepared() ? "humanvision-prepared-gate.apk" :
                    "humanvision-gpu-bridge-gate.apk", StringComparison.OrdinalIgnoreCase);
        }

        [MenuItem("HumanVision/Android/Build PREPARED Detector Gate (Development APK)")]
        public static void BuildPreparedInOpenProject()
        {
            if (Application.isBatchMode || EditorApplication.isPlayingOrWillChangePlaymode)
                throw new InvalidOperationException("PREPARED detector gate requires an interactive Unity Editor in Edit Mode.");

            string projectRoot = Directory.GetParent(Application.dataPath).FullName;
            string manifestPath = Path.Combine(projectRoot, PreparedManifestAsset);
            string detectorDirectory = Path.Combine(projectRoot,
                "Assets/StreamingAssets/HumanVisionPreparedGate/detector");
            ValidatePreparedInputs(manifestPath, detectorDirectory);
            if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
            for (int i = 0; i < SceneManager.sceneCount; ++i)
            {
                var scene = SceneManager.GetSceneAt(i);
                if (string.IsNullOrEmpty(scene.path) || scene.isDirty)
                    throw new InvalidOperationException("Save all open scenes before building the PREPARED gate.");
            }
            string output = Path.GetFullPath(Path.Combine(projectRoot, "Builds/humanvision-prepared-gate.apk"));
            string scenePath = FindAvailableGateScenePath(PreparedSceneBase,
                candidate => File.Exists(Path.Combine(projectRoot, candidate)) ||
                    File.Exists(Path.Combine(projectRoot, candidate) + ".meta") ||
                    AssetDatabase.LoadMainAssetAtPath(candidate) != null);
            string manifestCopyPath = FindAvailableGeneratedAssetPath(PreparedManifestCopyBase,
                candidate => File.Exists(Path.Combine(projectRoot, candidate)) ||
                    File.Exists(Path.Combine(projectRoot, candidate) + ".meta") ||
                    AssetDatabase.LoadMainAssetAtPath(candidate) != null);
            var state = new InteractiveEditorState();
            string sceneOwnerMarker = "HV_PREPARED_GATE_OWNER:" + Guid.NewGuid().ToString("N");
            try
            {
                if (File.Exists(Path.Combine(projectRoot, scenePath)) ||
                    File.Exists(Path.Combine(projectRoot, scenePath) + ".meta"))
                    throw new InvalidOperationException("Generated gate scene path became occupied: " + scenePath);
                if (File.Exists(Path.Combine(projectRoot, manifestCopyPath)) ||
                    File.Exists(Path.Combine(projectRoot, manifestCopyPath) + ".meta"))
                    throw new InvalidOperationException("Generated gate manifest path became occupied: " + manifestCopyPath);
                Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(projectRoot, manifestCopyPath)));
                using (var destination = new FileStream(Path.Combine(projectRoot, manifestCopyPath),
                    FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    using (var source = File.OpenRead(manifestPath)) source.CopyTo(destination);
                }
                AssetDatabase.Refresh();
                var fixture = AssetDatabase.LoadAssetAtPath<TextAsset>(manifestCopyPath);
                if (fixture == null)
                    throw new InvalidOperationException("PREPARED gate manifest copy did not import as a TextAsset: " + manifestCopyPath);
                Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(projectRoot, scenePath)));
                using (var reservation = new FileStream(Path.Combine(projectRoot, scenePath),
                    FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    using (var writer = new StreamWriter(reservation)) writer.Write(sceneOwnerMarker);
                }
                ConfigureGateSceneAndPlayer(scenePath, fixture, true, sceneOwnerMarker);
                SetInteractivePreparedPackageIdentity();
                BuildGateApk(scenePath, output, true);
                Debug.Log("HV_PREPARED_GATE retained generated scene=" + scenePath +
                    " manifest=" + manifestCopyPath);
            }
            finally { state.Restore(); }
        }

        public static void SetInteractivePreparedPackageIdentity()
        {
            PlayerSettings.productName = PreparedGateProductName;
            PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android, PreparedGateApplicationId);
        }

        public static string FindAvailableGateScenePath(string preferredPath, Func<string, bool> exists)
        {
            if (Path.GetExtension(preferredPath) != ".unity")
                throw new ArgumentException("Generated gate scene must have a .unity extension.");
            return FindAvailableGeneratedAssetPath(preferredPath, exists);
        }

        public static string FindAvailableGeneratedAssetPath(string preferredPath, Func<string, bool> exists)
        {
            if (string.IsNullOrEmpty(preferredPath) || exists == null)
                throw new ArgumentException("Generated gate scene path and occupancy check are required.");
            string directory = Path.GetDirectoryName(preferredPath).Replace('\\', '/');
            if (directory != "Assets/HumanVision/GpuGateGenerated")
                throw new InvalidOperationException("Gate scene must be under Assets/HumanVision/GpuGateGenerated.");
            string stem = Path.GetFileNameWithoutExtension(preferredPath);
            string extension = Path.GetExtension(preferredPath);
            if (extension != ".unity" && extension != ".json")
                throw new InvalidOperationException("Generated gate asset extension must be .unity or .json.");
            for (int i = 0; i < 10000; ++i)
            {
                string candidate = directory + "/" + stem + (i == 0 ? "" : "-" + i) + extension;
                if (!exists(candidate)) return candidate;
            }
            throw new InvalidOperationException("No unoccupied generated gate scene path is available.");
        }

        public static void ValidatePreparedInputs(string manifestPath, string detectorDirectory)
        {
            if (!File.Exists(manifestPath)) throw new InvalidOperationException("PREPARED gate input-contract.json is missing: " + manifestPath);
            PreparedManifest manifest;
            try { manifest = JsonUtility.FromJson<PreparedManifest>(File.ReadAllText(manifestPath)); }
            catch (Exception ex) { throw new InvalidOperationException("PREPARED gate manifest is invalid JSON.", ex); }
            if (manifest == null || manifest.schema_version != 2 ||
                manifest.pack_id != "precision-t-26-ncnn-fp16" ||
                manifest.profile_id != "android-ncnn-vulkan" ||
                manifest.active_role != "detector" || manifest.asset_root != "__ASSET_ROOT__" ||
                manifest.models == null || manifest.models.Length != 2)
                throw new InvalidOperationException("PREPARED gate manifest does not match the pinned detector contract.");
            PreparedModel detector = null;
            int bodyCount = 0;
            foreach (var model in manifest.models)
            {
                if (model != null && model.role == "detector")
                {
                    if (detector != null) throw new InvalidOperationException("PREPARED gate manifest has duplicate detector entries.");
                    detector = model;
                }
                else if (model != null && model.role == "body") ++bodyCount;
            }
            if (bodyCount != 1)
                throw new InvalidOperationException("PREPARED gate manifest must contain exactly one detector and one body declaration.");
            if (detector == null || detector.param_path != "detector/model.param" ||
                detector.bin_path != "detector/model.bin" ||
                detector.param_sha256 != DetectorParamSha256 || detector.bin_sha256 != DetectorBinSha256 ||
                !IsPinnedInput(detector.input_contract))
                throw new InvalidOperationException("PREPARED gate detector model or input contract does not match the pinned reference.");
            VerifySha256(Path.Combine(detectorDirectory, "model.param"), DetectorParamSha256);
            VerifySha256(Path.Combine(detectorDirectory, "model.bin"), DetectorBinSha256);
        }

        private static bool IsPinnedInput(DetectorInputContract input)
        {
            if (input == null || input.image_format != "rgba8-unorm" || input.color_order != "rgb" ||
                input.crop != "letterbox" || input.resize_interpolation != "bilinear" ||
                input.tensor_dtype != "fp16" || input.input_blob != "in0" ||
                input.width != 320 || input.height != 320 || input.elempack != 1 ||
                input.pad_rgb == null || input.pad_rgb.Length != 3 ||
                input.normalization == null || input.normalization.mean == null ||
                input.normalization.norm == null || input.normalization.mean.Length != 3 ||
                input.normalization.norm.Length != 3) return false;
            for (int i = 0; i < 3; ++i)
            {
                if (input.pad_rgb[i] != 114 ||
                    Math.Abs(input.normalization.mean[i] - new[] { 123.675f, 116.28f, 103.53f }[i]) > 0.0001f ||
                    Math.Abs(input.normalization.norm[i] - new[] { 0.0171247538f, 0.0175070028f, 0.0174291939f }[i]) > 0.000001f)
                    return false;
            }
            return true;
        }

        private static void VerifySha256(string path, string expected)
        {
            if (!File.Exists(path)) throw new InvalidOperationException("PREPARED gate detector file is missing: " + path);
            using (var stream = File.OpenRead(path))
            using (var sha = SHA256.Create())
            {
                string actual = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
                if (actual != expected)
                    throw new InvalidOperationException("PREPARED gate detector SHA-256 mismatch: " + path);
            }
        }
        public static void Build()
        {
            if (!Application.isBatchMode || !HasGateFlag())
                throw new InvalidOperationException("GPU gate build requires the isolated test script; batch=" +
                    Application.isBatchMode + " marker=" + HasGateFlag());
            string input = Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "input-contract.json"));
            if (string.IsNullOrEmpty(input) || !File.Exists(input) ||
                !Path.GetFullPath(input).Contains(IsPrepared() ? "android-prepared-gate-runtime" :
                    "android-gpu-gate-runtime"))
                throw new InvalidOperationException("Missing generated test-only input contract under out/android-gpu-gate-runtime");
            string assetPath = "Assets/HumanVision/GpuGateGenerated/input-contract.json";
            Directory.CreateDirectory(Path.GetDirectoryName(assetPath));
            File.Copy(input, assetPath, true);
            AssetDatabase.Refresh();
            var fixture = AssetDatabase.LoadAssetAtPath<TextAsset>(assetPath);
            if (fixture == null) throw new InvalidOperationException("GPU gate input contract import failed");
            string scenePath = "Assets/Scenes/HumanVisionCameraDemo.unity";
            ConfigureGateSceneAndPlayer(scenePath, fixture, IsPrepared());
            string output = Path.GetFullPath(IsPrepared() ? "../humanvision-prepared-gate.apk" :
                "../humanvision-gpu-bridge-gate.apk");
            BuildGateApk(scenePath, output, false);
        }

        private static void ConfigureGateSceneAndPlayer(string scenePath, TextAsset fixture, bool prepared,
            string sceneOwnerMarker = null)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(scenePath));
            if (sceneOwnerMarker != null && File.ReadAllText(scenePath) != sceneOwnerMarker)
                throw new InvalidOperationException("Generated gate scene reservation was changed before save: " + scenePath);
            HumanVisionCameraDemoBuilder.CreateGateScene(scenePath);
            foreach (var behaviour in UnityEngine.Object.FindObjectsOfType<MonoBehaviour>()) behaviour.enabled = false;
            var gate = new GameObject("Development GPU bridge gate").AddComponent<HumanVisionAndroidGpuGate>();
            gate.inputContract = fixture;
            gate.preparedDetectorGate = prepared;
            EditorSceneManager.SaveScene(EditorSceneManager.GetActiveScene(), scenePath);
            HumanVisionAndroidRuntimeSettings.instance.RuntimeModeId = "android-ncnn-vulkan";
            PlayerSettings.Android.minSdkVersion = AndroidSdkVersions.AndroidApiLevel26;
            PlayerSettings.Android.targetArchitectures = AndroidArchitecture.ARM64;
            PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android, ScriptingImplementation.IL2CPP);
            PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, false);
            PlayerSettings.SetGraphicsAPIs(BuildTarget.Android, new[] { GraphicsDeviceType.Vulkan });
            PlayerSettings.SetScriptingDefineSymbolsForGroup(BuildTargetGroup.Android, "HUMANVISION_GPU_GATE");
            var plugin = PluginImporter.GetAtPath("Assets/Plugins/Android/arm64-v8a/libhumanvision.so") as PluginImporter;
            if (plugin == null) throw new InvalidOperationException("Gate native libhumanvision.so is missing");
            plugin.SetCompatibleWithAnyPlatform(false);
            plugin.SetCompatibleWithPlatform(BuildTarget.Android, true);
            plugin.SetPlatformData("Android", "CPU", "ARM64");
            // UnityPluginLoad must install Vulkan interception before Unity creates VkDevice.
            plugin.isPreloaded = true;
            plugin.SaveAndReimport();
            plugin = PluginImporter.GetAtPath("Assets/Plugins/Android/arm64-v8a/libhumanvision.so") as PluginImporter;
            if (plugin == null || !plugin.isPreloaded)
                throw new InvalidOperationException("Gate libhumanvision.so must be preloaded before Vulkan initialization");
        }

        private static void BuildGateApk(string scenePath, string output, bool interactivePrepared)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(output));
            _active = true;
            _interactivePrepared = interactivePrepared;
            _authorizedOutput = Path.GetFullPath(output);
            try
            {
                var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions {
                    scenes = new[] { scenePath }, locationPathName = output,
                    target = BuildTarget.Android, options = BuildOptions.Development
                });
                if (report.summary.result != BuildResult.Succeeded)
                    throw new InvalidOperationException("GPU gate APK failed: " + report.summary.result +
                        " errors=" + report.summary.totalErrors);
                Debug.Log("HV_GPU_GATE_APK=" + output + " bytes=" + report.summary.totalSize);
            }
            finally
            {
                _active = false;
                _interactivePrepared = false;
                _authorizedOutput = null;
            }
        }
    }
}
