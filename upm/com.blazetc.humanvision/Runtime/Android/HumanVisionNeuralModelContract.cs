using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Security.Cryptography;
using J = HumanVision.HumanVisionConfigurationJson;

namespace HumanVision
{
    // Private deployment contract. Gameplay consumes a semantic preference, never this model/provider schema.
    internal static class HumanVisionNeuralModelContract
    {
        private const string ExecutionContract = "raw_tensor_rknn_nonquantized_v1";
        private sealed class Candidate
        {
            internal readonly ModelInputQualityChoice Choice;
            internal readonly string Pack, ModelHash, SourceHash, ValidationHash, ConversionHash, ComparisonHash;

            internal Candidate(ModelInputQuality quality, string profile, int width, int height, string pack,
                string model, string source, string validation, string conversion, string comparison)
            {
                Choice = new ModelInputQualityChoice(quality, profile, width, height);
                Pack = pack;
                ModelHash = model;
                SourceHash = source;
                ValidationHash = validation;
                ConversionHash = conversion;
                ComparisonHash = comparison;
            }
        }
        private static readonly Candidate[] Candidates = {
            new Candidate(ModelInputQuality.Low, HumanVisionAndroidAccelerationSelection.NeuralProfile, 512, 288,
                "yolov8n-pose-rectangle512x288-rknn-nonquantized-experimental",
                "1b3ba8dd5bb81e3d968cfeef019c42b1aa3bffca6f0ec1c9f89f257fe0c08030",
                "6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3",
                "5ab8bb97034e957f3176162a63b471790b64514c7ba5a34d17622190167d4c34",
                "244a28f1bc9e0b1665fe27a3b35e774a732f9f62f752b5f6faed376e28bef4bb",
                "fe29f669bd5fafecba7a21797cad35f99b9f21b540c419a5f479d98cd03687f9"),
            new Candidate(ModelInputQuality.Medium, HumanVisionAndroidAccelerationSelection.NeuralMediumProfile, 640, 384,
                "yolov8n-pose-rectangle640x384-rknn-nonquantized-experimental",
                "d2c1d88ed6402ac250cf0f5f1372d7493c25e191f9eb4f655cea27d423f4e0fb",
                "7e94dece2faf25127481ac010b0d863dabec316cb8116e14cf342ebd92e5d23c",
                "a022c6b5186784f72e0c5800e40e8662d6a04e9c05285572d68aee4f186d8aa3",
                "9afce958cd2745388130fd9b92033e159c64da6f617fca6a65b5fd66ecd43259",
                "7635a4d3eeb2536b9214b79d4635120350584136af9eabba7a8bc93b6d1b1936"),
            new Candidate(ModelInputQuality.High, HumanVisionAndroidAccelerationSelection.NeuralHighProfile, 960, 576,
                "yolov8n-pose-rectangle960x576-rknn-nonquantized-experimental",
                "b37a3f9f19d1a38966ddfbe9c50b2ecff0417059db138a81264419c89a9c3859",
                "fb7ad3aff9d1560072880d488d68c72ee46fbdbf5e50e01739177c2a8ee04db8",
                "aab163a9b306c93e6a315306b02fb36cb8292affc9eeb49fce65049d7809e6e7",
                "3a6c085951c251d947da8408deb2a3385cd090cfab0093972ed26aa7202ac741",
                "763dbbbe3ae46a5cd20f60cf4bcd4a483d9371428c24f149ca5b101c82339504")
        };

        private static string ProfilePath(string profile) => "profiles/" + profile + ".json";

        private static Dictionary<string,string> ReadIndex(string root)
        {
            var index = J.Object(J.Parse(File.ReadAllText(Path.Combine(root, "index.json"))));
            var files = new Dictionary<string,string>(StringComparer.Ordinal);
            foreach (var value in J.Array(J.Field(index, "files"))) {
                var row = J.Object(value); string name = J.Text(row, "path");
                if (files.ContainsKey(name)) throw new InvalidDataException("Duplicate neural runtime index path.");
                files.Add(name, J.Text(row, "sha256"));
            }
            return files;
        }

