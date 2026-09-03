using System;
using System.IO;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace HumanVision.Demo.Editor
{
    public static class HumanVisionDemoWindowsBuild
    {
        public const string RelativeOutputPath = "Builds/HumanVisionD04/HumanVisionD04.exe";

        [MenuItem("HumanVision/Build Windows x64 Demo")]
        public static void BuildWindowsPlayer()
        {
            if (AssetDatabase.LoadAssetAtPath<SceneAsset>(HumanVisionDemoSceneBuilder.ScenePath) == null)
            {
                throw new InvalidOperationException(
                    "The HumanVision Demo scene is missing: " + HumanVisionDemoSceneBuilder.ScenePath);
            }

            string projectRoot = Path.GetFullPath(Path.Combine(Application.dataPath, ".."));
            string outputPath = Path.GetFullPath(Path.Combine(projectRoot, RelativeOutputPath));
            string outputDirectory = Path.GetDirectoryName(outputPath);
            if (string.IsNullOrWhiteSpace(outputDirectory))
            {
                throw new InvalidOperationException("The HumanVision Demo build output directory is invalid.");
            }

            Directory.CreateDirectory(outputDirectory);
            BuildReport report = BuildPipeline.BuildPlayer(new BuildPlayerOptions
            {
                scenes = new[] { HumanVisionDemoSceneBuilder.ScenePath },
                locationPathName = outputPath,
                target = BuildTarget.StandaloneWindows64,
                options = BuildOptions.None
            });

            if (report.summary.result != BuildResult.Succeeded)
            {
                throw new InvalidOperationException(
                    $"HumanVision Windows x64 build failed: {report.summary.result}, " +
                    $"errors={report.summary.totalErrors}, warnings={report.summary.totalWarnings}.");
            }

            Debug.Log(
                $"HumanVision Windows x64 build succeeded: {outputPath} " +
                $"({report.summary.totalSize} bytes, {report.summary.totalTime}).");
        }
    }
}
