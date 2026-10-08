using System;
using System.Collections;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using HumanVision.Demo;
using UnityEngine;
using UnityEngine.Networking;
using UnityEngine.UI;

namespace HumanVision
{
    // This component is copied only into the isolated evaluation APK. Bytes are
    // independently verified offline fixture uploads; no image is read from GPU.
    [DefaultExecutionOrder(-210)]
    public sealed class TopDownEvalParitySource : MonoBehaviour
    {
        public const string ManifestSha256 = "REPLACE_MANIFEST_SHA256";
        [Serializable] private sealed class Artifact { public string file, sha256; public int byte_length; }
        [Serializable] private sealed class Artifacts { public Artifact rgba, tensor_fp32, tensor_fp16_rtz; }
        [Serializable] private sealed class Manifest {
            public int width, height, frame_index, rotation, source_width, source_height;
            public bool mirror; public string video_path, folder, fixture, source_sha256;
            public Artifacts artifacts; public Manifest[] cases;
        }
        [DllImport("humanvision", CallingConvention=CallingConvention.Cdecl)]
        private static extern int HV_R4ConfigureFixture(string root, string hash);
        [DllImport("humanvision", CallingConvention=CallingConvention.Cdecl)]
        private static extern int HV_R4ConfigureFixtureCase(string root, string hash,int caseIndex);
        [DllImport("humanvision", CallingConvention=CallingConvention.Cdecl)]
        private static extern int HV_R4ConfigureRun(int copyPath);
        private HumanVisionCameraManager _camera;
        private HumanVisionManager _manager;
        private VideoPlayerFrameSource _bridge;
        private Texture2D _upload;
        private RenderTexture _texture;
        private bool _leased;
        private RawImage[] _displays;
        private GameObject _label;
        private Material _orientation;
        private void Awake()
        {
            _camera=GetComponent<HumanVisionCameraManager>(); _camera.startAutomatically=false;
            _manager=GetComponent<HumanVisionManager>(); _bridge=GetComponent<VideoPlayerFrameSource>();
            _displays=FindObjectsOfType<RawImage>();
        }
        private static string Hash(byte[] bytes)
        {
            using(var sha=SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
        }
        private IEnumerator Start()
        {
            var root=Path.Combine(Application.persistentDataPath,"r4-parity"); Directory.CreateDirectory(root);
            string uri=Application.streamingAssetsPath.TrimEnd('/')+"/HumanVision/R4/";
            byte[] manifestBytes;
            using(var request=UnityWebRequest.Get(uri+"manifest.json")) {
                yield return request.SendWebRequest();
                if(request.result!=UnityWebRequest.Result.Success) throw new IOException(request.error);
                manifestBytes=request.downloadHandler.data;
            }
            if(Hash(manifestBytes)!=ManifestSha256) throw new InvalidDataException("R4 manifest hash mismatch");
            File.WriteAllBytes(Path.Combine(root,"manifest.json"),manifestBytes);
            var document=JsonUtility.FromJson<Manifest>(System.Text.Encoding.UTF8.GetString(manifestBytes));
            var manifest=document; int caseIndex=-1; string folder=""; Manifest sourceCase=null;
            if(document.cases!=null && document.cases.Length>0) {
                using(var player=new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
                using(var activity=player.GetStatic<AndroidJavaObject>("currentActivity"))
                using(var intent=activity.Call<AndroidJavaObject>("getIntent")) caseIndex=intent.Call<int>("getIntExtra","r4_case",0);
                if(caseIndex<0||caseIndex>=document.cases.Length) throw new InvalidDataException("R4 case index invalid");
                manifest=document.cases[caseIndex];
                if(Path.GetFileName(manifest.folder)!=manifest.folder) throw new InvalidDataException("R4 case folder invalid");
                folder=manifest.folder+"/"; Directory.CreateDirectory(Path.Combine(root,manifest.folder));
                foreach(var candidate in document.cases)
                    if(candidate.fixture==manifest.fixture && candidate.rotation==0 && !candidate.mirror) sourceCase=candidate;
                if(sourceCase==null || sourceCase.artifacts.rgba.sha256!=manifest.source_sha256)
                    throw new InvalidDataException("R4 original source identity missing");
            }
            byte[] rgba=null;
            foreach(var artifact in new[]{manifest.artifacts.rgba,manifest.artifacts.tensor_fp32,manifest.artifacts.tensor_fp16_rtz}) {
                if(Path.GetFileName(artifact.file)!=artifact.file) throw new InvalidDataException("R4 artifact filename invalid");
                using(var request=UnityWebRequest.Get(uri+folder+artifact.file)) {
                    yield return request.SendWebRequest();
                    if(request.result!=UnityWebRequest.Result.Success) throw new IOException(request.error);
                    var bytes=request.downloadHandler.data;
                    if(bytes.Length!=artifact.byte_length||Hash(bytes)!=artifact.sha256) throw new InvalidDataException("R4 artifact hash mismatch");
                    File.WriteAllBytes(Path.Combine(root,folder+artifact.file),bytes);
                    if(artifact==manifest.artifacts.rgba) rgba=bytes;
                }
            }
            if(sourceCase!=null) {
                using(var request=UnityWebRequest.Get(uri+sourceCase.folder+"/"+sourceCase.artifacts.rgba.file)) {
                    yield return request.SendWebRequest();
                    if(request.result!=UnityWebRequest.Result.Success) throw new IOException(request.error);
                    rgba=request.downloadHandler.data;
                    if(rgba.Length!=sourceCase.artifacts.rgba.byte_length||Hash(rgba)!=manifest.source_sha256)
                        throw new InvalidDataException("R4 original upload hash mismatch");
                }
            }
            int copyPath;
            using(var player=new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
            using(var activity=player.GetStatic<AndroidJavaObject>("currentActivity"))
            using(var intent=activity.Call<AndroidJavaObject>("getIntent")) copyPath=intent.Call<int>("getIntExtra","r4_copy_path",0);
            if(HV_R4ConfigureRun(copyPath)!=1) throw new InvalidDataException("R4 requested copy path invalid");
            int configured=caseIndex<0?HV_R4ConfigureFixture(root,ManifestSha256):HV_R4ConfigureFixtureCase(root,ManifestSha256,caseIndex);
            if(configured!=1) throw new InvalidOperationException("R4 native fixture rejected");
            while(!_camera.IsReady) yield return null;
            _upload=new Texture2D(sourceCase==null?manifest.width:sourceCase.width,sourceCase==null?manifest.height:sourceCase.height,TextureFormat.RGBA32,false,true);
            _upload.LoadRawTextureData(rgba); _upload.Apply(false,true);
            _upload.filterMode=FilterMode.Point; _upload.wrapMode=TextureWrapMode.Clamp;
            _texture=new RenderTexture(manifest.width,manifest.height,0,RenderTextureFormat.ARGB32,RenderTextureReadWrite.Linear);
            _texture.filterMode=FilterMode.Point; _texture.Create();
            if(sourceCase==null) Graphics.Blit(_upload,_texture);
            else {
                var shader=Resources.Load<Shader>("HumanVisionCameraOrientation");
                if(shader==null)throw new InvalidOperationException("Production orientation shader missing");
                _orientation=new Material(shader);
                // Raw fixture rows are top-down but Unity's UV y grows upward.
                // Conjugation by that origin reflection F gives F*R*F=R^-1.
                // One production shader pass implements the canonical rotation;
                // golden bytes and canonical metadata are never transformed here.
                int shaderRotation=(360-manifest.rotation)%360;
                _orientation.SetFloat("_Rotation",shaderRotation/90); _orientation.SetFloat("_FlipY",0);
                _orientation.SetFloat("_Mirror",manifest.mirror?1:0);
                Graphics.Blit(_upload,_texture,_orientation);
                Debug.Log("HV_R4_TRANSFORM case="+caseIndex+" canonical_rotation="+manifest.rotation+
                    " shader_rotation="+shaderRotation+" mirror="+manifest.mirror+" adapter=top_down_rows_to_unity_uv_F_R_F");
            }
            yield return new WaitForEndOfFrame();
            _bridge.ConfigureLiveInput(true,manifest.width,manifest.height);
            _manager.BeginAndroidGpuSourceLease(_texture); _leased=true;
            if(_displays.Length>0 && _displays[0].canvas!=null) {
                _label=new GameObject("Static Fixture Diagnostic Label",typeof(RectTransform),typeof(Text),typeof(Shadow));
                _label.transform.SetParent(_displays[0].canvas.transform,false);
                var rect=(RectTransform)_label.transform; rect.anchorMin=new Vector2(0,0); rect.anchorMax=new Vector2(1,0);
                rect.pivot=new Vector2(.5f,0); rect.anchoredPosition=new Vector2(0,12); rect.sizeDelta=new Vector2(0,48);
                var text=_label.GetComponent<Text>(); text.font=HumanVision.Input.HumanVisionUnityCompatibility.DefaultFont;
                text.fontSize=22; text.alignment=TextAnchor.MiddleCenter; text.color=Color.white; text.raycastTarget=false;
                text.text="STATIC VIDEO FRAME "+manifest.frame_index+" | "+
                    Path.GetFileName((manifest.video_path??"analytic fixture").Replace('\\','/'))+" | GPU INPUT DIAGNOSTIC";
            }
            Debug.Log("HV_R4_SOURCE manifest_sha256="+ManifestSha256+" rgba_sha256="+manifest.artifacts.rgba.sha256+
                " frame_index="+manifest.frame_index+" case="+caseIndex+" dimensions="+manifest.width+"x"+manifest.height+" route=static_gpu_upload");
            // Real timestamps at normal cadence let asynchronous detector results
            // be consumed within production freshness gates. Immutable fixtures
            // still cannot establish live/video performance or freshness.
            var interval=new WaitForSeconds(1f/30f);
            for(int i=0;i<(caseIndex<0?900:180);i++) {
                bool accepted=_bridge.SubmitExternalTexture(_texture,(long)(Time.realtimeSinceStartupAsDouble*1000000),manifest.rotation,manifest.mirror);
                Debug.Log("HV_R4_SUBMIT iteration="+i+" accepted="+accepted);
                yield return interval;
            }
            Debug.Log("HV_R4_STATIC_DONE");
        }
        private void LateUpdate()
        {
            // Offline fixture rows are top-to-bottom. The inference upload keeps
            // that byte contract; Unity's UI UV origin needs a display-only flip.
            if(_texture==null)return;
            foreach(var display in _displays)
                if(display!=null && display.texture==_texture) display.uvRect=new Rect(0,1,1,-1);
        }
        private void OnDestroy()
        {
            if(_leased) { _manager.EndAndroidGpuSourceLease(); _leased=false; }
            if(_texture!=null) { _texture.Release(); Destroy(_texture); }
            if(_upload!=null) Destroy(_upload);
            if(_label!=null) Destroy(_label);
            if(_orientation!=null) Destroy(_orientation);
        }
    }
}
