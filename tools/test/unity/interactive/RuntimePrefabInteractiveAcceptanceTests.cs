#if UNITY_EDITOR
using System;
using System.Linq;
using System.Reflection;
using System.Collections;
using HumanVision;
using HumanVision.Demo;
using HumanVision.Input;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class RuntimePrefabInteractiveAcceptanceTests
    {
        private GameObject ownerObject;
        [TearDown] public void TearDown() { if (ownerObject != null) UnityEngine.Object.DestroyImmediate(ownerObject); }
        private static Type DemoType => AppDomain.CurrentDomain.GetAssemblies()
            .Select(assembly => assembly.GetType("HumanVisionAcceptance.RuntimePrefabAcceptance"))
            .FirstOrDefault(type => type != null);

        private static HumanVisionSdkOptions Build(HumanVisionSettingsData draft, HumanVisionExecutionPolicy execution, bool preview)
        {
            Assert.That(DemoType, Is.Not.Null, "The interactive acceptance Demo has not been installed.");
            var method = DemoType.GetMethod("BuildInteractiveOptions", BindingFlags.Static | BindingFlags.NonPublic);
            Assert.That(method, Is.Not.Null, "Three-input interactive options are missing.");
            try { return (HumanVisionSdkOptions)method.Invoke(null, new object[] { draft, execution, preview }); }
            catch (TargetInvocationException error) { throw error.InnerException; }
        }

        private static HumanVisionSettingsData Draft(InputKind kind) => new HumanVisionSettingsData {
            SourceKind = kind, AccelerationMode = HumanVisionAccelerationMode.Cpu, InputQuality = ModelInputQuality.Low,
            Rtsp = new DemoModeSettings { RtspUrl = "rtsp://127.0.0.1:18573/Exercise-11.mp4" },
            Video = new DemoModeSettings { VideoPath = "C:/fixtures/Exercise-11.mp4", RequestedFramesPerSecond = 25 },
            Camera = new DemoModeSettings { CameraDevice = "fixture camera", RequestedFramesPerSecond = 30 }
        };

        [Test] public void RtspPreservesIndependentPolicyPreviewAndTheCallerDraft()
        {
            var draft = Draft(InputKind.Rtsp);
            var options = Build(draft, HumanVisionExecutionPolicy.IndependentRequired, false);
            Assert.That(options.ExecutionPolicy, Is.EqualTo(HumanVisionExecutionPolicy.IndependentRequired));
            Assert.That(options.PreviewEnabled, Is.False);
            Assert.DoesNotThrow(options.Validate);
            options.Input.RtspUrl = "rtsp://127.0.0.1:18573/changed";
            options.Recognition.MaxBodies = 1;
            Assert.That(draft.Rtsp.RtspUrl, Does.EndWith("Exercise-11.mp4"));
            Assert.That(draft.Recognition.MaxBodies, Is.EqualTo(4));
        }

        [TestCase(InputKind.Video)]
        [TestCase(InputKind.WebCamera)]
        public void UnityOwnedSourcesExplicitlyUseTheirCompatibilityDriver(InputKind kind)
        {
            var draft = Draft(kind);
            var options = Build(draft, HumanVisionExecutionPolicy.IndependentRequired, true);
            Assert.That(options.SourceKind, Is.EqualTo(kind));
            Assert.That(options.ExecutionPolicy, Is.EqualTo(HumanVisionExecutionPolicy.CompatibleUnityInput));
            Assert.DoesNotThrow(options.Validate);
            if (kind == InputKind.Video) Assert.That(options.Input.VideoPath, Does.EndWith("Exercise-11.mp4"));
            else Assert.That(options.Input.CameraDevice, Is.EqualTo("fixture camera"));
        }

        [Test] public void UnsupportedWindowsNativeMirrorIsRejectedBeforeInitialization()
        {
            if (Application.platform != RuntimePlatform.WindowsEditor) Assert.Ignore("Windows native-source contract.");
            var draft = Draft(InputKind.Rtsp); draft.Rtsp.Mirror = true;
            Assert.That(Assert.Throws<ArgumentException>(() => Build(draft, HumanVisionExecutionPolicy.IndependentRequired, true)).Message,
                Does.Contain("镜像"));
            Assert.DoesNotThrow(() => Build(draft, HumanVisionExecutionPolicy.CompatibleUnityInput, true).Validate());
        }

        [Test] public void WindowsNpuIsRejectedBeforeInitialization()
        {
            if (Application.platform != RuntimePlatform.WindowsEditor) Assert.Ignore("Windows accelerator contract.");
            var draft = Draft(InputKind.Rtsp); draft.AccelerationMode = HumanVisionAccelerationMode.Neural;
            Assert.That(Assert.Throws<ArgumentException>(() => Build(draft, HumanVisionExecutionPolicy.CompatibleUnityInput, true)).Message,
                Does.Contain("NPU"));
        }

        [TestCase(false, HumanVisionAccelerationMode.Cpu, false)]
        [TestCase(false, HumanVisionAccelerationMode.Graphics, false)]
        [TestCase(true, HumanVisionAccelerationMode.Cpu, false)]
        [TestCase(true, HumanVisionAccelerationMode.Neural, false)]
        [TestCase(true, HumanVisionAccelerationMode.Graphics, true)]
        public void AndroidGraphicsAdmissionWaitsForTheActualInputContext(bool android, HumanVisionAccelerationMode mode, bool deferred)
        {
            var method = DemoType.GetMethod("DeferGraphicsProbeUntilInput", BindingFlags.Static | BindingFlags.NonPublic);
            Assert.That(method, Is.Not.Null);
            Assert.That(method.Invoke(null, new object[] { android, mode }), Is.EqualTo(deferred));
        }

        [Test] public void RegionEditorUsesTheSdkValidationBeforeApplying()
        {
            var method = DemoType.GetMethod("ParseInteractiveRegions", BindingFlags.Static | BindingFlags.NonPublic);
            Assert.That(method, Is.Not.Null);
            var draft = Draft(InputKind.Rtsp);
            draft.Recognition.MaxBodies = 2; draft.Recognition.UseRegions = true;
            draft.Recognition.Regions = (Rect[])method.Invoke(null, new object[] { "0,0,0.5,1\n0.5,0,0.5,1" });
            Assert.DoesNotThrow(() => Build(draft, HumanVisionExecutionPolicy.IndependentPreferred, true).Validate());
            draft.Recognition.Regions[1] = new Rect(.4f, 0, .5f, 1);
            Assert.Throws<ArgumentException>(() => Build(draft, HumanVisionExecutionPolicy.IndependentPreferred, true));
        }

        private object Owner(InputKind source)
        {
            ownerObject = new GameObject("interactive handler state test");
            object owner = ownerObject.AddComponent(DemoType);
            object state = Activator.CreateInstance(DemoType.GetNestedType("InteractiveSettings", BindingFlags.NonPublic), true);
            var data = Draft(source);
            state.GetType().GetField("Data").SetValue(state, data);
            state.GetType().GetField("Execution").SetValue(state, HumanVisionExecutionPolicy.IndependentRequired);
            state.GetType().GetField("Preview").SetValue(state, true);
            Set(owner, "interactive", state);
            var options = Build(data, HumanVisionExecutionPolicy.IndependentRequired, true);
            Set(owner, "startup", options); Set(owner, "recognition", options.Recognition.Clone());
            Set(owner, "rtspUrl", options.SourceKind == InputKind.Rtsp ? options.Input.RtspUrl :
                options.SourceKind == InputKind.Video ? options.Input.VideoPath : options.Input.CameraDevice);
            return owner;
        }
        private static void Set(object owner, string name, object value) => DemoType.GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).SetValue(owner, value);
        private static object Get(object owner, string name) => DemoType.GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).GetValue(owner);
        private static object Call(object owner, string name, params object[] values)
        {
            try { return DemoType.GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic).Invoke(owner, values); }
            catch (TargetInvocationException error) { throw error.InnerException; }
        }

        [TestCase(InputKind.Video)]
        [TestCase(InputKind.WebCamera)]
        public void ReturningFromUnityOwnedInputPreservesTheRtspExecutionPreference(InputKind source)
        {
            object owner = Owner(source);
            Call(owner, "RememberInteractiveConfiguration");
            object state = Get(owner, "interactive");
            var preference = (HumanVisionExecutionPolicy)state.GetType().GetField("Execution").GetValue(state);
            Assert.That(preference, Is.EqualTo(HumanVisionExecutionPolicy.IndependentRequired));
            var data = (HumanVisionSettingsData)state.GetType().GetField("Data").GetValue(state); data.SourceKind = InputKind.Rtsp;
            Assert.That(Build(data, preference, true).ExecutionPolicy, Is.EqualTo(HumanVisionExecutionPolicy.IndependentRequired));
        }

        [TestCase(false)]
        [TestCase(true)]
        public void ReconnectUsesTheAppliedAutoStreamSelection(bool autoSelect)
        {
            object owner = Owner(InputKind.Rtsp);
            ((HumanVisionSdkOptions)Get(owner, "startup")).AutoSelectRtspStream = autoSelect;
            Set(owner, "useRequestedStream", autoSelect); // Deliberately contradictory legacy thermal-only field.
            var action = (IEnumerator)Call(owner, "ConnectInteractive", new object[] { null });
            Assert.That(action.MoveNext(), Is.True);
            var initialize = action.Current;
            var captured = initialize.GetType().GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
                .Where(field => field.FieldType == typeof(HumanVisionSdkOptions)).Select(field => field.GetValue(initialize) as HumanVisionSdkOptions)
                .FirstOrDefault(value => value != null);
            Assert.That(captured, Is.Not.Null, "Connect must pass one captured options object into the existing Initialize owner.");
            Assert.That(captured.AutoSelectRtspStream, Is.EqualTo(autoSelect));
        }

        [Test] public void OpeningSettingsKeepsAnUnconnectedAddressInTheFieldAndDraft()
        {
            object owner = Owner(InputKind.Rtsp);
            string applied = ((HumanVisionSdkOptions)Get(owner, "startup")).Input.RtspUrl;
            const string edited = "rtsp://127.0.0.1:18573/new-unconnected.mp4";
            Set(owner, "rtspUrl", edited);
            Call(owner, "OpenBasicOptions");
            object state = Get(owner, "interactive");
            var data = (HumanVisionSettingsData)state.GetType().GetField("Data").GetValue(state);
            Assert.That(data.Rtsp.RtspUrl, Is.EqualTo(edited));
            Assert.That(Get(owner, "rtspUrl"), Is.EqualTo(edited));
            Assert.That(((HumanVisionSdkOptions)Get(owner, "startup")).Input.RtspUrl, Is.EqualTo(applied), "Opening a draft must not apply it.");
        }

        private static object ParseEditorCommand(string json)
        {
            var method = DemoType.GetMethod("ParseEditorVerificationCommand", BindingFlags.NonPublic | BindingFlags.Static);
            Assert.That(method, Is.Not.Null, "The Editor verification bridge is not installed.");
            try { return method.Invoke(null, new object[] { json }); }
            catch (TargetInvocationException error) { throw error.InnerException; }
        }

        [TestCase("{\"action\":\"compute\",\"mode\":\"gpu\",\"quality\":\"high\"}", "compute")]
        [TestCase("{\"action\":\"source\",\"kind\":\"video\",\"address\":\"C:/fixtures/Exercise-11.mp4\"}", "source")]
        [TestCase("{\"action\":\"fps\",\"value\":15}", "fps")]
        [TestCase("{\"action\":\"connect\"}", "connect")]
        [TestCase("{\"action\":\"stop\"}", "stop")]
        [TestCase("{\"action\":\"restart\"}", "restart")]
        [TestCase("{\"action\":\"unload\"}", "unload")]
        [TestCase("{\"action\":\"preview\",\"enabled\":false}", "preview")]
        [TestCase("{\"action\":\"capacity\",\"value\":1}", "capacity")]
        [TestCase("{\"action\":\"capacity\",\"value\":8}", "capacity")]
        [TestCase("{\"action\":\"regions\",\"enabled\":false}", "regions")]
        [TestCase("{\"action\":\"skeleton\",\"enabled\":true}", "skeleton")]
        [TestCase("{\"action\":\"fullscreen\",\"enabled\":false}", "fullscreen")]
        [TestCase("{\"action\":\"display\",\"confidence\":0.99,\"ageMs\":1000,\"handAgeMs\":200}", "display")]
        [TestCase("{\"action\":\"save\"}", "save")]
        [TestCase("{\"action\":\"load\"}", "load")]
        public void EditorBridgeParsesOnlyAllowlistedActions(string json, string action)
        {
            object command = ParseEditorCommand(json);
            Assert.That(command.GetType().GetField("Action").GetValue(command), Is.EqualTo(action));
        }

        [TestCase("[]")]
        [TestCase("{}")]
        [TestCase("{\"action\":\"fps\",\"value\":30,\"value\":60}")]
        [TestCase("{\"action\":\"fps\",\"value\":30.0}")]
        [TestCase("{\"action\":\"fps\",\"value\":20}")]
        [TestCase("{\"action\":\"fps\",\"value\":\"30\"}")]
        [TestCase("{\"action\":\"stop\",\"extra\":true}")]
        [TestCase("{\"action\":\"stop\"} false")]
        [TestCase("{\"action\":\"shutdownAll\"}")]
        [TestCase("{\"action\":\"compute\",\"mode\":\"gpu\",\"quality\":\"invalid\"}")]
        [TestCase("{\"action\":\"source\",\"kind\":\"rtsp\",\"address\":false}")]
        [TestCase("{\"action\":\"preview\",\"enabled\":\"false\"}")]
        [TestCase("{\"action\":\"regions\",\"enabled\":true,\"text\":false}")]
        [TestCase("{\"action\":\"capacity\",\"value\":0}")]
        [TestCase("{\"action\":\"capacity\",\"value\":9}")]
        [TestCase("{\"action\":\"capacity\",\"value\":1.0}")]
        [TestCase("{\"action\":\"display\",\"confidence\":1.01,\"ageMs\":1000,\"handAgeMs\":200}")]
        [TestCase("{\"action\":\"display\",\"confidence\":0.5,\"ageMs\":0,\"handAgeMs\":200}")]
        [TestCase("{\"action\":\"display\",\"confidence\":0.5,\"ageMs\":1000}")]
        [TestCase("{\"action\":\"display\",\"confidence\":\"0.5\",\"ageMs\":1000,\"handAgeMs\":200}")]
        public void EditorBridgeRejectsMalformedUnknownOrWrongTypeParameters(string json) =>
            Assert.Throws<ArgumentException>(() => ParseEditorCommand(json));

        [Test] public void EditorBridgeRejectsOversizedJsonBeforeParsing() =>
            Assert.Throws<ArgumentException>(() => ParseEditorCommand(new string(' ', 4097)));

        [Test] public void EditorBridgeCannotStartAnOperationOutsidePlayMode()
        {
            object owner = Owner(InputKind.Rtsp);
            var property = DemoType.GetProperty("EditorVerificationCommand");
            Assert.That(property, Is.Not.Null);
            var error = Assert.Throws<TargetInvocationException>(() => property.SetValue(owner, "{\"action\":\"connect\"}"));
            Assert.That(error.InnerException, Is.TypeOf<ArgumentException>());
            Assert.That(Get(owner, "operating"), Is.False);
            Assert.That((string)property.GetValue(owner), Does.Contain("rejected"));
        }

        [Test] public void EditorConnectionRejectsAnInvalidAddressBeforeStartingTheExistingCoroutine()
        {
            object owner = Owner(InputKind.Rtsp);
            var options = (HumanVisionSdkOptions)Get(owner, "startup");
            ((bool[])Get(owner, "installedModes"))[(int)options.AccelerationMode] = true;
            var choice = (ModelInputQualityChoice)Activator.CreateInstance(typeof(ModelInputQualityChoice),
                BindingFlags.Instance | BindingFlags.NonPublic, null, new object[] { options.InputQuality, "fixture", 512, 288 }, null);
            ((ModelInputQualityChoice[][])Get(owner, "installedQualities"))[(int)options.AccelerationMode] = new[] { choice };
            Set(owner, "rtspUrl", "invalid input address");
            Assert.Throws<ArgumentException>(() => Call(owner, "ValidateEditorConnection"));
            Assert.That(Get(owner, "operating"), Is.False);
            Assert.That(options.Input.RtspUrl, Does.StartWith("rtsp://"), "Validation must not change the applied options.");
        }

        [Test] public void EditorRegionCommandPreservesInputGeometryAndUsesTheExistingSdkValidation()
        {
            var draft = Draft(InputKind.Rtsp);
            int requestedWidth = draft.Mode.RequestedWidth, requestedHeight = draft.Mode.RequestedHeight;
            object command = ParseEditorCommand("{\"action\":\"regions\",\"enabled\":true,\"text\":\"0,0,0.25,1\\n0.25,0,0.25,1\\n0.5,0,0.25,1\\n0.75,0,0.25,1\"}");
            var edit = DemoType.GetMethod("EditEditorBasicDraft", BindingFlags.NonPublic | BindingFlags.Static);
            edit.Invoke(null, new object[] { draft, command });
            var options = Build(draft, HumanVisionExecutionPolicy.IndependentRequired, true);
            Assert.DoesNotThrow(options.Validate);
            Assert.That(options.Recognition.UseRegions, Is.True);
            Assert.That(options.Recognition.Regions.Length, Is.EqualTo(4));
            Assert.That(options.Input.RequestedWidth, Is.EqualTo(requestedWidth));
            Assert.That(options.Input.RequestedHeight, Is.EqualTo(requestedHeight));
            command = ParseEditorCommand("{\"action\":\"regions\",\"enabled\":true,\"text\":\"0,0,2,1\"}");
            edit.Invoke(null, new object[] { draft, command });
            Assert.Throws<ArgumentException>(() => Build(draft, HumanVisionExecutionPolicy.IndependentRequired, true));
        }

        [Test] public void EditorDisplayCommandEditsTheValidatedSdkConfiguration()
        {
            var draft = Draft(InputKind.Rtsp);
            object command = ParseEditorCommand("{\"action\":\"display\",\"confidence\":0.99,\"ageMs\":600,\"handAgeMs\":80}");
            DemoType.GetMethod("EditEditorBasicDraft", BindingFlags.NonPublic | BindingFlags.Static).Invoke(null, new object[] { draft, command });
            var options = Build(draft, HumanVisionExecutionPolicy.IndependentRequired, true);
            Assert.DoesNotThrow(options.Validate);
            Assert.That(options.Recognition.MinimumJointConfidence, Is.EqualTo(.99f));
            Assert.That(options.Recognition.MaximumResultAgeMilliseconds, Is.EqualTo(600));
            Assert.That(options.Recognition.MaximumHandAgeMilliseconds, Is.EqualTo(80));
        }
    }
}
#endif
