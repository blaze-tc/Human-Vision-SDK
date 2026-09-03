using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using NUnit.Framework;
using UnityEditor;

namespace HumanVision.Tests.EditMode
{
    public sealed class HumanVisionPluginIsolationTests
    {
        [TestCase("onnxruntime")]
        [TestCase("onnxruntime_providers_shared")]
        public void NativeRuntimePluginNamesAreUnique(string pluginName)
        {
            List<string> compatiblePaths = PluginImporter.GetAllImporters()
                .Where(IsEditorOrWindowsCompatible)
                .Select(importer => importer.assetPath)
                .Where(path => string.Equals(
                    Path.GetFileNameWithoutExtension(path),
                    pluginName,
                    StringComparison.OrdinalIgnoreCase))
                .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
                .ToList();

            Assert.That(
                compatiblePaths,
                Has.Count.LessThanOrEqualTo(1),
                $"Expected at most one compatible '{pluginName}' plugin, but found: " +
                string.Join(", ", compatiblePaths));
        }

        private static bool IsEditorOrWindowsCompatible(PluginImporter importer)
        {
            return importer.GetCompatibleWithAnyPlatform() ||
                   importer.GetCompatibleWithEditor() ||
                   importer.GetCompatibleWithPlatform(BuildTarget.StandaloneWindows64);
        }
    }
}
