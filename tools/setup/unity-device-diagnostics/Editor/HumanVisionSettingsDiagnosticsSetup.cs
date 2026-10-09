using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading;
using System.Security.Cryptography;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using HumanVision.TestProject.Diagnostics;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.UI;

/// <summary>本测试项目专用安装器/验证入口；使用 Editor API 保存普通场景和 Prefab，不手工修改 YAML。</summary>
public static class HumanVisionSettingsDiagnosticsSetup
{
    private const string ScenePath = "Assets/HumanVisionSettingsDemo/HumanVisionSettingsDemo.unity";
    private const string PrefabPath = "Assets/HumanVisionSettingsDemo/HumanVisionSettingsDemo.prefab";
    private static string Root => Path.GetDirectoryName(Application.dataPath);
    private static string Evidence => Path.Combine(Root, "DiagnosticsVerification");
    private static HumanVisionSettingsDeviceDiagnostics activeProbe;
    private static double probeDeadline;
    private static long firstSequence;
    private static string probeReport;
    [Serializable] private sealed class ProbeConfig { public string video; }
    [Serializable] private sealed class BuildIdentity { public string label, native_sha256; public bool native_stage_trace, hardware_diagnostics; }

    [MenuItem("Tools/Human Vision/Development/Install Settings Demo device diagnostics")]
    public static void Install()
    {
        if (EditorApplication.isPlaying) throw new InvalidOperationException("Exit Play Mode before installing scene diagnostics.");
        var current = EditorSceneManager.GetActiveScene();
        if (current.isDirty && current.path != ScenePath) throw new InvalidOperationException("Save the current scene before installing the Settings Demo.");
        if (current.path != ScenePath) current = EditorSceneManager.OpenScene(ScenePath);
        UpdateBundledVideoCatalog();
        var target = UnityEngine.Object.FindObjectOfType<HumanVisionSettingsController>();
        if (target == null) throw new InvalidOperationException("HumanVisionSettingsDemo has no SettingsController.");
        InstallOn(target); EditorSceneManager.MarkSceneDirty(current); EditorSceneManager.SaveScene(current);
        var prefab = PrefabUtility.LoadPrefabContents(PrefabPath);
        try { InstallOn(prefab.GetComponentInChildren<HumanVisionSettingsController>(true)); PrefabUtility.SaveAsPrefabAsset(prefab, PrefabPath); }
        finally { PrefabUtility.UnloadPrefabContents(prefab); }
        AssetDatabase.SaveAssets(); VerifyScene();
        Debug.Log("HumanVisionSettingsDemo 已安装实机日志、Android 帧率配置与常驻复制/导出按钮。");
    }
    private static void InstallOn(HumanVisionSettingsController target)
    {
        if (target == null || target.View == null) throw new InvalidOperationException("Settings Demo references are missing.");
        HumanVision.Editor.HumanVisionSettingsDemoBuilder.UpgradeResolutionControls(target.View);
        var canvas = target.View.GetComponent<Canvas>();
        Font font = canvas.GetComponentsInChildren<Text>(true).First(t => t.font != null).font;
        var previous = canvas.transform.Find("Device diagnostics bar");
        if (previous != null) UnityEngine.Object.DestroyImmediate(previous.gameObject);
        // 为底部常驻日志条腾出空间；其余设置控件及原有布局引用均保留。
        var preview = canvas.transform.Find("Preview panel") as RectTransform;
        if (preview != null) preview.anchorMin = new Vector2(0, .34f);
        var statusPanel = canvas.transform.Find("Runtime status") as RectTransform;
        if (statusPanel != null) { statusPanel.anchorMin = new Vector2(0, .26f); statusPanel.anchorMax = new Vector2(.72f, .34f); }
        var panelObject = new GameObject("Device diagnostics bar", typeof(RectTransform), typeof(Image));
        panelObject.transform.SetParent(canvas.transform, false);
        var rect = (RectTransform)panelObject.transform; rect.anchorMin = new Vector2(0, .16f); rect.anchorMax = new Vector2(.72f, .26f);
        rect.offsetMin = new Vector2(8, 3); rect.offsetMax = new Vector2(-8, -3); panelObject.GetComponent<Image>().color = new Color(.08f, .13f, .19f, 1);
        var labelObject = new GameObject("Device log path", typeof(RectTransform), typeof(Text)); labelObject.transform.SetParent(rect, false);
        var labelRect = (RectTransform)labelObject.transform; labelRect.anchorMin = new Vector2(0, .51f); labelRect.anchorMax = Vector2.one;
        labelRect.offsetMin = new Vector2(6, 0); labelRect.offsetMax = new Vector2(-6, 0);
        var label = labelObject.GetComponent<Text>(); label.font = font; label.fontSize = 13; label.color = Color.white; label.raycastTarget = false;
        label.horizontalOverflow = HorizontalWrapMode.Overflow; label.verticalOverflow = VerticalWrapMode.Truncate;
        label.text = "实机日志：进入运行后自动创建会话；普通 Unity 日志和详细骨骼默认开启。";
        var copy = Button(rect, "Copy device log path", "复制日志路径", 0, font);
        copy.GetComponentInChildren<Text>().text = "复制日志文件夹";
        var export = Button(rect, "Export device logs", "导出日志 ZIP", 1, font);
        var zip = Button(rect, "Copy exported ZIP path", "复制 ZIP 路径", 2, font);
        var skeleton = Button(rect, "Toggle skeleton capture", "骨骼日志：开启", 3, font);
        var oldHardware = canvas.transform.Find("Device hardware bar");
        if (oldHardware != null) UnityEngine.Object.DestroyImmediate(oldHardware.gameObject);
        var hardwareObject = new GameObject("Device hardware bar", typeof(RectTransform), typeof(Image));
        hardwareObject.transform.SetParent(canvas.transform, false);
        var hardwareRect = (RectTransform)hardwareObject.transform; hardwareRect.anchorMin = new Vector2(0, .08f); hardwareRect.anchorMax = new Vector2(.72f, .16f);
        hardwareRect.offsetMin = new Vector2(8, 3); hardwareRect.offsetMax = new Vector2(-8, -3); hardwareObject.GetComponent<Image>().color = new Color(.08f, .13f, .19f, 1);
        var hardwareTextObject = new GameObject("Hardware usage and native stages", typeof(RectTransform), typeof(Text));
        hardwareTextObject.transform.SetParent(hardwareRect, false);
        var hardwareTextRect = (RectTransform)hardwareTextObject.transform; hardwareTextRect.anchorMin = Vector2.zero; hardwareTextRect.anchorMax = Vector2.one;
        hardwareTextRect.offsetMin = new Vector2(6, 2); hardwareTextRect.offsetMax = new Vector2(-6, -2);
        var hardwareText = hardwareTextObject.GetComponent<Text>(); hardwareText.font = font; hardwareText.fontSize = 12;
        hardwareText.color = Color.white; hardwareText.raycastTarget = false; hardwareText.horizontalOverflow = HorizontalWrapMode.Wrap;
        hardwareText.text = "硬件指标与真实阶段耗时：运行后自动采样；无权限的指标明确显示不可用。";
        var diagnostics = target.GetComponent<HumanVisionSettingsDeviceDiagnostics>() ?? target.gameObject.AddComponent<HumanVisionSettingsDeviceDiagnostics>();
        if (target.GetComponent<HumanVisionSettingsDemoFrameRate>() == null) target.gameObject.AddComponent<HumanVisionSettingsDemoFrameRate>();
        if (target.GetComponent<HumanVisionSettingsBundledVideos>() == null) target.gameObject.AddComponent<HumanVisionSettingsBundledVideos>();
        string version = UnityEditor.PackageManager.PackageInfo.FindForAssembly(typeof(IHumanVisionFrameSource).Assembly)?.version ?? "unknown";
        diagnostics.Configure(target, label, copy, export, zip, skeleton, version, hardwareText);
        EditorUtility.SetDirty(diagnostics);
        // 标明高级区里的兼容入口，原 SDK 日志会被合并到实机 ZIP 中。
        foreach (var button in canvas.GetComponentsInChildren<Button>(true)) {
            if (button.name == "CopyLogPath") button.GetComponentInChildren<Text>().text = "复制 SDK 原始日志路径（兼容入口）";
            if (button.name == "ExportLogs") button.GetComponentInChildren<Text>().text = "导出 SDK 原始日志（已包含在实机 ZIP）";
        }
    }
    private static Button Button(Transform parent, string name, string title, int index, Font font)
    {
        var go = new GameObject(name, typeof(RectTransform), typeof(Image), typeof(Button)); go.transform.SetParent(parent, false);
        var rect = (RectTransform)go.transform; rect.anchorMin = new Vector2(index / 4f, 0); rect.anchorMax = new Vector2((index + 1) / 4f, .49f);
        rect.offsetMin = new Vector2(4, 2); rect.offsetMax = new Vector2(-4, -2); go.GetComponent<Image>().color = new Color(.12f, .28f, .40f);
        var textGo = new GameObject("Text", typeof(RectTransform), typeof(Text)); textGo.transform.SetParent(go.transform, false);
        var textRect = (RectTransform)textGo.transform; textRect.anchorMin = Vector2.zero; textRect.anchorMax = Vector2.one; textRect.offsetMin = textRect.offsetMax = Vector2.zero;
        var text = textGo.GetComponent<Text>(); text.font = font; text.text = title; text.color = Color.white; text.fontSize = 18;
        text.resizeTextForBestFit = true; text.resizeTextMinSize = 12; text.resizeTextMaxSize = 18; text.alignment = TextAnchor.MiddleCenter; text.raycastTarget = false;
        return go.GetComponent<Button>();
    }
    [MenuItem("Tools/Human Vision/Development/Verify Settings Demo diagnostics bindings")]
    public static void VerifyScene()
    {
        Directory.CreateDirectory(Evidence);
        var target = UnityEngine.Object.FindObjectOfType<HumanVisionSettingsDeviceDiagnostics>();
        if (target == null) throw new Exception("Missing device diagnostic component in HumanVisionSettingsDemo.");
        var serialized = new SerializedObject(target);
        foreach (string name in new[] { "controller", "logPathLabel", "copyPathButton", "exportButton", "copyZipButton", "skeletonButton", "hardwareLabel" })
            if (serialized.FindProperty(name).objectReferenceValue == null) throw new Exception("Missing diagnostics reference: " + name);
        foreach (string name in new[] { "copyPathButton", "exportButton", "copyZipButton", "skeletonButton" }) {
            var button = (Button)serialized.FindProperty(name).objectReferenceValue;
            if (!button.gameObject.activeInHierarchy) throw new Exception("Diagnostic button is hidden: " + name);
        }
        if (!target.RecordSkeletons) throw new Exception("Detailed skeleton sampling must be enabled for device test.");
        // y=0..0.08 是 SDK 原有应用/停止/返回按钮区；新增硬件条不得遮住这些入口。
        var hardwareRect = (RectTransform)((Text)serialized.FindProperty("hardwareLabel").objectReferenceValue).transform.parent;
        var diagnosticsRect = (RectTransform)((Text)serialized.FindProperty("logPathLabel").objectReferenceValue).transform.parent;
        if (hardwareRect.anchorMin.y < .08f || diagnosticsRect.anchorMin.y < hardwareRect.anchorMax.y)
            throw new Exception("Hardware/log panels overlap the original action buttons or each other.");
        if (target.GetComponent<HumanVisionSettingsBundledVideos>() == null || Resources.Load<TextAsset>(HumanVisionSettingsBundledVideos.ResourceName) == null)
            throw new Exception("Settings Demo is missing its real StreamingAssets video catalog.");
        var view = UnityEngine.Object.FindObjectOfType<HumanVisionSettingsController>().View;
        if (view.GetComponentsInChildren<InputField>(true).Any(f => f.name == "Width" || f.name == "Height"))
            throw new Exception("Resolution must use a dropdown; legacy width/height text fields remain.");
        var resolution = view.GetComponentsInChildren<Dropdown>(true).Single(d => d.name == "CaptureChoice");
        if (resolution.options.Count != 4 || resolution.options[1].text != "1280 × 720" || view.transform.GetComponentsInChildren<Transform>(true).All(t => t.name != "Capture resolution hint"))
            throw new Exception("Resolution choices or actual source size explanation are missing.");
        var frameRate = target.GetComponent<HumanVisionSettingsDemoFrameRate>();
        if (frameRate == null || frameRate.AndroidTargetFrameRate != 60)
            throw new Exception("Settings Demo is missing the explicit Android 60 FPS configuration.");
        string identityPath = Path.Combine(Root, "Assets/HumanVisionSettingsDemo/Resources/HumanVisionDeviceBuildInfo.json");
        if (File.Exists(identityPath)) {
            var package = UnityEditor.PackageManager.PackageInfo.FindForAssembly(typeof(HumanVisionSdk).Assembly);
            string expectedRoot = Path.GetFullPath(Path.Combine(Root, "Packages/com.blazetc.humanvision")).TrimEnd('/', '\\');
            if (package == null || Path.GetFullPath(package.resolvedPath).TrimEnd('/', '\\') != expectedRoot)
                throw new Exception("The RK3588 fix APK must use the project's embedded SDK package.");
            var identity = JsonUtility.FromJson<BuildIdentity>(File.ReadAllText(identityPath));
            using (var sha = SHA256.Create()) {
                string nativePath = Path.Combine(expectedRoot, "Runtime/Plugins/Android/arm64-v8a/libhumanvision.so");
                string actual = BitConverter.ToString(sha.ComputeHash(File.ReadAllBytes(nativePath))).Replace("-", "").ToLowerInvariant();
                if (actual != identity.native_sha256) throw new Exception("Embedded native library differs from this fix's verified identity.");
            }
        }
        if (EditorBuildSettings.scenes.Count(s => s.enabled) != 1 || EditorBuildSettings.scenes.Single(s => s.enabled).path != ScenePath)
            throw new Exception("Build scene must be HumanVisionSettingsDemo only.");
        File.WriteAllText(Path.Combine(Evidence, "scene-bindings.txt"), "PASS HumanVisionSettingsDemo: diagnostic component, all UGUI references, always visible buttons, resolution dropdown without width/height entry, real bundled video catalog, detailed skeleton capture, Android target 60 FPS, embedded native identity and single build scene.\n");
    }
    // 每次安装/构建都从真实文件生成目录；APK 内无需用文件系统扫描。
    // SHA 用流式读取，103 MB 测试视频不会整段加载进编辑器内存。
    private static void UpdateBundledVideoCatalog()
    {
        string streaming = Path.GetFullPath(Application.streamingAssetsPath).TrimEnd('/', '\\');
        var files = Directory.Exists(streaming) ? Directory.GetFiles(streaming, "*", SearchOption.AllDirectories)
            .Where(p => string.Equals(Path.GetExtension(p), ".mp4", StringComparison.OrdinalIgnoreCase)).OrderBy(p => p, StringComparer.Ordinal).ToArray() : Array.Empty<string>();
        var catalog = new HumanVisionSettingsBundledVideos.Catalog {
            videos = files.Select(p => {
                using (var sha = SHA256.Create()) using (var stream = File.OpenRead(p))
                    return new HumanVisionSettingsBundledVideos.Entry { relativePath = p.Substring(streaming.Length + 1).Replace('\\', '/'), bytes = stream.Length,
                        sha256 = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant() };
            }).ToArray()
        };
        string relative = "Assets/HumanVisionSettingsDemo/Resources/" + HumanVisionSettingsBundledVideos.ResourceName + ".json";
        string destination = Path.Combine(Root, relative); Directory.CreateDirectory(Path.GetDirectoryName(destination));
        string json = JsonUtility.ToJson(catalog, true) + "\n";
        if (!File.Exists(destination) || File.ReadAllText(destination) != json) File.WriteAllText(destination, json);
        AssetDatabase.ImportAsset(relative, ImportAssetOptions.ForceUpdate);
    }
    [MenuItem("Tools/Human Vision/Development/Verify Settings Demo real video logging")]
    public static void StartProbe()
    {
        if (!EditorApplication.isPlaying) throw new Exception("Enter Play Mode in HumanVisionSettingsDemo first.");
        activeProbe = UnityEngine.Object.FindObjectOfType<HumanVisionSettingsDeviceDiagnostics>();
        var controller = UnityEngine.Object.FindObjectOfType<HumanVisionSettingsController>();
        if (activeProbe == null || controller == null) throw new Exception("Diagnostic Settings Demo is not loaded.");
        Directory.CreateDirectory(Evidence); probeReport = Path.Combine(Evidence, "real-video-probe.txt");
        File.WriteAllText(probeReport, "START genuine local video; Unity all-level and threaded log capture; ZIP and clipboard\n");
        string video = Path.Combine(Application.streamingAssetsPath, "video-1.mp4");
        if (!File.Exists(video)) video = JsonUtility.FromJson<ProbeConfig>(File.ReadAllText(Path.Combine(Root, "sdk-api-probe-config.json"))).video;
        if (!File.Exists(video)) throw new FileNotFoundException("Provide an existing person-containing video in sdk-api-probe-config.json", video);
        var data = controller.Draft; data.SourceKind = InputKind.Video; data.Video.VideoPath = "";
        // 只更改运行草稿，不保存私有测试视频路径。
        data.UseWindowsCpu = true; data.Recognition.MaxBodies = 4; data.Recognition.UseRegions = false;
        data.Recognition.Regions = HumanVisionSdkConfiguration.CreateEqualRegions(4);
        controller.View.ShowDraft(data); controller.View.RefreshSources();
        var videoChoice = controller.View.GetComponentsInChildren<Dropdown>(true).Single(d => d.name == "VideoChoice");
        int index = videoChoice.options.FindIndex(o => o.text == Path.GetFileName(video));
        if (index >= 0) videoChoice.value = index;
        else data.Video.VideoPath = video;
        if (index < 0) controller.View.ShowDraft(data);
        if (Path.GetFullPath(controller.View.ReadDraft().Video.VideoPath) != Path.GetFullPath(video)) throw new Exception("Bundled video dropdown did not resolve the real StreamingAssets file.");
        controller.Execute("Apply"); firstSequence = 0; probeDeadline = EditorApplication.timeSinceStartup + 100;
        Debug.Log("HV_DIAGNOSTICS_VERIFY_MAIN rtsp://tester:do-not-export@camera/live?token=do-not-export-token");
        Debug.LogWarning("HV_DIAGNOSTICS_VERIFY_WARNING");
        new Thread(() => Debug.Log("HV_DIAGNOSTICS_VERIFY_BACKGROUND")).Start();
        EditorApplication.update -= ProbeTick; EditorApplication.update += ProbeTick;
    }
    private static void ProbeTick()
    {
        try {
            var controller = UnityEngine.Object.FindObjectOfType<HumanVisionSettingsController>();
            if (controller == null || activeProbe == null || !EditorApplication.isPlaying) throw new Exception("Probe scene stopped.");
            var sdk = controller.Sdk;
            if (EditorApplication.timeSinceStartup > probeDeadline) throw new Exception("No genuine skeleton logged: " + sdk.State + " " + sdk.LastError);
            if (!sdk.IsRunning || !sdk.HasFreshResult || sdk.GetUsersCount() == 0 || sdk.ResultSequence < firstSequence + 5) return;
            string session = activeProbe.CurrentLogDirectory;
            if (!Directory.GetFiles(session, "skeletons-*.jsonl").Any(p => new FileInfo(p).Length > 0)) return;
            if (Directory.GetFiles(session, "performance-*.csv").All(p => new FileInfo(p).Length < 700)) return;
            var buttons = controller.View.GetComponentsInChildren<Button>(true);
            buttons.Single(b => b.name == "Copy device log path").onClick.Invoke();
            if (GUIUtility.systemCopyBuffer != session) throw new Exception("Log path clipboard mismatch.");
            buttons.Single(b => b.name == "Export device logs").onClick.Invoke();
            if (!File.Exists(activeProbe.LastExportPath)) throw new Exception("ZIP not created: " + activeProbe.LastWriteError);
            buttons.Single(b => b.name == "Copy exported ZIP path").onClick.Invoke();
            if (GUIUtility.systemCopyBuffer != activeProbe.LastExportPath) throw new Exception("ZIP path clipboard mismatch.");
            string logs;
            // 验证交付给实测用户的 ZIP 内容，不读取运行中可能刚开始新批次写入的文件。
            using (var archive = ZipFile.OpenRead(activeProbe.LastExportPath))
                logs = string.Join("\n", archive.Entries.Select(e => { using (var reader = new StreamReader(e.Open())) return reader.ReadToEnd(); }));
            foreach (string marker in new[] { "HV_DIAGNOSTICS_VERIFY_MAIN", "HV_DIAGNOSTICS_VERIFY_WARNING", "HV_DIAGNOSTICS_VERIFY_BACKGROUND", "runtime.diagnostics", "pipeline.snapshot", "sourceId", "sourceClockDomain", "worldPlaneValid", "stableTrackId", "result.first" })
                if (!logs.Contains(marker)) throw new Exception("Missing real device diagnostic field: " + marker);
            if (logs.Contains("do-not-export") || logs.Contains("do-not-export-token")) throw new Exception("Sensitive URL leaked into diagnostic output.");
            File.AppendAllText(probeReport, "PASS native result sequence=" + sdk.ResultSequence + " users=" + sdk.GetUsersCount() + "\nPASS StreamingAssets video selection + Unity main/warning/background + redaction + genuine skeletons + CSV + runtime diagnostics + UGUI copy/export/ZIP path\nsession=" + session + "\nzip=" + activeProbe.LastExportPath + "\n");
            EditorApplication.update -= ProbeTick; Debug.Log("PASS HumanVisionSettingsDemo 实际视频日志与常驻按钮验证。");
        } catch (Exception e) { File.AppendAllText(probeReport, "FAIL " + e + "\n"); EditorApplication.update -= ProbeTick; Debug.LogException(e); }
    }
    [MenuItem("Tools/Human Vision/Development/Build Settings Demo device diagnostics APK")]
    public static void BuildAndroid()
    {
        if (EditorApplication.isPlaying) throw new Exception("Exit Play Mode before building.");
        PlayerSettings.enableFrameTimingStats = true;
        UpdateBundledVideoCatalog(); VerifyScene(); Directory.CreateDirectory(Path.Combine(Root, "Builds")); Directory.CreateDirectory(Evidence);
        bool localFix = File.Exists(Path.Combine(Root, "Assets/HumanVisionSettingsDemo/Resources/HumanVisionDeviceBuildInfo.json"));
        var identity = localFix ? JsonUtility.FromJson<BuildIdentity>(File.ReadAllText(Path.Combine(Root, "Assets/HumanVisionSettingsDemo/Resources/HumanVisionDeviceBuildInfo.json"))) : null;
        bool outputFix = identity != null && (identity.label ?? "").StartsWith("RK3588-Fix2-", StringComparison.Ordinal);
        string apk = Path.Combine(Root, "Builds", identity != null && identity.hardware_diagnostics ? "HumanVisionSettingsDemo-HardwareDiagnostics.apk" : identity != null && identity.native_stage_trace ? "HumanVisionSettingsDemo-PerformanceTrace.apk" :
            outputFix ? "HumanVisionSettingsDemo-RK3588-Fix2.apk" : localFix ? "HumanVisionSettingsDemo-RK3588-Fix1.apk" : "HumanVisionSettingsDemo-DeviceDiagnostics.apk");
        var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions { scenes = new[] { ScenePath }, locationPathName = apk, target = BuildTarget.Android, options = BuildOptions.None });
        File.WriteAllText(Path.Combine(Evidence, "android-build.txt"), "result=" + report.summary.result + "\nerrors=" + report.summary.totalErrors + "\nwarnings=" + report.summary.totalWarnings + "\nbytes=" + report.summary.totalSize + "\nduration=" + report.summary.totalTime + "\napk=" + apk + "\n" +
            string.Join("\n", report.steps.SelectMany(s => s.messages).Where(m => m.type == LogType.Error || m.type == LogType.Exception).Select(m => m.content)));
        if (report.summary.result != BuildResult.Succeeded) throw new Exception("Android Settings Demo build failed. See DiagnosticsVerification/android-build.txt");
        Debug.Log("PASS Settings Demo device diagnostic APK: " + apk);
    }
}
