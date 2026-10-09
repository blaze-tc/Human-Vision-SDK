using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using HumanVision.Demo;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionAndroidCpuContractTests
    {
        private const string Profile = "android-cpu-nohands";
        private string root;
        [Serializable] private sealed class Index { public string version; public Entry[] files; }
        [Serializable] private sealed class Entry { public string path, sha256; }
        [SetUp] public void CopyActualCpuClosure()
        {
            string source = Environment.GetEnvironmentVariable("HV_TEST_NEURAL_RUNTIME_ROOT");
            if (string.IsNullOrEmpty(source)) source = Path.Combine(Environment.GetEnvironmentVariable("HV_TEST_RUNTIME_ROOT") ?? "", "out", "rknn-dual-20261009", "stage", "Runtime");
            if (!Directory.Exists(source)) source = Path.Combine(Application.streamingAssetsPath, "HumanVision", "Runtime");
            var index = JsonUtility.FromJson<Index>(File.ReadAllText(Path.Combine(source,"index.json")));
            index.files = index.files.Where(row => row.path == "profiles/" + Profile + ".json" ||
                row.path.StartsWith("modelpacks/precision-t-26/",StringComparison.Ordinal) || row.path.StartsWith("modelpacks/rtmo-t-416/",StringComparison.Ordinal)).ToArray();
            root = Path.Combine(Path.GetTempPath(),"HumanVision-cpu-"+Guid.NewGuid().ToString("N")); Directory.CreateDirectory(root);
            foreach(var row in index.files) { string target=Path.Combine(root,row.path); Directory.CreateDirectory(Path.GetDirectoryName(target)); File.Copy(Path.Combine(source,row.path),target); }
            File.WriteAllText(Path.Combine(root,"index.json"),JsonUtility.ToJson(index));
        }
        [TearDown] public void Cleanup() { if(Directory.Exists(root)) Directory.Delete(root,true); }
        [TestCase(1,"precision-t-26",192,256)] [TestCase(2,"precision-t-26",192,256)]
        [TestCase(3,"rtmo-t-416",416,416)] [TestCase(8,"rtmo-t-416",416,416)]
        public void ActualCpuContractUsesItsCapacityBranchAndFixedGeometry(int people,string pack,int width,int height)
        {
            var shared=new SharedRecognitionSettings { MaxBodies=people, AccelerationMode=(HumanVisionAccelerationMode)2, InputQuality=ModelInputQuality.High };
            var contract=shared.ResolveContract(root,Profile);
            Assert.AreEqual(Profile,contract.ProfileId); Assert.AreEqual(pack,contract.ModelPackId);
            Assert.AreEqual(new Vector2Int(width,height),new Vector2Int(contract.PoseWidth,contract.PoseHeight));
            var capabilities=HumanVisionSdkQualityCapabilities.Load(root,Profile);
            Assert.IsEmpty(capabilities.Error); Assert.False(capabilities.Selectable); StringAssert.Contains("416×416",capabilities.Message);
        }
        [TestCase("backend")] [TestCase("fallback")] [TestCase("capacity")]
        [TestCase("missing-small-body")] [TestCase("missing-large-body")] [TestCase("rehashed-large-body")]
        [TestCase("manifest")]
        public void ReindexedCpuDamageFailsBeforeAContractOrChoiceCanBeAdvertised(string mutation)
        {
            string profile=Path.Combine(root,"profiles",Profile+".json");
            switch(mutation) {
                case "backend": Replace(profile,"backend.ort.cpu","backend.rknn"); break;
                case "fallback": Replace(profile,"\"allow_fallback\": false","\"allow_fallback\": true"); break;
                case "capacity": Replace(profile,"\"max_people\": 2","\"max_people\": 8"); break;
                case "missing-small-body": File.Delete(Path.Combine(root,"modelpacks/precision-t-26/body.onnx")); break;
                case "missing-large-body": File.Delete(Path.Combine(root,"modelpacks/rtmo-t-416/body.onnx")); break;
                case "rehashed-large-body": using(var file=File.OpenWrite(Path.Combine(root,"modelpacks/rtmo-t-416/body.onnx"))) { file.WriteByte(0); } break;
                case "manifest": Replace(Path.Combine(root,"modelpacks/rtmo-t-416/manifest.json"),"\"width\": 416","\"width\": 512"); break;
            }
            var index=JsonUtility.FromJson<Index>(File.ReadAllText(Path.Combine(root,"index.json")));
            foreach(var row in index.files) if(File.Exists(Path.Combine(root,row.path))) row.sha256=Hash(Path.Combine(root,row.path));
            File.WriteAllText(Path.Combine(root,"index.json"),JsonUtility.ToJson(index));
            Assert.Throws<InvalidDataException>(() => new SharedRecognitionSettings { MaxBodies=1 }.ResolveContract(root,Profile));
            var capabilities=HumanVisionSdkQualityCapabilities.Load(root,Profile);
            Assert.IsNotEmpty(capabilities.Error); Assert.IsEmpty(capabilities.Choices);
        }
        private static void Replace(string path,string before,string after) {
            string text=File.ReadAllText(path); StringAssert.Contains(before,text); File.WriteAllText(path,text.Replace(before,after));
        }
        private static string Hash(string path) { using(var file=File.OpenRead(path)) using(var sha=SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(file)).Replace("-","").ToLowerInvariant(); }
        [Test] public void ActualAuditedCpuElfClosurePassesAndAlteredClaimsAreRejected()
        {
            string repository=Environment.GetEnvironmentVariable("HV_TEST_RUNTIME_ROOT");
            string package;
            if(!string.IsNullOrEmpty(repository)) package=Path.Combine(repository,"upm/com.blazetc.humanvision");
            else {
                var nativeImporter=UnityEditor.PluginImporter.GetAllImporters().First(value=>Path.GetFileName(value.assetPath)=="libhumanvision.so");
                package=UnityEditor.PackageManager.PackageInfo.FindForAssetPath(nativeImporter.assetPath).resolvedPath;
            }
            string native=Path.Combine(package,"Runtime/Plugins/Android/arm64-v8a/libhumanvision.so");
            string runtime=Path.Combine(package,"Runtime/Plugins/Android/arm64-v8a/libonnxruntime.so");
            string saved=File.ReadAllText(Path.Combine(package,"android-cpu-runtime-symbols.json"));
            string audit=Path.Combine(root,"cpu-audit.json");
            var method=typeof(HumanVision.Editor.HumanVisionAndroidBuildSettings).GetMethod("ValidateCpuRuntimeAudit",System.Reflection.BindingFlags.Static|System.Reflection.BindingFlags.NonPublic);
            Assert.NotNull(method); File.WriteAllText(audit,saved);
            Assert.True((bool)method.Invoke(null,new object[]{native,runtime,audit}));
            foreach(var change in new[] {
                new[]{"\"ort_cpu_symbols_verified\": true","\"ort_cpu_symbols_verified\": false"},
                new[]{"\"ort_api_version\": 23","\"ort_api_version\": 22"},
                new[]{"\"abi\": \"arm64-v8a\"","\"abi\": \"x86_64\""},
                new[]{"cd1285f8955f3abcb0127d1ffaf1e5da7893d2547872a831b4717c0bfe388328",new string('0',64)} }) {
                StringAssert.Contains(change[0],saved); File.WriteAllText(audit,saved.Replace(change[0],change[1]));
                Assert.False((bool)method.Invoke(null,new object[]{native,runtime,audit}));
            }
        }
    }
}
