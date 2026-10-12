using NUnit.Framework;
using System;
using System.Linq;
namespace HumanVision.TestProject.Diagnostics.Tests {
 public sealed class InferenceInstanceTests {
  private static object Read(string value,bool initialized) {
   var type=AppDomain.CurrentDomain.GetAssemblies().Select(a=>a.GetType("HumanVision.TestProject.Diagnostics.InferenceInstanceSnapshot")).First(t=>t!=null);
   return type.GetMethod("Read").Invoke(null,new object[]{value,initialized});
  }
  private static object Field(object value,string name)=>value.GetType().GetField(name).GetValue(value);
  [TestCase(0,3)] [TestCase(2,3)] [TestCase(1,1)]
  public void ReadsActualBusyAndTotal(int busy,int total) {
   var s=Read("Inference instances="+total+"\nActive inference instances="+busy+"\n",true);
   Assert.AreEqual(busy,Field(s,"busy"));Assert.AreEqual(total,Field(s,"total"));
  }
  [Test] public void MissingCountersAreUnavailableAndStoppedIsZero() {
   Assert.AreEqual(-1,Field(Read("Profile=test",true),"total"));
   Assert.AreEqual(-1,Field(Read("Inference instances=1\nActive inference instances=2",true),"total"));
   Assert.AreEqual(0,Field(Read(null,false),"total"));
  }
  [Test] public void FallbackReportsActualCreatedCount() {
   var s=Read("Inference instances=1\nActive inference instances=1\nInstance pool fallback=second context failed",true);
   Assert.AreEqual(1,Field(s,"total"));Assert.AreEqual("second context failed",Field(s,"fallback"));
  }
 }
}
