// Actual sample composition is compiled separately. These are draw I/O boundaries.
using System;
namespace PreviewContract
{
    internal struct Rect {internal float x,y,width,height;internal float xMax=>x+width;internal float yMax=>y+height;internal Rect(float x,float y,float w,float h){this.x=x;this.y=y;width=w;height=h;}}
    internal static class Screen{internal static float width=1440;internal static Rect safeArea=new Rect(0,0,1440,866);}
    internal static class Mathf{internal static float Max(float a,float b)=>Math.Max(a,b);internal static float Clamp(float x,float min,float max)=>Math.Min(max,Math.Max(min,x));}
    internal struct Vector2{}
    internal static class GUI{internal static bool enabled=true;internal static readonly System.Collections.Generic.List<Rect> Viewports=new System.Collections.Generic.List<Rect>();internal static int Ends;internal static Vector2 BeginScrollView(Rect viewport,Vector2 position,Rect content,bool horizontal,bool vertical){Viewports.Add(viewport);return position;}internal static void EndScrollView(){++Ends;}internal static void Reset(){Viewports.Clear();Ends=0;enabled=true;}}
    internal sealed class Texture{}
    internal sealed class Configuration{internal bool UseRegions=true;}
    internal sealed partial class Demo
    {
        internal Texture live=new Texture(),SeenTexture;internal bool showSkeleton=true;private string inputLine="input",poseLine="pose";internal Configuration recognition=new Configuration();
        internal int Cards,FullscreenButtons,CameraDraws,RegionDraws,BoneDraws;internal bool CardRegions,CardSkeleton;internal Rect CameraRect,RegionRect,BoneRect,CardRect,SidebarRect;
        private sealed class Skeleton {private readonly Demo owner;internal Skeleton(Demo owner){this.owner=owner;}internal int VisibleBodies=>4;internal void Draw(Rect image,float width){++owner.BoneDraws;owner.BoneRect=image;}}
        private readonly Skeleton liveSkeleton;
        internal Demo(){liveSkeleton=new Skeleton(this);}
        private Vector2 sidebarScroll,sidebarLogScroll;private float sidebarContentHeight=1600,sidebarLogContentHeight=1000;
        private static readonly object Panel=new object(),Inset=new object();private string status="status",sdkLine="sdk";private int visibleJoints=96,visibleBones=92;
        private sealed class Telemetry{internal string PerformanceLine="source 30 Texture 30 infer 10",ReadLine="read",StageLine="stages";}
        private readonly Telemetry telemetry=new Telemetry();internal bool ThrowChoices;
        internal enum HumanVisionSdkState{Running,Error}
        internal sealed class Sdk{internal string PreviewError="",LastError="";internal HumanVisionSdkState State=HumanVisionSdkState.Running;}
        internal sealed class View{internal readonly Sdk Sdk=new Sdk();}
        internal View view;
        internal readonly System.Collections.Generic.List<string> LogTexts=new System.Collections.Generic.List<string>();
        private void Box(Rect rect,object color){}
        private void DrawSidebarChoices(float width,ref float y,ref bool handled){if(ThrowChoices)throw new InvalidOperationException("controlled GUI callback failure");handled=true;y=1200;}
        private void DrawStandaloneSidebarChoices(float width,ref float y){y=1200;}
        private void DrawSidebarRuntimeActions(float width,ref float y){y+=400;}
        private void DrawSidebarText(string text,float width,ref float y,bool section=false){LogTexts.Add(text);y+=40;}
        private void DrawCard(Rect card,string title,Texture texture,string detail,bool showRegions=false,bool drawSkeleton=false){++Cards;CardRect=card;SeenTexture=texture;CardRegions=showRegions;CardSkeleton=drawSkeleton;DrawPreviewImage(card,texture,showRegions,drawSkeleton);}
        private void DrawFullscreenButton(Rect card){++FullscreenButtons;}
        private void DrawCameraImage(Rect image,Texture texture){++CameraDraws;CameraRect=image;}
        private void DrawRegions(Rect image){++RegionDraws;RegionRect=image;}
    }
    internal static class Program
    {
        private static int tests,failed;
        private static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
        private static void Run(string name,Action test){++tests;try{test();Console.WriteLine("PASS "+name);}catch(Exception e){++failed;Console.WriteLine("FAIL "+name+": "+e.Message);}}
        public static int Main(){
            Run("OnePreviewSurfaceUsesTheOriginalLiveTexture",()=>{var d=new Demo();d.DrawLayout();Check(d.Cards==1&&ReferenceEquals(d.SeenTexture,d.live),"ordinary GUI must display the borrowed live Texture exactly once");Check(d.FullscreenButtons==1,"one preview has one fullscreen action");});
            Run("RegionsAndSkeletonShareTheImageRectangle",()=>{var d=new Demo();d.DrawLayout();Check(d.CameraDraws==1&&d.RegionDraws==1&&d.BoneDraws==1,"one image must carry both overlays");Check(d.CameraRect.Equals(d.RegionRect)&&d.CameraRect.Equals(d.BoneRect),"all layers must use the same image rectangle");});
            Run("SkeletonToggleDoesNotHideTheRegionOverlay",()=>{var d=new Demo{showSkeleton=false};d.DrawLayout();Check(d.CameraDraws==1&&d.RegionDraws==1&&d.BoneDraws==0,"skeleton and region choices remain independent");});
            Run("RegionSettingDoesNotHideSdkSkeleton",()=>{var d=new Demo();d.recognition.UseRegions=false;d.DrawLayout();Check(d.CameraDraws==1&&d.RegionDraws==0&&d.BoneDraws==1,"UseRegions only controls the region overlay");});
            Run("MissingTextureDoesNotHideValidSdkCoordinates",()=>{var d=new Demo{live=null};d.DrawLayout();Check(d.CameraDraws==0&&d.RegionDraws==1&&d.BoneDraws==1,"texture availability is not an SDK geometry admission gate");});
            Run("FullscreenKeepsRegionsWithoutTextureOrSkeleton",()=>{var d=new Demo{live=null,showSkeleton=false};d.DrawFullscreenContent();Check(d.CameraDraws==0&&d.RegionDraws==1&&d.BoneDraws==0,"enabled regions must also be visible in fullscreen");});
            Run("PreviewStaysToTheRightOfTheControlSidebar",()=>{var d=new Demo();d.DrawLayout();Check(d.CardRect.x>=d.SidebarRect.xMax,"preview must start after the left sidebar, rather than span both columns");Check(d.CardRect.height>=700,"preview must extend through the available vertical area");});
            Run("ImageWindowUsesTheTallPreviewCard",()=>{var d=new Demo();d.DrawLayout();var image=d.ImageWindow();Check(image.height>=500&&image.height<d.CardRect.height,"the preview cannot retain the old fixed 365 pixel image window");});
            Run("LogsStayVisibleOutsideTheOptionsScroll",()=>{GUI.Reset();var d=new Demo();d.DrawSidebarFrame();Check(GUI.Viewports.Count==2&&GUI.Ends==2,"options and logs require independent balanced scroll views");var options=GUI.Viewports[0];var logs=GUI.Viewports[1];Check(options.yMax<=logs.y&&logs.height>=200&&logs.yMax<=838,"long options must not push the fixed visible log viewport below the sidebar");});
            Run("FailedSidebarCallbackStillClosesItsScrollScope",()=>{GUI.Reset();var d=new Demo{ThrowChoices=true};bool failed=false;try{d.DrawSidebarFrame();}catch(InvalidOperationException){failed=true;}Check(failed&&GUI.Viewports.Count==1&&GUI.Ends==1&&GUI.enabled,"a failed GUI callback must leave clipping and enabled state restored");});
            Run("OriginalPreviewFailureIsVisibleInTheFixedLogPane",()=>{GUI.Reset();var d=new Demo{live=null,view=new Demo.View()};d.view.Sdk.PreviewError="Original-resolution GPU preview unavailable";d.DrawSidebarFrame();Check(d.LogTexts.Exists(text=>text.Contains("Original-resolution GPU preview unavailable")),"healthy inference with unavailable original preview needs an explicit visible reason");});
            Console.WriteLine("RESULT total="+tests+" passed="+(tests-failed)+" failed="+failed);return failed==0?0:1;
        }
    }
}
