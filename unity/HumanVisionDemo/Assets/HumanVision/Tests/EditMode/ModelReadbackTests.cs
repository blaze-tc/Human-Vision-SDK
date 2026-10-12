using HumanVision.Demo;
using NUnit.Framework;
using Unity.Collections;
using UnityEngine;
using UnityEngine.Rendering;
namespace HumanVision.Tests {
public sealed class ModelReadbackTests {
    [UnityEngine.TestTools.UnityTest]
    public System.Collections.IEnumerator DeviceProbeCompletesBothMirrorsAcrossUnityFrames() {
        int completed=0;
        yield return ModelReadbackDeviceProbe.Run((detail,passed)=>{
            Assert.That(passed,Is.True,detail);completed++;
        });
        // Six model-resize comparisons plus eight presentation comparisons (four sizes, both mirrors).
        Assert.That(completed,Is.EqualTo(14));
    }
    [Test] public void ResizeQualificationRequiresThreeExactFramesAndRejectsAnyMismatch() {
        var type=typeof(VideoPlayerFrameSource).Assembly.GetType("HumanVision.Demo.ModelReadbackQualification");
        Assert.That(type,Is.Not.Null,"Resized readback needs a same-frame pixel gate on the actual device.");
        var gate=System.Activator.CreateInstance(type,true);
        var observe=type.GetMethod("Observe",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic);
        var qualified=type.GetProperty("Qualified",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic);
        observe.Invoke(gate,new object[]{true}); observe.Invoke(gate,new object[]{true});
        Assert.That(qualified.GetValue(gate),Is.False);
        observe.Invoke(gate,new object[]{true}); Assert.That(qualified.GetValue(gate),Is.True);
        gate=System.Activator.CreateInstance(type,true);
        observe.Invoke(gate,new object[]{true}); observe.Invoke(gate,new object[]{false});
        for(int i=0;i<4;i++)observe.Invoke(gate,new object[]{true});
        Assert.That(qualified.GetValue(gate),Is.False,"A failed comparison cannot be averaged away.");
    }
    [TestCase(640,360)] [TestCase(1024,576)] [TestCase(1280,720)]
    public void ExactShaderPreservesEveryModelRgbByte(int width,int height) {
        if(!SystemInfo.supportsAsyncGPUReadback)Assert.Ignore("GPU readback unavailable; this is not a passing pixel gate.");
        var shader=Resources.Load<Shader>("HumanVisionExactRgbReadback");Assert.That(shader,Is.Not.Null);Assert.That(shader.isSupported,Is.True);
        var input=new Texture2D(width,height,TextureFormat.RGBA32,false,true);
        var baseline=new RenderTexture(width,height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.sRGB);baseline.Create();
        var candidate=new RenderTexture(512,288,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);candidate.Create();
        var material=new Material(shader);
        var original=new NativeArray<byte>(width*height*4,Allocator.Persistent);
        var smaller=new NativeArray<byte>(512*288*4,Allocator.Persistent);
        try {
            var pixels=new byte[width*height*4];for(int i=0;i<pixels.Length;i++)pixels[i]=(byte)((i*71+(i/31)*113)&255);
            input.LoadRawTextureData(pixels);input.Apply();Graphics.Blit(input,baseline);Graphics.Blit(baseline,candidate,material);
            var a=AsyncGPUReadback.RequestIntoNativeArray(ref original,baseline,0,TextureFormat.RGBA32);
            var b=AsyncGPUReadback.RequestIntoNativeArray(ref smaller,candidate,0,TextureFormat.RGBA32);
            AsyncGPUReadback.WaitAllRequests();Assert.That(a.hasError||b.hasError,Is.False);
            Assert.That(ExactModelReadback.Equal(original,width,height,smaller,out int differences,out int maximum),Is.True,
                "Complete model RGB tensor differs: bytes="+differences+", maximum="+maximum+", source="+original[0]+","+original[1]+","+original[2]+", candidate="+smaller[0]+","+smaller[1]+","+smaller[2]+"; mid="+smaller[295000]+"; texel="+material.GetVector("_MainTex_TexelSize"));
        } finally {
            AsyncGPUReadback.WaitAllRequests();original.Dispose();smaller.Dispose();baseline.Release();candidate.Release();
            Object.DestroyImmediate(baseline);Object.DestroyImmediate(candidate);Object.DestroyImmediate(input);Object.DestroyImmediate(material);
        }
    }
}}
