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
        internal const string Pack = "yolov8n-pose-rectangle512x288-rknn-nonquantized-experimental";
        private const string ModelHash = "1b3ba8dd5bb81e3d968cfeef019c42b1aa3bffca6f0ec1c9f89f257fe0c08030";
        private const string SourceHash = "6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3";
        private const string ValidationHash = "5ab8bb97034e957f3176162a63b471790b64514c7ba5a34d17622190167d4c34";
        private const string ConversionHash = "244a28f1bc9e0b1665fe27a3b35e774a732f9f62f752b5f6faed376e28bef4bb";
        private const string ComparisonHash = "fe29f669bd5fafecba7a21797cad35f99b9f21b540c419a5f479d98cd03687f9";
        private const string ExecutionContract = "raw_tensor_rknn_nonquantized_v1";
        internal static void Validate(string root)
        {
            var index = J.Object(J.Parse(File.ReadAllText(Path.Combine(root, "index.json"))));
            var files = new Dictionary<string,string>(StringComparer.Ordinal);
            foreach (var value in J.Array(J.Field(index, "files"))) {
                var row = J.Object(value); string name = J.Text(row, "path");
                if (files.ContainsKey(name)) throw new InvalidDataException("Duplicate neural runtime index path.");
                files.Add(name, J.Text(row, "sha256"));
            }
            string profilePath = "profiles/" + HumanVisionAndroidAccelerationSelection.NeuralProfile + ".json";
            var profile = Read(root, files, profilePath);
            var backend = J.Object(J.Field(profile, "backend"));
            var body = J.Object(J.Field(profile, "body"));
            if (J.Integer(profile, "schema_version") != 1 || !J.Boolean(profile, "experimental") ||
                J.Text(profile, "execution_contract") != ExecutionContract ||
                !Exactly(J.Field(profile, "required_capabilities"), "body_pose", "multi_person", "tensor_inference") ||
                J.Text(profile, "profile") != HumanVisionAndroidAccelerationSelection.NeuralProfile ||
                !J.Boolean(profile, "local_evaluation_only") || J.Boolean(J.Object(J.Field(profile,"hands")),"enabled") ||
                J.Boolean(backend,"allow_fallback") || !Strings(J.Field(backend,"preference")).SequenceEqual(new[] { "backend.rknn" }) ||
                J.Text(body,"pipeline") != "pipeline.yolo.tensor" || J.Text(body,"modelPack") != Pack)
                throw new InvalidDataException("Neural profile is outside the admitted experimental contract.");
            string prefix = "modelpacks/" + Pack + "/";
            if (File.Exists(Path.Combine(root, prefix + "modelpack.json"))) throw new InvalidDataException("Ambiguous neural ModelPack manifest.");
            var manifest = Read(root, files, prefix + "manifest.json");
            if (J.Integer(manifest,"schema_version") != 1 || J.Text(manifest,"pack_id") != Pack ||
                J.Text(manifest,"profile_id") != HumanVisionAndroidAccelerationSelection.NeuralProfile ||
                J.Integer(manifest,"max_people") != 8 || !Exactly(J.Field(manifest,"capabilities"), "body_pose", "multi_person") ||
                J.Text(manifest,"pipeline_id") != "pipeline.yolo.tensor" || !J.Boolean(manifest,"local_evaluation_only") ||
                !J.Boolean(manifest,"experimental") || J.Text(manifest,"execution_contract") != ExecutionContract ||
                J.Text(manifest,"profile_sha256") != files[profilePath]) throw new InvalidDataException("Neural ModelPack/profile binding mismatch.");
            var models = J.Array(J.Field(manifest,"models"));
            if (models.Count != 1) throw new InvalidDataException("Neural model requires exactly one body model.");
            var model = J.Object(models[0]); var input = J.Object(J.Field(model,"input_contract"));
            var output = J.Object(J.Field(model,"output_contract")); var options = J.Object(J.Field(model,"backend_options"));
            if (J.Text(model,"role") != "body" || J.Text(model,"format") != "rknn" ||
                J.Text(model,"asset_path") != "body.rknn" || J.Text(model,"sha256") != ModelHash ||
                J.Text(model,"decoder_id") != "yolov8_pose_dfl17_v1" || J.Integer(input,"width") != 512 || J.Integer(input,"height") != 288 ||
                J.Text(input,"color_order") != "RGB" || J.Text(input,"tensor_dtype") != "uint8" || J.Text(input,"tensor_layout") != "NHWC" ||
                J.Text(input,"input_blob") != "in0" || J.Text(input,"crop_mode") != "letterbox" ||
                J.Text(output,"decoder") != "yolov8_pose_dfl17_v1" || J.Text(output,"tensor_dtype") != "fp32" ||
                J.Integer(output,"rows") != 3024 || !Integers(J.Field(output,"columns"),65,51) ||
                !Strings(J.Field(output,"output_blobs")).SequenceEqual(new[] { "out0","out1" }) ||
                J.Text(options,"runtime_library") != "librknnrt.so" || J.Integer(options,"core_mask") != 7 ||
                J.Text(options,"input_layout") != "nhwc" || J.Text(options,"input_type") != "uint8")
                throw new InvalidDataException("Neural model differs from the admitted Low 512×288 contract.");
            var bounds = J.Object(J.Field(output,"max_output_bytes"));
            if (J.Integer(bounds,"out0") != 786240 || J.Integer(bounds,"out1") != 616896) throw new InvalidDataException("Neural output bounds mismatch.");
            var normalization = J.Object(J.Field(input,"normalization"));
            Numbers(J.Field(input,"pad_rgb"),114); Numbers(J.Field(normalization,"mean"),0); Numbers(J.Field(normalization,"norm"),1d/255);
            ValidateEvidence(root, files, prefix, manifest, model);
            string asset = prefix + "body.rknn";
            Verify(root,files,asset);
            if (files[asset] != ModelHash) throw new InvalidDataException("Neural model is not the pinned non-quantized candidate.");
        }
        private static void ValidateEvidence(string root, Dictionary<string,string> files, string prefix,
            Dictionary<string,object> manifest, Dictionary<string,object> model)
        {
            var qualification = J.Object(J.Field(manifest,"qualification"));
            if (!J.Boolean(qualification,"offline_numerical_passed") || J.Boolean(qualification,"deployment_ready") ||
                J.Boolean(qualification,"device_performance_verified") || J.Text(qualification,"source_onnx_sha256") != SourceHash ||
                J.Text(qualification,"validation_index_sha256") != ValidationHash)
                throw new InvalidDataException("Neural qualification must remain the pinned offline experiment.");
            var evidence = J.Array(J.Field(model,"evidence"));
            if (evidence.Count != 2) throw new InvalidDataException("Both saved neural offline receipts are required.");
            foreach (var expected in new[] { new { File="conversion-receipt.json", Field="conversion_receipt_sha256", Hash=ConversionHash },
                new { File="simulator-comparison.json", Field="simulator_comparison_sha256", Hash=ComparisonHash } }) {
                var rows = evidence.Select(J.Object).Where(row => J.Text(row,"path") == expected.File).ToArray();
                string relative = prefix + expected.File, expectedHash = J.Text(qualification,expected.Field);
                if (expectedHash != expected.Hash || rows.Length != 1 || J.Text(rows[0],"sha256") != expectedHash ||
                    !files.TryGetValue(relative,out string indexed) || indexed != expectedHash)
                    throw new InvalidDataException("Neural offline receipt declaration/index mismatch: " + expected.File);
            }
            var conversion = Read(root,files,prefix+"conversion-receipt.json");
            if (J.Text(conversion,"target") != "rk3588" || J.Text(conversion,"toolkit_version") != "2.3.2" ||
                J.Text(conversion,"source_onnx_sha256") != SourceHash || J.Text(conversion,"rknn_sha256") != ModelHash ||
                J.Text(conversion,"precision_request") != "non-quantized" || J.Boolean(conversion,"deployment_ready") ||
                J.Boolean(conversion,"device_performance_verified"))
                throw new InvalidDataException("Neural conversion receipt is outside the admitted non-quantized experiment.");
            var simulator = Read(root,files,prefix+"simulator-comparison.json");
            var identity = J.Object(J.Field(simulator,"identity"));
            var fixtures = J.Array(J.Field(simulator,"fixtures")).Select(J.Object).ToArray();
            if (!J.Boolean(simulator,"offline_numerical_passed") || J.Boolean(simulator,"deployment_ready") ||
                J.Boolean(simulator,"device_performance_verified") || J.Text(identity,"rknn_sha256") != ModelHash ||
                J.Text(identity,"validation_index_sha256") != ValidationHash || fixtures.Length != 3 ||
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
