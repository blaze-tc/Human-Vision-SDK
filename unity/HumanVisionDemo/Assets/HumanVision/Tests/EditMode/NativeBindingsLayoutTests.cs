using System;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using HumanVision.Interop;
using NUnit.Framework;

namespace HumanVision.Tests
{
    public sealed class NativeBindingsLayoutTests
    {
        [Test]
        public void NativeStructsMatchWindowsX64CAbi()
        {
            Assert.That(IntPtr.Size, Is.EqualTo(8), "D0 Unity Demo is Windows x64 only.");
            Assert.That(Marshal.SizeOf<HVConfigNative>(), Is.EqualTo(48));
            Assert.That(Marshal.SizeOf<HVVideoFrameNative>(), Is.EqualTo(56));
            Assert.That(Marshal.SizeOf<HVJointNative>(), Is.EqualTo(24));
            Assert.That(Marshal.SizeOf<HVRectNative>(), Is.EqualTo(16));
            Assert.That(Marshal.SizeOf<HVBodyNative>(), Is.EqualTo(436));
            Assert.That(Marshal.SizeOf<HVResultMetaNative>(), Is.EqualTo(40));
            Assert.That(Marshal.SizeOf<HVStatsNative>(), Is.EqualTo(56));
        }

        [Test]
        public void NativeStructOffsetsMatchPublishedSdkContract()
        {
            AssertOffset<HVConfigNative>(nameof(HVConfigNative.DetectorModelPathUtf8), 32);
            AssertOffset<HVVideoFrameNative>(nameof(HVVideoFrameNative.FrameId), 24);
            AssertOffset<HVVideoFrameNative>(nameof(HVVideoFrameNative.Data), 40);
            AssertOffset<HVBodyNative>(nameof(HVBodyNative.Joint0), 28);
            AssertOffset<HVResultMetaNative>(nameof(HVResultMetaNative.ResultSequence), 8);
            AssertOffset<HVStatsNative>(nameof(HVStatsNative.SubmittedFrames), 32);
        }

        [Test]
        public void EveryNativeCallUsesCdeclAndExactEntryPoint()
        {
            MethodInfo[] methods = typeof(NativeBindings)
                .GetMethods(BindingFlags.Static | BindingFlags.NonPublic)
                .Where(method => method.GetCustomAttribute<DllImportAttribute>() != null)
                .ToArray();

            Assert.That(methods, Has.Length.EqualTo(10));
            foreach (MethodInfo method in methods)
            {
                DllImportAttribute attribute = method.GetCustomAttribute<DllImportAttribute>();
                Assert.That(attribute.Value, Is.EqualTo("humanvision"), method.Name);
                Assert.That(attribute.CallingConvention, Is.EqualTo(CallingConvention.Cdecl), method.Name);
                Assert.That(attribute.ExactSpelling, Is.True, method.Name);
                Assert.That(attribute.EntryPoint, Is.EqualTo(method.Name), method.Name);
            }
        }

        private static void AssertOffset<T>(string fieldName, int expected)
        {
            Assert.That(Marshal.OffsetOf<T>(fieldName).ToInt32(), Is.EqualTo(expected),
                $"Unexpected native offset for {typeof(T).Name}.{fieldName}");
        }
    }
}
