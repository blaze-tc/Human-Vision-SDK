using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using J = HumanVision.HumanVisionConfigurationJson;

namespace HumanVision
{
    // Medium is zero so older persisted settings migrate to the actual existing shape.
    public enum ModelInputQuality { Medium = 0, High = 1, Low = 2 }

    public sealed class ModelInputQualityChoice
    {
        public ModelInputQuality Quality { get; private set; }
        public string ProfileId { get; private set; }
        public int Width { get; private set; }
        public int Height { get; private set; }
        internal ModelInputQualityChoice(ModelInputQuality quality, string profile, int width, int height)
        { Quality = quality; ProfileId = profile; Width = width; Height = height; }
    }

    /// <summary>Validated configuration-time model choices. An absent family means quality is unavailable.</summary>
    public sealed class HumanVisionModelInputQualities
    {
        public const string CatalogFilename = "model-input-qualities.json";
        public const string AdmittedRuntimeMode = "android-ncnn-vulkan";
        private const string Pipeline = "pipeline.yolo.pose";
        private const string Execution = "raw_tensor_fp32_v1";
        private const string Decoder = "yolov8_pose_dfl17_v1";
        private const string ParamHash = "908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905";
        private const string BinHash = "6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9";
        private const string ModelSource = "https://github.com/nihui/ncnn-android-yolov8/tree/f1ac75ec54ccb3817a9eba620fe51da8bdcf87ca";
        private const string ModelLicense = "Ultralytics origin; distribution rights unestablished; local evaluation only";
        private readonly ModelInputQualityChoice[] choices;
        private HumanVisionModelInputQualities(ModelInputQualityChoice[] choices) { this.choices = choices; }
        public ModelInputQualityChoice[] ChoicesForMode(string mode) => mode == AdmittedRuntimeMode ? (ModelInputQualityChoice[])choices.Clone() : new ModelInputQualityChoice[0];
        public string[] ProfilesForMode(string mode) => ChoicesForMode(mode).Select(choice => choice.ProfileId).ToArray();
        public string ResolveQuality(string mode, ModelInputQuality quality)
        {
            var choice = ChoicesForMode(mode).FirstOrDefault(item => item.Quality == quality);
            if (choice == null) throw new InvalidOperationException("Model input quality is unavailable for runtime profile '" + mode + "'.");
            return choice.ProfileId;
        }
        public string ResolveQuality(string mode, string quality)
        {
            ModelInputQuality value;
            switch (quality) { case "medium": value = ModelInputQuality.Medium; break; case "low": value = ModelInputQuality.Low; break; case "high": value = ModelInputQuality.High; break; default: throw new InvalidOperationException("Unknown model input quality: " + quality); }
            return ResolveQuality(mode, value);
        }
        public static HumanVisionModelInputQualities Load(string runtimeRoot)
        {
            var path = ContainedPath(runtimeRoot, CatalogFilename);
            var indexPath = ContainedPath(runtimeRoot, "index.json");
            if (!File.Exists(indexPath))
            {
                if (File.Exists(path)) throw new InvalidDataException("Quality runtime index is missing.");
                return new HumanVisionModelInputQualities(new ModelInputQualityChoice[0]);
            }
            // The current index is authoritative. Retained caches from another APK
            // are not declarations and must not invent choices for a legacy build.
            var sourceIndex = ReadObject(indexPath);
            bool declared = J.Array(J.Field(sourceIndex, "files")).Any(row => J.Text(J.Object(row), "path") == CatalogFilename);
            if (!declared) return new HumanVisionModelInputQualities(new ModelInputQualityChoice[0]);
            if (!File.Exists(path)) throw new InvalidDataException("Indexed quality catalog is missing.");
            var files = ValidateIndex(runtimeRoot);
            RequireIndexed(files, CatalogFilename);
            var catalog = ReadObject(path); J.Keys(catalog, "schema_version", "families");
            if (J.Integer(catalog, "schema_version") != 1) throw new InvalidDataException("Unsupported model input quality schema.");
            var families = J.Array(J.Field(catalog, "families"));
            if (families.Count != 1) throw new InvalidDataException("Exactly one admitted model input quality family is required.");
            var family = J.Object(families[0]); J.Keys(family, "runtime_mode", "base_profile", "default_quality", "qualities");
            if (J.Text(family, "runtime_mode") != AdmittedRuntimeMode || J.Text(family, "base_profile") != AdmittedRuntimeMode || J.Text(family, "default_quality") != "medium") throw new InvalidDataException("Unsupported quality family/default.");
            var entries = J.Array(J.Field(family, "qualities"));
            if (entries.Count != 3) throw new InvalidDataException("Exactly low, medium and high qualities are required.");
            var rows = new Dictionary<string, Dictionary<string, object>>(StringComparer.Ordinal);
            foreach (var entry in entries)
            {
                var row = J.Object(entry); J.Keys(row, "id", "profile", "width", "height"); string id = J.Text(row, "id");
                if (rows.ContainsKey(id) || (id != "low" && id != "medium" && id != "high")) throw new InvalidDataException("Unexpected/duplicate quality ID.");
                rows.Add(id, row);
            }
            var result = new List<ModelInputQualityChoice>();
            foreach (string id in new[] { "low", "medium", "high" })
            {
                int width = id == "low" ? 512 : id == "high" ? 960 : 640, height = id == "low" ? 288 : id == "high" ? 576 : 384;
                string profile = id == "medium" ? AdmittedRuntimeMode : AdmittedRuntimeMode + "-quality-" + id;
                var row = rows[id];
                if (J.Text(row, "profile") != profile || J.Integer(row, "width") != width || J.Integer(row, "height") != height) throw new InvalidDataException("Quality profile/shape does not match the admitted contract.");
                ValidateProfile(runtimeRoot, files, profile, width, height);
                result.Add(new ModelInputQualityChoice(id == "low" ? ModelInputQuality.Low : id == "high" ? ModelInputQuality.High : ModelInputQuality.Medium, profile, width, height));
            }
            return new HumanVisionModelInputQualities(result.ToArray());
        }
        private static void ValidateProfile(string root, Dictionary<string, string> files, string id, int width, int height)
        {
            string profileRelative = "profiles/" + id + ".json"; RequireIndexed(files, profileRelative);
            var profile = ReadObject(ContainedPath(root, profileRelative));
            if (J.Integer(profile, "schema_version") != 1 || J.Text(profile, "profile") != id || !J.Boolean(profile, "local_evaluation_only") || J.Text(profile, "frame_policy") != "every_frame") throw new InvalidDataException("Quality profile identity/contract mismatch.");
            var hands = J.Object(J.Field(profile, "hands")); if (J.Boolean(hands, "enabled")) throw new InvalidDataException("Quality profiles require hands disabled.");
            var backend = J.Object(J.Field(profile, "backend"));
            if (J.Boolean(backend, "allow_fallback") || !Strings(J.Field(backend, "preference")).SequenceEqual(new[] { "backend.ncnn.vulkan" })) throw new InvalidDataException("Quality profiles require the selected NCNN backend without fallback.");
            var requiredCapabilities = new HashSet<string>(new[] { "body_pose", "multi_person", "gpu_input", "vulkan", "android-hardware-buffer", "external-sync-fd" }, StringComparer.Ordinal);
            var requirements = Strings(J.Field(profile, "required_capabilities"));
            if (requirements.Distinct().Count() != requirements.Length || !requiredCapabilities.SetEquals(requirements)) throw new InvalidDataException("Quality profile required capabilities mismatch.");
            var body = J.Object(J.Field(profile, "body")); string packId = J.Text(body, "modelPack");
            if (!Regex.IsMatch(packId, "^[a-z0-9][a-z0-9._-]{0,63}$") || J.Text(body, "pipeline") != Pipeline) throw new InvalidDataException("Quality pack/pipeline mismatch.");
            string prefix = "modelpacks/" + packId + "/";
            if (File.Exists(ContainedPath(root, prefix + "manifest.json"))) throw new InvalidDataException("Ambiguous quality model-pack manifest.");
            string manifest = prefix + "modelpack.json"; RequireIndexed(files, manifest);
            var pack = ReadObject(ContainedPath(root, manifest));
            if (J.Integer(pack, "schema_version") != 2 || J.Text(pack, "pack_id") != packId || string.IsNullOrWhiteSpace(J.Text(pack, "pack_version")) || J.Text(pack, "pipeline_id") != Pipeline || J.Integer(pack, "max_people") != 8 || !J.Boolean(pack, "local_evaluation_only") || J.Text(pack, "execution_contract") != Execution || J.Text(pack, "profile_sha256") != files[profileRelative] || (pack.ContainsKey("profile_id") && J.Text(pack, "profile_id") != id)) throw new InvalidDataException("Quality model-pack/profile binding mismatch.");
            var capabilities = Strings(J.Field(pack, "capabilities"));
            if (capabilities.Distinct().Count() != capabilities.Length || !requiredCapabilities.SetEquals(capabilities)) throw new InvalidDataException("Quality model-pack capabilities mismatch.");
            var models = J.Array(J.Field(pack, "models")); if (models.Count != 1) throw new InvalidDataException("Quality pack requires one body model.");
            var model = J.Object(models[0]);
            string recipe = width == 640 ? "Pinned upstream ncnn graph; M1 FP32 rectangle640x384 input eligibility" : "Pinned upstream ncnn graph; reviewed FP32 rectangle" + width + "x" + height + " offline numerical and raised-left-arm eligibility; local evaluation only";
            if (J.Text(model, "source") != ModelSource || J.Text(model, "license") != ModelLicense || J.Text(model, "conversion_recipe") != recipe) throw new InvalidDataException("Quality model pinned provenance metadata mismatch.");
            if (J.Text(model, "role") != "body" || J.Text(model, "format") != "ncnn" || J.Text(model, "decoder_id") != Decoder || J.Text(model, "execution_contract") != Execution) throw new InvalidDataException("Quality model decoder/role mismatch.");
            var options = J.Object(J.Field(model, "backend_options"));
            J.Keys(options, "use_packing_layout", "use_subgroup_ops", "use_fp16_packed", "use_fp16_storage", "use_fp16_arithmetic");
            foreach (string key in options.Keys) if (J.Boolean(options, key) != (key == "use_packing_layout")) throw new InvalidDataException("Quality model requires the approved FP32 options.");
            var input = J.Object(J.Field(model, "input_contract"));
            J.Keys(input, "image_format", "color_order", "crop", "resize_interpolation", "pad_rgb", "normalization", "width", "height", "tensor_dtype", "elempack", "input_blob");
            if (J.Integer(input, "width") != width || J.Integer(input, "height") != height || J.Integer(input, "elempack") != 1 || J.Text(input, "tensor_dtype") != "fp32" || J.Text(input, "image_format") != "rgba8-unorm" || J.Text(input, "color_order") != "rgb" || J.Text(input, "crop") != "letterbox" || J.Text(input, "resize_interpolation") != "bilinear" || J.Text(input, "input_blob") != "in0") throw new InvalidDataException("Quality model actual input shape/contract mismatch.");
            Numbers(J.Field(input, "pad_rgb"), 114); var normalization = J.Object(J.Field(input, "normalization")); J.Keys(normalization, "mean", "norm"); Numbers(J.Field(normalization, "mean"), 0); Numbers(J.Field(normalization, "norm"), 1.0 / 255);
            var output = J.Object(J.Field(model, "output_contract")); J.Keys(output, "decoder", "output_blobs", "max_output_bytes");
            if (J.Text(output, "decoder") != Decoder || !Strings(J.Field(output, "output_blobs")).SequenceEqual(new[] { "out0", "out1" })) throw new InvalidDataException("Quality output decoder/blobs mismatch.");
            int anchors = new[] { 8, 16, 32 }.Sum(stride => width / stride * (height / stride));
            var bounds = J.Object(J.Field(output, "max_output_bytes")); J.Keys(bounds, "out0", "out1");
            if (J.Integer(bounds, "out0") != anchors * 65 * 4 || J.Integer(bounds, "out1") != anchors * 51 * 4) throw new InvalidDataException("Quality output geometry/bounds mismatch.");
            foreach (string kind in new[] { "param", "bin" })
            {
                string filename = "yolov8n_pose.ncnn." + kind, hash = kind == "param" ? ParamHash : BinHash;
                if (J.Text(model, kind + "_path") != filename || J.Text(model, kind + "_sha256") != hash) throw new InvalidDataException("Quality model differs from the pinned graph/weights.");
                RequireIndexed(files, prefix + filename); if (files[prefix + filename] != hash) throw new InvalidDataException("Quality model index/declaration mismatch.");
            }
        }
        private static string[] Strings(object value) => J.Array(value).Select(item => item as string ?? throw new InvalidDataException("Expected configuration string array.")).ToArray();
        private static void Numbers(object value, double expected)
        {
            var rows = J.Array(value); if (rows.Count != 3) throw new InvalidDataException("Expected three normalization/color values.");
            foreach (var row in rows)
                if (!(row is long || row is double) || Convert.ToDouble(row) != expected) throw new InvalidDataException("Unsupported quality normalization/color value.");
        }
        private static Dictionary<string, object> ReadObject(string path) => J.Object(J.Parse(File.ReadAllText(path)));
        private static void RequireIndexed(Dictionary<string, string> files, string path) { if (!files.ContainsKey(path)) throw new InvalidDataException("Quality runtime file is not indexed: " + path); }
        private static Dictionary<string, string> ValidateIndex(string root)
        {
            var index = ReadObject(ContainedPath(root, "index.json")); if (string.IsNullOrWhiteSpace(J.Text(index, "version"))) throw new InvalidDataException("Empty runtime index version.");
            var rows = J.Array(J.Field(index, "files")); if (rows.Count == 0) throw new InvalidDataException("Empty runtime index.");
            var files = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var value in rows)
            {
                var row = J.Object(value); J.Keys(row, "path", "sha256"); string relative = J.Text(row, "path"), hash = J.Text(row, "sha256");
                if (files.ContainsKey(relative)) throw new InvalidDataException("Duplicate runtime index path.");
                string path = ContainedPath(root, relative);
                if (!Regex.IsMatch(hash, "^[a-f0-9]{64}$") || !File.Exists(path)) throw new InvalidDataException("Missing runtime file/hash: " + relative);
                using (var stream = File.OpenRead(path)) using (var sha = SHA256.Create())
                    if (!string.Equals(BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", ""), hash, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Runtime SHA-256 mismatch: " + relative);
                files.Add(relative, hash);
            }
            return files;
        }
        private static string ContainedPath(string root, string relative)
        {
            if (string.IsNullOrEmpty(relative) || relative.Contains("..") || relative.Contains(":") || relative.Contains("\\") || relative.Split('/').Any(part => part == "" || part == ".")) throw new InvalidDataException("Unsafe quality runtime path.");
            string fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, path = Path.GetFullPath(Path.Combine(root, relative));
            if (!path.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Quality runtime path escapes root.");
            for (string current = path; current.Length >= fullRoot.Length; current = Path.GetDirectoryName(current))
                if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("Quality runtime path uses a link/junction.");
            return path;
        }
    }
}
