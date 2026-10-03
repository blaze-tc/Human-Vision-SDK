using System.Linq;
using UnityEditor;
using UnityEngine;

namespace HumanVision.Editor
{
    [FilePath("ProjectSettings/HumanVisionAndroidRuntimeSettings.asset", FilePathAttribute.Location.ProjectFolder)]
    public sealed class HumanVisionAndroidRuntimeSettings : ScriptableSingleton<HumanVisionAndroidRuntimeSettings>
    {
        [SerializeField]
        private string runtimeModeId = "android-ncnn-vulkan";

        public string RuntimeModeId
        {
            get { return string.IsNullOrEmpty(runtimeModeId) ? "android-ncnn-vulkan" : runtimeModeId; }
            set
            {
                HumanVisionAndroidRuntimeModeRegistry.Resolve(value);
                runtimeModeId = value;
                Save(true);
            }
        }

        [SettingsProvider]
        public static SettingsProvider CreateProvider()
        {
            return new SettingsProvider("Project/Human Vision/Android Runtime", SettingsScope.Project)
            {
                label = "Android Runtime",
                guiHandler = searchContext =>
                {
                    var settings = instance;
                    var modes = HumanVisionAndroidRuntimeModeRegistry.All;
                    var labels = modes.Select(x => x.DisplayName).ToArray();
                    var selected = modes.ToList().FindIndex(x => x.Id == settings.RuntimeModeId);
                    var next = EditorGUILayout.Popup("Runtime mode", selected, labels);
                    if (next != selected && next >= 0 && next < modes.Count)
                        settings.RuntimeModeId = modes[next].Id;

                    var descriptor = HumanVisionAndroidRuntimeModeRegistry.Resolve(settings.RuntimeModeId);
                    EditorGUILayout.LabelField("Profile", descriptor.ProfileId);
                    EditorGUILayout.LabelField("Required capabilities", string.Join(", ", descriptor.RequiredCapabilities.ToArray()));
                },
                keywords = new[] { "Human Vision", "Android", "Runtime", "NCNN", "Vulkan", "ORT" }
            };
        }
    }
}
