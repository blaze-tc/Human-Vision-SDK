using System.IO;
using HumanVision.Demo;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace HumanVision.Demo.Editor
{
    public static class HumanVisionGpuDemoSetup
    {
        [MenuItem("HumanVision/Use camera test image")]
        public static void ConfigureImage()
        {
            const string relativeImage = "HumanVision/Media/cameraImage.png";
            if (!File.Exists(Path.Combine(Application.streamingAssetsPath, relativeImage)))
                throw new FileNotFoundException("Copy cameraImage.png to StreamingAssets/HumanVision/Media first.");
            var bootstrap = Object.FindObjectOfType<HumanVisionDemoBootstrap>();
            if (bootstrap == null) throw new System.InvalidOperationException("Open HumanVisionD04Demo first.");
            var serialized = new SerializedObject(bootstrap);
            serialized.FindProperty("startupVideo").stringValue = relativeImage;
            serialized.FindProperty("maxBodies").intValue = 8;
            serialized.ApplyModifiedProperties();
            EnsurePresentationCamera();
            EditorSceneManager.SaveScene(bootstrap.gameObject.scene);
        }

        internal static void EnsurePresentationCamera()
        {
            if (Object.FindObjectOfType<Camera>() != null) return;
            var camera = new GameObject("HumanVision Presentation Camera", typeof(Camera)).GetComponent<Camera>();
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = Color.black;
            camera.cullingMask = 0;
        }

        [MenuItem("HumanVision/Capture runtime evidence")]
        public static void CaptureEvidence()
        {
            if (!Application.isPlaying) throw new System.InvalidOperationException("Enter Play Mode first.");
            var manager = Object.FindObjectOfType<HumanVisionManager>();
            var source = Object.FindObjectOfType<VideoPlayerFrameSource>();
            var bodies = new System.Collections.Generic.List<BodyEvidence>();
            for (int i = 0; i < manager.BodyCount; i++)
            {
                var body = manager.Bodies[i];
                var joints = new System.Collections.Generic.List<JointEvidence>();
                foreach (var joint in body.Joints)
                    joints.Add(new JointEvidence { x = joint.Pixel.x, y = joint.Pixel.y, confidence = joint.Confidence, valid = joint.Valid });
                bodies.Add(new BodyEvidence { id = body.TrackId, box = new Rect(body.BoundingBoxPixels.x, body.BoundingBoxPixels.y,
                    body.BoundingBoxPixels.width, body.BoundingBoxPixels.height), joints = joints.ToArray() });
            }
            string directory = Path.GetFullPath("Screenshots");
            Directory.CreateDirectory(directory);
            string stem = Path.Combine(directory, "humanvision-" + System.DateTime.Now.ToString("yyyyMMdd-HHmmss"));
            File.WriteAllText(stem + ".json", JsonUtility.ToJson(new Evidence {
                image = source.CurrentVideoPath, width = source.SourceWidth, height = source.SourceHeight,
                sequence = manager.ResultSequence, sourceFrame = manager.SourceFrameId,
                presentationFrame = source.PresentationFrameId, readbackErrors = source.ReadbackErrors,
                error = manager.LastError, inferenceFps = manager.Stats.InferenceFps,
                totalMs = manager.Stats.TotalMs, bodies = bodies.ToArray()
            }, true));
            ScreenCapture.CaptureScreenshot(stem + ".png");
            Debug.Log("HumanVision evidence saved: " + stem);
        }
        [System.Serializable] private sealed class Evidence {
            public string image, error;
            public int width, height;
            public long sequence, sourceFrame, presentationFrame, readbackErrors;
            public float inferenceFps, totalMs;
            public BodyEvidence[] bodies;
        }
        [System.Serializable] private sealed class BodyEvidence {
            public int id;
            public Rect box;
            public JointEvidence[] joints;
        }
        [System.Serializable] private sealed class JointEvidence {
            public float x, y, confidence;
            public bool valid;
        }
        [MenuItem("HumanVision/Use validated person detector")]
        public static void Configure()
        {
            const string relativeModel = "HumanVision/Models/rtmdet_tiny_person_640.onnx";
            if (!File.Exists(Path.Combine(Application.streamingAssetsPath, relativeModel)))
                throw new FileNotFoundException("Prepare and copy the validated person detector first.");
            var bootstrap = Object.FindObjectOfType<HumanVisionDemoBootstrap>();
            if (bootstrap == null) throw new System.InvalidOperationException("Open HumanVisionD04Demo first.");
            var serialized = new SerializedObject(bootstrap);
            serialized.FindProperty("detectorModel").stringValue = relativeModel;
            serialized.ApplyModifiedProperties();
            EditorSceneManager.SaveScene(bootstrap.gameObject.scene);
            Debug.Log("HumanVision Demo now uses the validated person-only detector.");
        }
    }
}

