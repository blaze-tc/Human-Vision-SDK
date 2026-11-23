#if UNITY_EDITOR
using System.Collections;
using System.Linq;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using UnityEngine.SceneManagement;
using UnityEngine.TestTools;
namespace HumanVision.Input.Tests
{
    public sealed class InputRetirementPumpEditModeTests
    {
        [UnityTest] public IEnumerator PendingRetirementUsesEditorUpdatesWithoutCreatingSceneObjects()
        {
            Assert.That(Application.isPlaying, Is.False, "This is the EditMode retirement contract.");
            int before = Resources.FindObjectsOfTypeAll<GameObject>().Count(value => value.name == "HumanVision input retirement");
            bool sceneDirty = SceneManager.GetActiveScene().isDirty;
            var owner = new SourceRetirement(); bool pending = true; int progress = 0;
            var registration = new InputRetirementRegistration(owner, () => ++progress, () => pending);
            try {
                InputRetirementPump.Watch(registration);
                double deadline = EditorApplication.timeSinceStartup + 2;
                while (progress == 0 && EditorApplication.timeSinceStartup < deadline) yield return null;
                Assert.That(progress, Is.GreaterThan(0), "Editor updates must continue actual retirement polling.");
                Assert.That(registration.Queued, Is.True, "An unfinished owner cannot be discarded.");
                pending = false;
                while (registration.Queued && EditorApplication.timeSinceStartup < deadline) yield return null;
                Assert.That(registration.Queued, Is.False, "Proven retirement must detach its editor registration.");
                Assert.That(Resources.FindObjectsOfTypeAll<GameObject>().Count(value => value.name == "HumanVision input retirement"), Is.EqualTo(before));
                Assert.That(SceneManager.GetActiveScene().isDirty, Is.EqualTo(sceneDirty));
            } finally { pending = false; }
        }
    }
}
#endif
