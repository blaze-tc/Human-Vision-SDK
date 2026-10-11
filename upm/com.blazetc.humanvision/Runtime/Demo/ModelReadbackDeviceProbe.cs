using System;
using System.Collections;
using Unity.Collections;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVision.Demo
{
    /// <summary>Explicit diagnostic fixtures only. These pixels never enter recognition
    /// or preview and cannot be counted as a successful skeleton observation.</summary>
    public static class ModelReadbackDeviceProbe
    {
        public static IEnumerator Run(Action<string,bool> report)
        {
            var shader=Resources.Load<Shader>("HumanVisionExactRgbReadback");
            if(!SystemInfo.supportsAsyncGPUReadback || shader==null || !shader.isSupported) {
                report("model RGB probe unsupported on this graphics API",false);yield break;
            }
            foreach(int width in new[]{640,1024,1280}) {
                int height=width*9/16;
                var input=new Texture2D(width,height,TextureFormat.RGBA32,false,true);
                var baseline=new RenderTexture(width,height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.sRGB);
                var candidate=new RenderTexture(512,288,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);
                var material=new Material(shader);
                var original=new NativeArray<byte>(width*height*4,Allocator.Persistent);
                var smaller=new NativeArray<byte>(512*288*4,Allocator.Persistent);
                try {
                    baseline.Create();candidate.Create();
                    var bytes=new byte[width*height*4];for(int i=0;i<bytes.Length;i++)bytes[i]=(byte)((i*71+(i/31)*113)&255);
                    input.LoadRawTextureData(bytes);input.Apply();
                    // Test both physical horizontal orientations. Do not derive the
                    // expected result from the shader being tested.
                    foreach(bool mirror in new[]{false,true}) {
                        Graphics.Blit(input,baseline,mirror?new Vector2(-1,1):Vector2.one,mirror?new Vector2(1,0):Vector2.zero);
                        Graphics.Blit(baseline,candidate,material);
                        // A completed request expires one Unity frame later. Capture
                        // each status in its callback; a slower peer must not turn a
                        // successful request into a false readback error.
                        bool aDone=false,bDone=false,aError=false,bError=false;
                        AsyncGPUReadback.RequestIntoNativeArray(ref original,baseline,0,TextureFormat.RGBA32,
                            request=>{aError=request.hasError;aDone=true;});
                        AsyncGPUReadback.RequestIntoNativeArray(ref smaller,candidate,0,TextureFormat.RGBA32,
                            request=>{bError=request.hasError;bDone=true;});
                        while(!aDone || !bDone)yield return null;
                        int different=-1,maximum=-1;
                        bool exact=!aError&&!bError&&ExactModelReadback.Equal(original,width,height,smaller,out different,out maximum);
                        report("model RGB "+width+"x"+height+" mirror="+mirror+" differences="+different+" maximum="+maximum,exact);
                    }
                } finally {
                    AsyncGPUReadback.WaitAllRequests();original.Dispose();smaller.Dispose();baseline.Release();candidate.Release();
                    Destroy(input);Destroy(baseline);Destroy(candidate);Destroy(material);
                }
            }
        }
        private static void Destroy(UnityEngine.Object value) {
            if(Application.isPlaying)UnityEngine.Object.Destroy(value);else UnityEngine.Object.DestroyImmediate(value);
        }
    }
}
