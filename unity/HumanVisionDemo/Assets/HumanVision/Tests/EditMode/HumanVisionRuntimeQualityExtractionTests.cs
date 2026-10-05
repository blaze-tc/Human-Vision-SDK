using System;
using System.Collections;
using System.IO;
using System.Security.Cryptography;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;

namespace HumanVision.Tests
{
    public sealed class HumanVisionRuntimeQualityExtractionTests
    {
        private string source, cache, name, sourceBackup;
        private byte[] previousIndex;
        [SetUp] public void Setup()
        {
            if (!Application.persistentDataPath.Replace('\\','/').Contains("quality-q2-project")) Assert.Ignore("Run the isolated Q2 scratch fixture.");
            source=Path.Combine(Application.streamingAssetsPath,"HumanVision/Runtime");
            if (Directory.Exists(source))
            {
                sourceBackup=Path.Combine(Path.GetDirectoryName(Application.dataPath),"Q2RuntimeBackup-"+Guid.NewGuid().ToString("N"));
                Directory.Move(source,sourceBackup);
            }
            Directory.CreateDirectory(source); cache=Path.Combine(Application.persistentDataPath,"HumanVisionRuntime"); Directory.CreateDirectory(cache);
            string index=Path.Combine(cache,"index.json"); previousIndex=File.Exists(index)?File.ReadAllBytes(index):null;
            name="q2-test-"+Guid.NewGuid().ToString("N")+".bin";
        }
        [TearDown] public void Cleanup()
        {
            if(source!=null && Directory.Exists(source)) Directory.Delete(source,true);
            if(sourceBackup!=null && Directory.Exists(sourceBackup)) Directory.Move(sourceBackup,source);
            if(cache==null) return;
            string index=Path.Combine(cache,"index.json");
            if(previousIndex==null) File.Delete(index); else File.WriteAllBytes(index,previousIndex);
            foreach(string suffix in new[]{"",".partial"}) File.Delete(Path.Combine(cache,name+suffix));
        }
        [UnityTest] public IEnumerator PreparedRootReceivesOriginalIndexOnlyAfterAssetHashValidation()
        {
            string asset=Path.Combine(source,name); File.WriteAllText(asset,"actual Q2 extraction fixture bytes");
            string json="{\"version\":\"q2-extraction\",\"files\":[{\"path\":\""+name+"\",\"sha256\":\""+Hash(asset)+"\"}]}";
            File.WriteAllText(Path.Combine(source,"index.json"),json); string ready=null,error=null;
            yield return Complete(HumanVisionRuntimeData.Prepare(value=>ready=value,value=>error=value));
            Assert.That(error,Is.Null); Assert.That(ready,Is.EqualTo(cache));
            Assert.That(File.Exists(Path.Combine(cache,"index.json")),Is.True,"Prepared root must contain its validated source index for configuration preflight.");
            Assert.That(File.ReadAllText(Path.Combine(cache,"index.json")),Is.EqualTo(json));
            Assert.That(File.ReadAllBytes(Path.Combine(cache,name)),Is.EqualTo(File.ReadAllBytes(asset)));
        }
        [UnityTest] public IEnumerator FailedExtractionDoesNotPublishIndexOrReadyRoot()
        {
            string index=Path.Combine(cache,"index.json"); File.WriteAllText(index,"previous validated index bytes");
            File.WriteAllText(Path.Combine(source,"index.json"),"{\"version\":\"q2-failure\",\"files\":[{\"path\":\""+name+"\",\"sha256\":\""+new string('0',64)+"\"}]}");
            string ready=null,error=null; yield return Complete(HumanVisionRuntimeData.Prepare(value=>ready=value,value=>error=value));
            Assert.That(ready,Is.Null); Assert.That(error,Is.Not.Null);
            Assert.That(File.ReadAllText(index),Is.EqualTo("previous validated index bytes"));
        }
        private static IEnumerator Complete(IEnumerator operation)
        {
            while(operation.MoveNext())
            {
                var pending=operation.Current as AsyncOperation;
                if(pending!=null) { while(!pending.isDone) yield return null; }
                else yield return operation.Current;
            }
        }
        private static string Hash(string path) { using(var stream=File.OpenRead(path)) using(var sha=SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant(); }
    }
}
