using System;
using System.Threading;
using NUnit.Framework;
using UnityEngine;
namespace HumanVision.Input.Tests
{
    public class UnitySourceTests
    {
        [Test] public void NormalizerPreservesCallerActiveTarget()
        {
            var input = new Texture2D(4,4);
            var caller = new RenderTexture(4,4,0); caller.Create();
            var normalizer = new FrameTextureNormalizer();
            var previous = RenderTexture.active;
            try
            {
                RenderTexture.active = caller;
                normalizer.Update(input,0,false,false);
                Assert.AreSame(caller,RenderTexture.active,"Normalization must preserve the caller's render target.");
            }
            finally
            {
                RenderTexture.active = previous;
                normalizer.Dispose();
                UnityEngine.Object.DestroyImmediate(input); UnityEngine.Object.DestroyImmediate(caller);
            }
        }
        [Test] public void DisposeClearsOnlyOwnedActiveTarget()
        {
            var input = new Texture2D(4,4);
            var normalizer = new FrameTextureNormalizer();
            var previous = RenderTexture.active;
            try
            {
                RenderTexture.active = normalizer.Update(input,0,false,false);
                normalizer.Dispose();
                Assert.IsNull(RenderTexture.active,"An owned retired target must not remain bound.");
            }
            finally
            {
                RenderTexture.active = previous;
                normalizer.Dispose(); UnityEngine.Object.DestroyImmediate(input);
            }
        }
        [Test] public void AsymmetricMarkerTransformsExactlyOnce()
        {
            Debug.Log($"INPUT_GRAPHICS api={SystemInfo.graphicsDeviceType} fences={SystemInfo.supportsGraphicsFence}");
            var input = new Texture2D(6,4,TextureFormat.RGBA32,false,true);
            var pixels = new Color[24];
            for(int y=0;y<4;y++) for(int x=0;x<6;x++) pixels[y*6+x]=new Color((x+1)/8f,(y+1)/6f,(x+y+1)/12f,1);
            input.SetPixels(pixels);
            input.Apply();
            var normalizer=new FrameTextureNormalizer();
            try
            {
                foreach(int rotation in new[]
                {
                    0,90,180,270
                }
                ) foreach(bool vertical in new[]
                {
                    false,true
                }
                ) foreach(bool mirror in new[]
                {
                    false,true
                }
                )
                {
                    var rt=normalizer.Update(input,rotation,vertical,mirror);
                    Assert.AreEqual(rotation%180==0?6:4,rt.width);
                    Assert.AreEqual(rotation%180==0?4:6,rt.height);
                    var read=new Texture2D(rt.width,rt.height,TextureFormat.RGBA32,false,true);
                    var old=RenderTexture.active;
                    RenderTexture.active=rt;
                    read.ReadPixels(new Rect(0,0,rt.width,rt.height),0,0);
                    read.Apply();
                    RenderTexture.active=old;
                    for(int y=0;y<rt.height;y++) for(int x=0;x<rt.width;x++)
                    {
                        int ox=mirror?rt.width-1-x:x, sx=ox,sy=y;
                        if(rotation==90)
                        {
                            sx=5-y;
                            sy=ox;
                        }
                        else if(rotation==180)
                        {
                            sx=5-ox;
                            sy=3-y;
                        }
                        else if(rotation==270)
                        {
                            sx=y;
                            sy=3-ox;
                        }
                        if(vertical)sy=3-sy;
                        Color expected=pixels[sy*6+sx],actual=read.GetPixel(x,y);
                        Assert.That(actual.r,Is.EqualTo(expected.r).Within(.015),$"r={rotation},v={vertical},m={mirror},xy={x},{y}");
                        Assert.That(actual.g,Is.EqualTo(expected.g).Within(.015));
                        Assert.That(actual.b,Is.EqualTo(expected.b).Within(.015));
                    }
                    UnityEngine.Object.DestroyImmediate(read);
                }
            }
            finally
            {
                normalizer.Dispose();
                UnityEngine.Object.DestroyImmediate(input);
            }
        }
        [Test] public void MidtoneEncodingMatchesGpuPixels()
        {
            foreach (bool linear in new[] {false,true})
            {
                var input = new Texture2D(4,4,TextureFormat.RGBA32,false,linear);
                var color = new Color(.2f,.5f,.7f,1);
                var pixels = new Color[16];
                for(int i=0;i<pixels.Length;i++) pixels[i]=color;
                input.SetPixels(pixels); input.Apply();
                var normalizer = new FrameTextureNormalizer();
                var rt = normalizer.Update(input,0,false,false);
                var read = new Texture2D(4,4,TextureFormat.RGBA32,false,true);
                var previous = RenderTexture.active; RenderTexture.active=rt;
                read.ReadPixels(new Rect(0,0,4,4),0,0); read.Apply(); RenderTexture.active=previous;
                var expected = !linear && QualitySettings.activeColorSpace==ColorSpace.Linear ? color.linear : color;
                var actual = read.GetPixel(0,0);
                Assert.That(actual.r,Is.EqualTo(expected.r).Within(.015));
                Assert.That(actual.g,Is.EqualTo(expected.g).Within(.015));
                Assert.That(actual.b,Is.EqualTo(expected.b).Within(.015));
                Assert.AreEqual(QualitySettings.activeColorSpace==ColorSpace.Gamma && !linear ? FrameColorSpace.Srgb : FrameColorSpace.Linear,normalizer.OutputColorSpace);
                normalizer.Dispose(); UnityEngine.Object.DestroyImmediate(input); UnityEngine.Object.DestroyImmediate(read);
            }
        }
        [Test] public void NormalizerRejectsWorkerThread()
        {
            var normalizer=new FrameTextureNormalizer();
            Exception failure=null;
            var thread=new Thread(()=>
            {
                try
                {
                    normalizer.Dispose();
                }
                catch(Exception ex)
                {
                    failure=ex;
                }
            }
            );
            thread.Start();
            thread.Join();
            Assert.IsInstanceOf<InvalidOperationException>(failure);
            normalizer.Dispose();
        }
        [Test] public void RequestedResolutionIsNotActualResolution()
        {
            var input=new Texture2D(32,18);
            var n=new FrameTextureNormalizer();
            try
            {
                Assert.AreEqual(32,n.Update(input,0,false,false).width);
                Assert.AreEqual(18,n.CurrentTexture.height);
            }
            finally
            {
                n.Dispose();
                UnityEngine.Object.DestroyImmediate(input);
            }
        }
    }
}
