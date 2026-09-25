using System;
using NUnit.Framework;
using HumanVision.Editor;

namespace HumanVision.Tests
{
    public sealed class HumanVisionAndroidGpuGateBuildTests
    {
        [Test]
        public void OrdinaryEditorBuildCannotActivateGateBypass()
        {
            Assert.False(HumanVisionAndroidGpuGateBuild.IsAuthorizedGateBuild(null));
            var error = Assert.Throws<InvalidOperationException>(() => HumanVisionAndroidGpuGateBuild.Build());
            StringAssert.Contains("isolated test script", error.Message);
        }
    }
}
