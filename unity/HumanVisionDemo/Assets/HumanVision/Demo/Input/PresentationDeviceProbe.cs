using System;
using System.Collections;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.Rendering;

namespace HumanVision.Demo {
    // Explicit device diagnostics only: fixtures never enter the recognition pipeline or its FPS counters.
    internal static class PresentationDeviceProbe {
        private sealed class Source : IHumanVisionFrameSource {
            internal readonly SourceRetirement retirement=new SourceRetirement();
            private readonly SourceGeneration owner;
            internal readonly RenderTexture texture;
            internal readonly HumanVisionTextureFrame frame;
            internal Source(RenderTexture value){texture=value;owner=new SourceGeneration(retirement);owner.BeginGeneration();
                var token=retirement.Register(owner.Generation,texture,()=>{});
                frame=new HumanVisionTextureFrame(owner.SourceId,owner.Generation,1,texture,1,-1,0,false,FrameRowOrigin.UnityBottomLeft,FrameColorSpace.Srgb,FrameTimestampKind.UnityObserved,token);
                owner.TryPublish(in frame);}
            public InputSourceState State=>InputSourceState.Streaming;
            public string LastError=>"";
            public Texture CurrentTexture=>texture;
            public void Open(HumanVisionSourceSettings settings){}
            public void Close(){}
            public bool TryGetLatestFrame(long after,out HumanVisionTextureFrame value){value=frame;return after<1;}
            public bool TryAcquireSourceCopyLease(in HumanVisionTextureFrame value,out SourceCopyLease lease)=>owner.TryAcquireSourceCopyLease(in value,out lease);
        }
        internal static IEnumerator Run(Action<string,bool> report){
            if(!SystemInfo.supportsGraphicsFence||!SystemInfo.supportsAsyncGPUReadback){report("presentation GPU fixture unsupported",false);yield break;}
            foreach(int width in new[]{640,960,1280,1920})foreach(bool mirror in new[]{false,true}){
                int height=width*9/16;
                var input=new Texture2D(width,height,TextureFormat.RGBA32,false,true);
                var baseline=new RenderTexture(width,height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Default);baseline.Create();
                var history=new PresentationTextureHistory();
                var source=new Source(baseline);
                try{
                    var data=new byte[width*height*4];for(int i=0;i<data.Length;i++)data[i]=(byte)((i*71+i/31*113)&255);
                    input.LoadRawTextureData(data);input.Apply();
                    Graphics.Blit(input,baseline,mirror?new Vector2(-1,1):Vector2.one,mirror?Vector2.right:Vector2.zero);
                    history.Capture(source,in source.frame,1);
                    double end=Time.realtimeSinceStartupAsDouble+5;
                    while(history.Texture==null&&Time.realtimeSinceStartupAsDouble<end){source.retirement.Poll();history.Present(1);yield return null;}
                    if(history.Texture==null){report("presentation fence timed out",false);yield break;}
                    byte[] a=null,b=null;bool doneA=false,doneB=false;
                    AsyncGPUReadback.Request(baseline,0,TextureFormat.RGBA32,r=>{if(!r.hasError)a=r.GetData<byte>().ToArray();doneA=true;});
                    AsyncGPUReadback.Request(history.Texture,0,TextureFormat.RGBA32,r=>{if(!r.hasError)b=r.GetData<byte>().ToArray();doneB=true;});
                    while(!doneA||!doneB)yield return null;
                    int differences=0;if(a==null||b==null||a.Length!=b.Length)differences=-1;
                    else for(int i=0;i<a.Length;i++)if(a[i]!=b[i])differences++;
                    report("presentation pixels "+width+"x"+height+" mirror="+mirror+" differences="+differences,differences==0);
                    source.retirement.Poll();
                }finally{history.Dispose();baseline.Release();Destroy(baseline);Destroy(input);}
            }
        }
        private static void Destroy(UnityEngine.Object value) {
            // The same pixel probe can run from EditMode tests or an actual player.
            if(Application.isPlaying)UnityEngine.Object.Destroy(value);else UnityEngine.Object.DestroyImmediate(value);
        }
    }
}
