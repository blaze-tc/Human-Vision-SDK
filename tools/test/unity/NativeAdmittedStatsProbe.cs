using System;
using System.Reflection;
using System.Threading;
using HumanVision;
class NativeStatsProbe {
 const BindingFlags Fields=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
 static object Call(object owner,string name, params object[] args){return owner.GetType().GetMethod(name,Fields).Invoke(owner,args);}
 static int Main(string[] args){
  var assembly=typeof(HumanVisionConfig).Assembly;
  var config=new HumanVisionConfig{RuntimeRoot=args[0],Profile="cpu",MaxBodies=1,ExecutionPolicy=HumanVisionExecutionPolicy.IndependentRequired};
  var sourceType=assembly.GetType("HumanVision.HumanVisionNativeSourceConfiguration");
  var source=Activator.CreateInstance(sourceType,true);
  sourceType.GetField("Library",Fields).SetValue(source,args[1]);sourceType.GetField("Url",Fields).SetValue(source,args[2]);
  sourceType.GetField("Width",Fields).SetValue(source,512u);sourceType.GetField("Height",Fields).SetValue(source,288u);
  sourceType.GetField("Preview",Fields).SetValue(source,false);
  typeof(HumanVisionConfig).GetField("NativeSource",Fields).SetValue(config,source);
  var sessionType=assembly.GetType("HumanVision.HumanVisionRuntimeSession");
  var session=Activator.CreateInstance(sessionType,Fields,null,new object[]{config},null);
  try{
   var init=new object[]{null};var deadline=DateTime.UtcNow.AddSeconds(30);
   while(!(bool)Call(session,"PollInitialization",init)){if(!string.IsNullOrEmpty((string)init[0]))throw new Exception((string)init[0]);if(DateTime.UtcNow>deadline)throw new Exception("init timeout");Thread.Sleep(10);}
   var progressArgs=new object[]{null};HumanVisionRuntimeProgress progress=default;
   while(DateTime.UtcNow<deadline){if((bool)Call(session,"TryGetRuntimeProgress",progressArgs)){progress=(HumanVisionRuntimeProgress)progressArgs[0];if(progress.NativeFreshFrames>0&&progress.NativeInputFrames>0)break;}Thread.Sleep(10);}
   if(progress.NativeFreshFrames==0||progress.NativeInputFrames==0)throw new Exception("No native RTSP admission/publication was observed");
   for(int i=0;i<20;i++){
    Call(session,"PollLatestResult");Call(session,"RefreshStats");
    var control=sessionType.GetField("_control",Fields).GetValue(session);
    var native=control.GetType().GetProperty("LastStats",Fields).GetValue(control);
    ulong admitted=(ulong)native.GetType().GetField("NativeInputFrames",Fields).GetValue(native);
    var stats=(HumanVisionStats)sessionType.GetProperty("Stats").GetValue(session);
    Console.WriteLine("native_admitted="+admitted+" public_submitted="+stats.SubmittedFrames);
    if(admitted==0||stats.SubmittedFrames!=(long)admitted)throw new Exception("public submitted counter does not match this native admission snapshot");
   }
   Console.WriteLine("PASS: 20 raw/sample/stat reads preserve the authoritative native admission counter");return 0;
  }catch(Exception error){Console.Error.WriteLine(error);return 1;}
  finally{Call(session,"RequestClose");var end=DateTime.UtcNow.AddSeconds(15);while(!(bool)Call(session,"TryCompleteClose")){if(DateTime.UtcNow>end)throw new Exception("retirement timeout; owners retained");Thread.Sleep(10);}}
 }
}