        internal static ModelInputQualityChoice[] LoadInstalled(string root, string requiredProfile = null)
        {
            var files = ReadIndex(root);
            var choices = new List<ModelInputQualityChoice>();
            foreach (var candidate in Candidates) {
                var choice = candidate.Choice;
                string profile = ProfilePath(choice.ProfileId);
                if (choice.Quality == ModelInputQuality.Low || choice.ProfileId == requiredProfile ||
                    files.ContainsKey(profile) || File.Exists(Path.Combine(root, profile))) {
                    Validate(root, files, candidate);
                    choices.Add(choice);
                }
            }
            return choices.ToArray();
        }

        internal static ModelInputQualityChoice Validate(string root,
            string profileId = HumanVisionAndroidAccelerationSelection.NeuralProfile)
        {
            var candidate = Candidates.FirstOrDefault(model => model.Choice.ProfileId == profileId);
            if (candidate == null) throw new InvalidDataException("Unknown RK3588 NPU profile: " + profileId);
            Validate(root, ReadIndex(root), candidate);
            return candidate.Choice;
        }

        private static void Validate(string root, Dictionary<string,string> files, Candidate candidate)
        {
            var choice = candidate.Choice;
            int rows = choice.Width / 8 * (choice.Height / 8) + choice.Width / 16 * (choice.Height / 16) +
                choice.Width / 32 * (choice.Height / 32);
            string profilePath = ProfilePath(choice.ProfileId);
            var profile = Read(root, files, profilePath);
            var backend = J.Object(J.Field(profile, "backend"));
            var body = J.Object(J.Field(profile, "body"));
            if (J.Integer(profile, "schema_version") != 1 || !J.Boolean(profile, "experimental") ||
                J.Text(profile, "execution_contract") != ExecutionContract ||
                !Exactly(J.Field(profile, "required_capabilities"), "body_pose", "multi_person", "tensor_inference") ||
                J.Text(profile, "profile") != choice.ProfileId ||
                !J.Boolean(profile, "local_evaluation_only") || J.Boolean(J.Object(J.Field(profile,"hands")),"enabled") ||
                J.Boolean(backend,"allow_fallback") || !Strings(J.Field(backend,"preference")).SequenceEqual(new[] { "backend.rknn" }) ||
                J.Text(body,"pipeline") != "pipeline.yolo.tensor" || J.Text(body,"modelPack") != candidate.Pack)
                throw new InvalidDataException("Neural profile is outside the admitted experimental contract.");
            string prefix = "modelpacks/" + candidate.Pack + "/";
            if (File.Exists(Path.Combine(root, prefix + "modelpack.json"))) throw new InvalidDataException("Ambiguous neural ModelPack manifest.");
            var manifest = Read(root, files, prefix + "manifest.json");
            if (J.Integer(manifest,"schema_version") != 1 || J.Text(manifest,"pack_id") != candidate.Pack ||
                J.Text(manifest,"profile_id") != choice.ProfileId ||
                J.Integer(manifest,"max_people") != 8 || !Exactly(J.Field(manifest,"capabilities"), "body_pose", "multi_person") ||
                J.Text(manifest,"pipeline_id") != "pipeline.yolo.tensor" || !J.Boolean(manifest,"local_evaluation_only") ||
                !J.Boolean(manifest,"experimental") || J.Text(manifest,"execution_contract") != ExecutionContract ||
                J.Text(manifest,"profile_sha256") != files[profilePath]) throw new InvalidDataException("Neural ModelPack/profile binding mismatch.");
            var models = J.Array(J.Field(manifest,"models"));
            if (models.Count != 1) throw new InvalidDataException("Neural model requires exactly one body model.");
            var model = J.Object(models[0]); var input = J.Object(J.Field(model,"input_contract"));
            var output = J.Object(J.Field(model,"output_contract")); var options = J.Object(J.Field(model,"backend_options"));
            if (J.Text(model,"role") != "body" || J.Text(model,"format") != "rknn" ||
                J.Text(model,"asset_path") != "body.rknn" || J.Text(model,"sha256") != candidate.ModelHash ||
                J.Text(model,"decoder_id") != "yolov8_pose_dfl17_v1" || J.Integer(input,"width") != choice.Width || J.Integer(input,"height") != choice.Height ||
                J.Text(input,"color_order") != "RGB" || J.Text(input,"tensor_dtype") != "uint8" || J.Text(input,"tensor_layout") != "NHWC" ||
                J.Text(input,"input_blob") != "in0" || J.Text(input,"crop_mode") != "letterbox" ||
                J.Text(output,"decoder") != "yolov8_pose_dfl17_v1" || J.Text(output,"tensor_dtype") != "fp32" ||
                J.Integer(output,"rows") != rows || !Integers(J.Field(output,"columns"),65,51) ||
                !Strings(J.Field(output,"output_blobs")).SequenceEqual(new[] { "out0","out1" }) ||
                J.Text(options,"runtime_library") != "librknnrt.so" || J.Integer(options,"core_mask") != 7 ||
                J.Text(options,"input_layout") != "nhwc" || J.Text(options,"input_type") != "uint8")
                throw new InvalidDataException("Neural model differs from its pinned input and output contract.");
            var bounds = J.Object(J.Field(output,"max_output_bytes"));
            if (J.Integer(bounds,"out0") != rows * 65 * sizeof(float) || J.Integer(bounds,"out1") != rows * 51 * sizeof(float)) throw new InvalidDataException("Neural output bounds mismatch.");
            var normalization = J.Object(J.Field(input,"normalization"));
            Numbers(J.Field(input,"pad_rgb"),114); Numbers(J.Field(normalization,"mean"),0); Numbers(J.Field(normalization,"norm"),1d/255);
            ValidateEvidence(root, files, prefix, manifest, model, candidate);
            string asset = prefix + "body.rknn";
            Verify(root,files,asset);
            if (files[asset] != candidate.ModelHash) throw new InvalidDataException("Neural model is not the pinned non-quantized candidate.");
        }
        private static void ValidateEvidence(string root, Dictionary<string,string> files, string prefix,
            Dictionary<string,object> manifest, Dictionary<string,object> model, Candidate candidate)
        {
            var qualification = J.Object(J.Field(manifest,"qualification"));
            if (!J.Boolean(qualification,"offline_numerical_passed") || J.Boolean(qualification,"deployment_ready") ||
                J.Boolean(qualification,"device_performance_verified") || J.Text(qualification,"source_onnx_sha256") != candidate.SourceHash ||
                J.Text(qualification,"validation_index_sha256") != candidate.ValidationHash)
                throw new InvalidDataException("Neural qualification must remain the pinned offline experiment.");
            var evidence = J.Array(J.Field(model,"evidence"));
            if (evidence.Count != 2) throw new InvalidDataException("Both saved neural offline receipts are required.");
            foreach (var expected in new[] { new { File="conversion-receipt.json", Field="conversion_receipt_sha256", Hash=candidate.ConversionHash },
                new { File="simulator-comparison.json", Field="simulator_comparison_sha256", Hash=candidate.ComparisonHash } }) {
                var rows = evidence.Select(J.Object).Where(row => J.Text(row,"path") == expected.File).ToArray();
                string relative = prefix + expected.File, expectedHash = J.Text(qualification,expected.Field);
                if (expectedHash != expected.Hash || rows.Length != 1 || J.Text(rows[0],"sha256") != expectedHash ||
                    !files.TryGetValue(relative,out string indexed) || indexed != expectedHash)
                    throw new InvalidDataException("Neural offline receipt declaration/index mismatch: " + expected.File);
            }
            var conversion = Read(root,files,prefix+"conversion-receipt.json");
            var conversionInput = J.Object(J.Field(conversion, "input"));
            if (!Integers(J.Field(conversionInput, "shape"), 1, 3, candidate.Choice.Height, candidate.Choice.Width) ||
                J.Text(conversion,"target") != "rk3588" || J.Text(conversion,"toolkit_version") != "2.3.2" ||
                J.Text(conversion,"source_onnx_sha256") != candidate.SourceHash || J.Text(conversion,"rknn_sha256") != candidate.ModelHash ||
                J.Text(conversion,"precision_request") != "non-quantized" || J.Boolean(conversion,"deployment_ready") ||
                J.Boolean(conversion,"device_performance_verified"))
                throw new InvalidDataException("Neural conversion receipt is outside the admitted non-quantized experiment.");
            var simulator = Read(root,files,prefix+"simulator-comparison.json");
            var identity = J.Object(J.Field(simulator,"identity"));
            var fixtures = J.Array(J.Field(simulator,"fixtures")).Select(J.Object).ToArray();
            if (!J.Boolean(simulator,"offline_numerical_passed") || J.Boolean(simulator,"deployment_ready") ||
                J.Boolean(simulator,"device_performance_verified") || J.Text(identity,"rknn_sha256") != candidate.ModelHash ||
                J.Text(identity,"validation_index_sha256") != candidate.ValidationHash || fixtures.Length != 3 ||
                !fixtures.Select(row => J.Integer(row,"expected_people")).OrderBy(value => value).SequenceEqual(new long[] {0,1,7}) ||
                fixtures.Any(row => !J.Boolean(row,"passed")))
                throw new InvalidDataException("Neural simulator receipt requires all unchanged seven/one/empty offline controls.");
        }
        private static bool Exactly(object value, params string[] expected) {
            var rows = Strings(value); return rows.Length == expected.Length && new HashSet<string>(rows,StringComparer.Ordinal).SetEquals(expected);
        }
        private static bool Integers(object value, params long[] expected) =>
            J.Array(value).Select(item => item is long number ? number : throw new InvalidDataException("Expected tensor dimensions.")).SequenceEqual(expected);
        private static string[] Strings(object value) => J.Array(value).Select(item => item as string ?? throw new InvalidDataException("Expected model strings.")).ToArray();
        private static void Numbers(object value,double expected) {
            var rows = J.Array(value); if (rows.Count != 3 || rows.Any(item => !(item is long || item is double) || Math.Abs(Convert.ToDouble(item)-expected)>1e-15)) throw new InvalidDataException("Neural normalization/color mismatch.");
        }
        private static Dictionary<string,object> Read(string root,Dictionary<string,string> files,string relative) {
            Verify(root,files,relative); return J.Object(J.Parse(File.ReadAllText(Path.Combine(root,relative))));
        }
        private static void Verify(string root,Dictionary<string,string> files,string relative) {
            if (!files.TryGetValue(relative,out string hash) || hash.Length != 64) throw new InvalidDataException("Neural runtime file is not indexed: " + relative);
            string path = Path.Combine(root,relative);
            if (!File.Exists(path)) throw new InvalidDataException("Neural runtime file is missing: " + relative);
            for (string current = Path.GetFullPath(path); current.Length > Path.GetFullPath(root).Length; current = Path.GetDirectoryName(current))
                if ((File.Exists(current)||Directory.Exists(current))&&(File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0) throw new InvalidDataException("Neural runtime paths cannot use links/junctions.");
            using (var stream=File.OpenRead(path)) using(var sha=SHA256.Create())
                if (BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","").ToLowerInvariant()!=hash) throw new InvalidDataException("Neural runtime SHA-256 mismatch: " + relative);
        }
    }
}
