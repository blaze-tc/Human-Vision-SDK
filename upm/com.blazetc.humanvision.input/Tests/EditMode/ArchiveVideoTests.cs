using System;
using System.IO;
using System.Reflection;
using System.Collections;
using NUnit.Framework;
using UnityEngine.TestTools;
namespace HumanVision.Input.Tests {
public sealed class ArchiveVideoTests {
    private static MethodInfo Method(string name) {
        var type=typeof(VideoFrameSource).Assembly.GetType("HumanVision.Input.ArchiveVideoCache");
        Assert.That(type,Is.Not.Null,"APK videos need a streamed local-file cache before VideoPlayer opens them.");
        return type.GetMethod(name,BindingFlags.Static|BindingFlags.NonPublic|BindingFlags.Public);
    }
    [TestCase("jar:file:///data/app/base.apk!/assets/video.mp4",true)]
    [TestCase("https://host/video.mp4",false)]
    [TestCase("/storage/emulated/0/video.mp4",false)]
    public void OnlyArchiveUrlsRequireExtraction(string value,bool expected) {
        Assert.That(Method("RequiresExtraction").Invoke(null,new object[]{value}),Is.EqualTo(expected));
    }
    [Test] public void CacheIdentityIsBoundToCompleteArchiveUrlAndCannotEscapeRoot() {
        var root=Path.Combine(Path.GetTempPath(),"hv-video-cache-test");
        string first=(string)Method("Destination").Invoke(null,new object[]{root,"jar:file:///a.apk!/assets/../中文.mp4"});
        string second=(string)Method("Destination").Invoke(null,new object[]{root,"jar:file:///b.apk!/assets/../中文.mp4"});
        Assert.That(first.StartsWith(Path.GetFullPath(root)+Path.DirectorySeparatorChar),Is.True);
        Assert.That(Path.GetExtension(first),Is.EqualTo(".mp4"));Assert.That(first,Is.Not.EqualTo(second));
    }
    [Test] public void MissingTruncatedOrUncommittedFileIsNeverACompleteCache() {
        string path=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".mp4");
        try {
            Assert.That(Method("IsComplete").Invoke(null,new object[]{path}),Is.False);
            File.WriteAllBytes(path,new byte[12]);Assert.That(Method("IsComplete").Invoke(null,new object[]{path}),Is.False);
            File.WriteAllText(path+".length","13");Assert.That(Method("IsComplete").Invoke(null,new object[]{path}),Is.False);
            File.WriteAllText(path+".length","12");Assert.That(Method("IsComplete").Invoke(null,new object[]{path}),Is.True);
        } finally {File.Delete(path);File.Delete(path+".length");}
    }
    [UnityTest] public IEnumerator StreamedTransferCommitsActualBytesAndCancellationLeavesNoReceipt() {
        string root=Path.Combine(Path.GetTempPath(),"hv-video-test-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        string input=Path.Combine(root,"source.mp4"), output=Path.Combine(root,"copied.mp4");
        var type=typeof(VideoFrameSource).Assembly.GetType("HumanVision.Input.ArchiveVideoCache+Transfer");
        object transfer=null;
        try {
            byte[] bytes=new byte[262144];for(int i=0;i<bytes.Length;i++)bytes[i]=(byte)(i*71+13);File.WriteAllBytes(input,bytes);
            transfer=Activator.CreateInstance(type,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{new Uri(input).AbsoluteUri,output},null);
            var done=type.GetProperty("IsDone",BindingFlags.Instance|BindingFlags.NonPublic);
            double deadline=UnityEngine.Time.realtimeSinceStartupAsDouble+10;
            while(!(bool)done.GetValue(transfer) && UnityEngine.Time.realtimeSinceStartupAsDouble<deadline)yield return null;
            Assert.That((bool)done.GetValue(transfer),Is.True);
            type.GetMethod("Commit",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(transfer,null);
            Assert.That(File.ReadAllBytes(output),Is.EqualTo(bytes));
            Assert.That(Method("IsComplete").Invoke(null,new object[]{output}),Is.True);
            ((IDisposable)transfer).Dispose();transfer=null;
            string cancelled=Path.Combine(root,"cancelled.mp4");
            transfer=Activator.CreateInstance(type,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{new Uri(input).AbsoluteUri,cancelled},null);
            ((IDisposable)transfer).Dispose();transfer=null;
            Assert.That(File.Exists(cancelled+".length"),Is.False);Assert.That(Directory.GetFiles(root,"*.part"),Is.Empty);
        } finally {
            (transfer as IDisposable)?.Dispose();
            // Only this test's newly created leaf directory is removed.
            foreach(string file in Directory.GetFiles(root))File.Delete(file);Directory.Delete(root);
        }
    }
}
}
