using System;
using System.IO;
using UnityEditor;
using UnityEditor.TestTools.TestRunner.Api;
using UnityEngine;

// Runs the explicitly named regression fixtures together; does not drive input.
[InitializeOnLoad]
public static class RuntimePrefabRealtimeTestBatch
{
    private static TestRunnerApi api;
    private static string Root => Path.GetDirectoryName(Application.dataPath);
    private static string Status => Path.Combine(Root, "Library/HumanVisionRealtimeTestBatch.json");
    [Serializable] private sealed class Receipt { public string state; public int passed, failed, skipped, inconclusive; public bool accepted; }
    static RuntimePrefabRealtimeTestBatch()
    {
        if (Path.GetFileName(Root) != "Human-Vision-SDK-Test") return;
        api = ScriptableObject.CreateInstance<TestRunnerApi>(); api.hideFlags = HideFlags.HideAndDontSave;
        api.RegisterCallbacks(new Capture(), 1001);
    }
    [MenuItem("Human Vision/Acceptance/Run Realtime Regression Batch")]
    public static void Run()
    {
        if (api == null || EditorApplication.isPlayingOrWillChangePlaymode || EditorApplication.isCompiling)
            throw new InvalidOperationException("Use the stopped acceptance Editor after compilation.");
        if (File.Exists(Status) && JsonUtility.FromJson<Receipt>(File.ReadAllText(Status)).state == "running")
            throw new InvalidOperationException("The realtime regression batch is already running.");
        File.WriteAllText(Status, JsonUtility.ToJson(new Receipt { state = "running" }));
        api.Execute(new ExecutionSettings(new Filter { testMode = TestMode.EditMode, testNames = new[] {
            "HumanVision.Input.Tests.NativeLiveFrameConsumerTests",
            "HumanVision.Input.Tests.InputRetirementPumpEditModeTests",
            "HumanVision.Tests.NativeLiveAttachmentTests",
            "HumanVision.Tests.HumanVisionIndependentRuntimeTests",
            "HumanVision.Tests.NativePreviewOwnershipTests",
            "HumanVision.Input.Tests.AndroidRtspOutputSizeTests",
            "HumanVision.Tests.RuntimePrefabInteractiveAcceptanceTests",
            "HumanVision.Tests.InputAdapterTests"
        } }));
    }
    private sealed class Capture : ICallbacks
    {
        public void RunStarted(ITestAdaptor test) { }
        public void TestStarted(ITestAdaptor test) { }
        public void TestFinished(ITestResultAdaptor result) { }
        public void RunFinished(ITestResultAdaptor result)
        {
            if (!File.Exists(Status) || JsonUtility.FromJson<Receipt>(File.ReadAllText(Status)).state != "running") return;
            var value = new Receipt { state = "completed", passed = result.PassCount, failed = result.FailCount,
                skipped = result.SkipCount, inconclusive = result.InconclusiveCount };
            int total = value.passed + value.failed + value.skipped + value.inconclusive;
            value.accepted = total >= 104 && value.failed == 0 && value.skipped == 0 && value.inconclusive == 0;
            File.WriteAllText(Status, JsonUtility.ToJson(value, true));
            Debug.Log("HV_REALTIME_REGRESSION_BATCH " + JsonUtility.ToJson(value));
        }
    }
}
