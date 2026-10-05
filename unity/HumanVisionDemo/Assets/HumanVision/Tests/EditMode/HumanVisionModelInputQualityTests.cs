using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Xml;
using NUnit.Framework;
using UnityEngine;

namespace HumanVision.Tests
{
    public sealed class HumanVisionModelInputQualityTests
    {
        private const string Base = "android-ncnn-vulkan";
        private const string High = Base + "-quality-high";
        private const string Low = Base + "-quality-low";
        private string root;
        [Serializable] private sealed class Index { public string version; public Entry[] files; }
        [Serializable] private sealed class Entry { public string path; public string sha256; }
        [SetUp] public void Setup()
        {
            root = Path.Combine(Path.GetTempPath(), "HumanVision-quality-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(root);
        }
        [TearDown] public void Cleanup() { Directory.Delete(root, true); }

        [Test] public void BakedQualityAllowlistAdmitsHighAndLow()
        {
            Assert.That(Resolve(High, Base, Base, new[] { Low, Base, High }), Is.EqualTo(High));
            Assert.That(Resolve(Low, Base, Base, new[] { Low, Base, High }), Is.EqualTo(Low));
            Assert.That(Resolve("auto", Base, Base, new[] { Low, Base, High }), Is.EqualTo(Base));
        }
        [TestCase("windows-pc-cpu")]
        [TestCase("android-ort-cpu")]
        [TestCase("android-ncnn-vulkan-quality-ultra")]
        public void BakedQualityAllowlistRejectsUnbundledOrCrossModeProfiles(string profile)
        {
            Assert.That(Caught(() => Resolve(profile, Base, Base, new[] { Low, Base, High })), Is.TypeOf<InvalidOperationException>());
        }
        [Test] public void OriginalTwoArgumentConflictBehaviorStaysExact()
        {
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile(High, Base));
            Assert.That(HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile("auto", Base), Is.EqualTo(Base));
            Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile(Base, "android-ort-cpu"));
        }
        [Test] public void InvalidBakedAllowlistFailsBeforeAdmission()
        {
            foreach (var list in new[] { new[] { High, High }, new[] { High }, new[] { "android-ort-cpu", Low }, new[] { High, Base, Low, "other" } })
                Assert.That(Caught(() => Resolve(Base, Base, Base, list)), Is.TypeOf<InvalidOperationException>());
            Assert.That(Caught(() => Resolve(High, "android-ort-cpu", "android-ort-cpu", new[] { Low, Base, High })), Is.TypeOf<InvalidOperationException>());
        }
        [Test] public void MissingCatalogKeepsLegacyAndPcHasNoQualityFamily()
        {
            var catalog = Load(root);
            Assert.That(Profiles(catalog, Base), Is.Empty);
            Assert.That(Profiles(catalog, "windows-pc-cpu"), Is.Empty);
        }
        [Test] public void RealCombinedCatalogResolvesActualProfileShapesAndPcUnavailability()
        {
            CopyApproved(); var catalog = Load(root);
            Assert.That(Profiles(catalog, Base), Is.EquivalentTo(new[] { Low, Base, High }));
            Assert.That(Profiles(catalog, "windows-pc-directml"), Is.Empty);
            var resolve = catalog.GetType().GetMethod("ResolveQuality", new[] { typeof(string), typeof(string) });
            Assert.That(resolve.Invoke(catalog, new object[] { Base, "high" }), Is.EqualTo(High));
            Assert.That(Caught(() => resolve.Invoke(catalog, new object[] { "windows-pc-cpu", "high" })), Is.TypeOf<InvalidOperationException>());
        }
        [TestCase("schema")]
        [TestCase("duplicate")]
        [TestCase("missing")]
        [TestCase("unknown-field")]
        [TestCase("duplicate-json-key")]
        [TestCase("unsafe")]
        [TestCase("dimensions")]
        [TestCase("bool-type")]
        [TestCase("fallback")]
        [TestCase("weights")]
        [TestCase("capacity")]
        [TestCase("capabilities")]
        [TestCase("profile-capabilities")]
        [TestCase("source")]
        [TestCase("license")]
        [TestCase("recipe")]
        [TestCase("options")]
        [TestCase("hash")]
        [TestCase("indexed-missing")]
        public void RehashedMalformedOrSemanticallyWrongCatalogFails(string mutation)
        {
            CopyApproved(); string catalog = Path.Combine(root, "model-input-qualities.json");
            string profile = Path.Combine(root, "profiles", High + ".json");
            string pack = Directory.GetFiles(Path.Combine(root, "modelpacks"), "modelpack.json", SearchOption.AllDirectories).Single(p => p.Contains("960x576"));
            switch (mutation)
            {
                case "schema": Replace(catalog, "\"schema_version\": 1", "\"schema_version\": 2"); break;
                case "duplicate": Replace(catalog, "\"id\": \"high\"", "\"id\": \"low\""); break;
                case "missing": Replace(catalog, "\"families\": [", "\"other\": ["); break;
                case "unknown-field": Replace(catalog, "\"schema_version\": 1", "\"extra\": true, \"schema_version\": 1"); break;
                case "duplicate-json-key": Replace(catalog, "\"schema_version\": 1", "\"schema_version\": 1, \"schema_version\": 1"); break;
                case "unsafe": Replace(catalog, High, "../" + High); break;
                case "dimensions": Replace(pack, "\"height\": 576", "\"height\": 544"); break;
                case "bool-type": Replace(profile, "\"allow_fallback\": false", "\"allow_fallback\": \"false\""); Rebind(pack, profile); break;
                case "fallback": Replace(profile, "\"allow_fallback\": false", "\"allow_fallback\": true"); Rebind(pack, profile); break;
                case "weights": Replace(pack, "6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9", new string('0',64)); break;
                case "capacity": Replace(pack, "\"max_people\": 8", "\"max_people\": 4"); break;
                case "capabilities": Replace(pack, "\"body_pose\"", "\"body_pose\", \"hand_pose\""); break;
                case "profile-capabilities": Replace(profile, "\"body_pose\"", "\"other\""); Rebind(pack, profile); break;
                case "source": Replace(pack, "https://github.com/nihui/ncnn-android-yolov8/tree/f1ac75ec54ccb3817a9eba620fe51da8bdcf87ca", ""); break;
                case "license": Replace(pack, "Ultralytics origin; distribution rights unestablished; local evaluation only", ""); break;
                case "recipe": Replace(pack, "Pinned upstream ncnn graph; reviewed FP32 rectangle960x576 offline numerical and raised-left-arm eligibility; local evaluation only", ""); break;
                case "options": Replace(pack, "\"use_fp16_storage\": false", "\"use_fp16_storage\": true"); break;
                case "hash": File.AppendAllText(Path.Combine(root, "profiles/windows-pc-cpu.json"), "tamper"); break;
            }
            var index = JsonUtility.FromJson<Index>(File.ReadAllText(Path.Combine(root,"index.json")));
            foreach (var row in index.files)
                if (new[] { catalog, profile, pack }.Contains(Path.Combine(root, row.path))) row.sha256 = Hash(Path.Combine(root,row.path));
            File.WriteAllText(Path.Combine(root,"index.json"),JsonUtility.ToJson(index));
            if (mutation == "indexed-missing") File.Delete(catalog);
            Assert.That(Caught(() => Load(root)), Is.TypeOf<InvalidDataException>());
        }
        [Test] public void UnindexedStaleCachedCatalogIsRetainedAndIgnored()
        {
            CopyApproved(); var index = JsonUtility.FromJson<Index>(File.ReadAllText(Path.Combine(root,"index.json")));
            index.files = index.files.Where(row => row.path != "model-input-qualities.json").ToArray();
            File.WriteAllText(Path.Combine(root,"index.json"),JsonUtility.ToJson(index));
            File.WriteAllText(Path.Combine(root,"model-input-qualities.json"),"stale malformed bytes");
            Assert.That(Profiles(Load(root),Base),Is.Empty);
            Assert.That(File.ReadAllText(Path.Combine(root,"model-input-qualities.json")),Is.EqualTo("stale malformed bytes"));
        }
        [Test] public void BuildMetadataEmitsOnlyValidatedSameModeQualityAllowlist()
        {
            CopyApproved(); var editor = EditorType("HumanVisionAndroidGradleMetadataWriter");
            var registry = EditorType("HumanVisionAndroidRuntimeModeRegistry");
            var descriptor = registry.GetMethod("Resolve").Invoke(null, new object[] { Base });
            string manifest = Path.Combine(root,"AndroidManifest.xml");
            File.WriteAllText(manifest,"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"><application /></manifest>");
            var method = editor.GetMethods().FirstOrDefault(m => m.Name == "WriteSelectionMetadata" && m.GetParameters().Length == 3);
            if (method == null) editor.GetMethod("WriteSelectionMetadata").Invoke(null,new[] { manifest, descriptor });
            else method.Invoke(null,new[] { manifest, descriptor, root });
            var xml = new XmlDocument(); xml.Load(manifest);
            var values = xml.SelectNodes("//meta-data").Cast<XmlElement>().ToDictionary(e => e.GetAttribute("name","http://schemas.android.com/apk/res/android"),e => e.GetAttribute("value","http://schemas.android.com/apk/res/android"));
            Assert.That(values.ContainsKey("com.blazetc.humanvision.quality_profiles"),Is.True);
            Assert.That(values["com.blazetc.humanvision.quality_profiles"],Is.EqualTo("[\"" + Low + "\",\"" + Base + "\",\"" + High + "\"]"));
        }
        private static string Resolve(string configured,string baked,string mode,string[] list)
        {
            var method=typeof(HumanVisionAndroidRuntimeSelection).GetMethods().FirstOrDefault(m => m.Name=="ResolveConfiguredProfile" && m.GetParameters().Length==4);
            return method == null ? HumanVisionAndroidRuntimeSelection.ResolveConfiguredProfile(configured,baked) : (string)method.Invoke(null,new object[] { configured,baked,mode,list });
        }
        private static object Load(string path)
        {
            var type=typeof(HumanVisionAndroidRuntimeSelection).Assembly.GetType("HumanVision.HumanVisionModelInputQualities");
            Assert.That(type,Is.Not.Null,"Q2 runtime catalog reader is missing");
            return type.GetMethod("Load").Invoke(null,new object[] { path });
        }
        private static string[] Profiles(object catalog,string mode) => (string[])catalog.GetType().GetMethod("ProfilesForMode").Invoke(catalog,new object[] { mode });
        private static Exception Caught(Action action) { try { action(); return null; } catch (TargetInvocationException e) { return e.InnerException; } catch (Exception e) { return e; } }
        private void CopyApproved()
        {
            string source=Path.Combine(Application.dataPath,"..","ApprovedQualities"); Assert.That(Directory.Exists(source),Is.True,"Stage real Q2 model closure first");
            foreach (var file in Directory.GetFiles(source,"*",SearchOption.AllDirectories)) { string target=Path.Combine(root,file.Substring(source.Length+1)); Directory.CreateDirectory(Path.GetDirectoryName(target)); File.Copy(file,target); }
        }
        private static Type EditorType(string name) => AppDomain.CurrentDomain.GetAssemblies().Select(a=>a.GetType("HumanVision.Editor."+name)).First(t=>t!=null);
        private static void Replace(string path,string from,string to) { var text=File.ReadAllText(path); Assert.That(text,Does.Contain(from)); File.WriteAllText(path,text.Replace(from,to)); }
        private static void Rebind(string pack,string profile) { var text=File.ReadAllText(pack); var match=System.Text.RegularExpressions.Regex.Match(text,"\\\"profile_sha256\\\": \\\"([a-f0-9]{64})\\\""); Replace(pack,match.Groups[1].Value,Hash(profile)); }
        private static string Hash(string path) { using(var stream=File.OpenRead(path)) using(var sha=SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant(); }
    }
}
