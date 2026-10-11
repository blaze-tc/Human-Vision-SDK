using System;
using System.Reflection;
using System.Linq;
using NUnit.Framework;
namespace HumanVision.TestProject.Diagnostics.Tests {
public sealed class FreshSkeletonCounterTests {
    [Test] public void DisabledComparisonRestoresInteractabilityEvenWithoutCoroutineCompletion() {
        var type=AppDomain.CurrentDomain.GetAssemblies().Select(a=>a.GetType("HumanVision.TestProject.Diagnostics.HumanVisionCombinedBenchmark")).First(t=>t!=null);
        var root=new UnityEngine.GameObject("benchmark lifecycle test");
        try {
            var group=root.AddComponent<UnityEngine.CanvasGroup>();group.interactable=false;
            var benchmark=root.AddComponent(type);
            var fields=BindingFlags.Instance|BindingFlags.NonPublic;
            type.GetField("controls",fields).SetValue(benchmark,group);
            type.GetField("oldInteractable",fields).SetValue(benchmark,true);
            type.GetField("controlsLocked",fields).SetValue(benchmark,true);
            root.SetActive(false);
            // Ordinary MonoBehaviours do not receive play-mode lifecycle callbacks in EditMode.
            type.GetMethod("OnDisable",fields).Invoke(benchmark,null);
            Assert.That(group.interactable,Is.True,"Disabling the benchmark must release its UI lock.");
            Assert.That(type.GetProperty("Running").GetValue(benchmark),Is.False);
        } finally { UnityEngine.Object.DestroyImmediate(root); }
    }
    [Test] public void DuplicatePredictedAndOlderObservationsCannotIncreaseFreshCount() {
        var type=AppDomain.CurrentDomain.GetAssemblies().Select(a=>a.GetType("HumanVision.TestProject.Diagnostics.FreshSkeletonCounter")).FirstOrDefault(t=>t!=null);
        Assert.That(type,Is.Not.Null,"Acceptance must count per-person observations, not render frames.");
        var counter=Activator.CreateInstance(type);
        var observe=type.GetMethod("Observe");
        Assert.That(observe.Invoke(counter,new object[]{12L,1L,20L,100L,true,12d}),Is.True);
        Assert.That(observe.Invoke(counter,new object[]{12L,2L,20L,100L,true,12d}),Is.False);
        Assert.That(observe.Invoke(counter,new object[]{12L,3L,21L,100L,true,12d}),Is.False);
        Assert.That(observe.Invoke(counter,new object[]{12L,4L,19L,99L,true,12d}),Is.False);
        Assert.That(observe.Invoke(counter,new object[]{12L,5L,22L,101L,false,15d}),Is.True);
        Assert.That(observe.Invoke(counter,new object[]{13L,1L,20L,100L,true,12d}),Is.True,"Different people count independently.");
    }
}}
