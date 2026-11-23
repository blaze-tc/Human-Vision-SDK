using System;
using System.IO;
using System.Text;
using System.Xml;
using UnityEditor;
using UnityEditor.TestTools.TestRunner.Api;
using UnityEngine;

// Acceptance-project instrumentation; preserves the original recursive NUnit result.
[InitializeOnLoad]
public sealed class RuntimePrefabIndependentTestRecorder : ICallbacks
{
    private static readonly TestRunnerApi Api;
    static RuntimePrefabIndependentTestRecorder()
    {
        if (Path.GetFileName(Path.GetDirectoryName(Application.dataPath)) != "Human-Vision-SDK-Test") return;
        Api = ScriptableObject.CreateInstance<TestRunnerApi>();
        Api.hideFlags = HideFlags.HideAndDontSave;
        Api.RegisterCallbacks(new RuntimePrefabIndependentTestRecorder(), 1000);
    }
    public void RunStarted(ITestAdaptor testsToRun) { }
    public void TestStarted(ITestAdaptor test) { }
    public void TestFinished(ITestResultAdaptor result) { }
    public void RunFinished(ITestResultAdaptor result)
    {
        try {
            var directory = Path.Combine(Path.GetDirectoryName(Application.dataPath), "Builds/IndependentRuntime-20261123/TestResults");
            Directory.CreateDirectory(directory);
            var path = Path.Combine(directory, DateTime.UtcNow.ToString("yyyyMMddTHHmmssfffZ") + "-" + Guid.NewGuid().ToString("N") + ".xml");
            using (var file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
            using (var writer = XmlWriter.Create(file, new XmlWriterSettings { Encoding = new UTF8Encoding(false), Indent = true })) {
                writer.WriteStartElement("test-run");
                writer.WriteAttributeString("total", (result.PassCount + result.FailCount + result.SkipCount + result.InconclusiveCount).ToString());
                writer.WriteAttributeString("passed", result.PassCount.ToString()); writer.WriteAttributeString("failed", result.FailCount.ToString());
                writer.WriteAttributeString("skipped", result.SkipCount.ToString()); writer.WriteAttributeString("inconclusive", result.InconclusiveCount.ToString());
                result.ToXml().WriteTo(writer); writer.WriteEndElement();
            }
            Debug.Log("HV_INDEPENDENT_TEST_RESULTS " + path);
        } catch (Exception error) { Debug.LogException(error); }
    }
}
