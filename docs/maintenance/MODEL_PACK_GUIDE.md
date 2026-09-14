# ModelPack changes

Each `modelpacks/ID` has exactly one manifest: schema 1 uses `manifest.json`, while
schema 2 may use `modelpack.json` (or `manifest.json` during migration). A pack must
not contain both filenames. It also contains assets, README, SOURCES and
component.json. ModelPackManager checks the supported schema, identity,
capabilities, capacity, confined paths, unique roles, complete contracts and
SHA-256. Profiles reference the pack ID, never an absolute model.

For weights-only replacement: copy the pack to a new ID, update pack_id/version,
source/license evidence and the asset SHA-256 (`Get-FileHash -Algorithm SHA256`).
Keep the pipeline ID, decoder ID and actual tensor contract compatible. Update the
profile and run the real plugin golden plus ModelPack tests. Do not edit Unity.

For same-family resolution changes: change width/height only if the exported graph
and selected pipeline support them; current limits are 32–2048. RTMO uses centered
letterbox; TopDown/Hand use an affine RGB crop. Resolution is not a cosmetic flag.
Current normalization is fixed by those pipeline implementations. Changing channel
order, mean/std or tensor layout needs pipeline support and a new golden test;
adding an ignored manifest field does not implement normalization changes.

For a new output schema, add/extend a pipeline decoder and declare the corresponding
decoder ID. Do not map model indices in gameplay or common services. Shape/count
checks must fail usefully for incompatible graphs. A new algorithm is a plugin
change, not a misleading ModelPack-only change.

Every asset byte change requires a new hash and provenance update. Never reuse a
hash from the checkpoint for an exported graph. Nano export/fix scripts are in
tools/reference; its person-only output transformation is documented in SOURCES.
Schema changes require parser and invalid-manifest tests before emitting a new
schema version. Rebuild runtime index and UPM artifacts after every pack change.

Schema 1 remains supported from `manifest.json` without field or behavior changes.
Revision 2 packs may use `modelpack.json`; `manifest.json` remains readable during
the migration. Schema 2 represents each ncnn model as two named files (`param` and
`bin`); it does not reuse the
schema-1 `asset_path`. This complete example uses fixture-byte hashes only to show
the contract shape. Production values must come from the accepted converted files.

```json
{
  "schema_version": 2,
  "pack_id": "precision-t-26-ncnn-fp16",
  "pack_version": "2.0.0",
  "pipeline_id": "pipeline.topdown",
  "capabilities": [
    "body_pose",
    "multi_person",
    "gpu_input",
    "vulkan",
    "fp16-storage",
    "fp16-arithmetic",
    "android-hardware-buffer",
    "external-sync-fd"
  ],
  "max_people": 2,
  "models": [
    {
      "role": "detector",
      "format": "ncnn",
      "decoder_id": "rtmdet_nano_raw_v1",
      "param_path": "detector/model.param",
      "param_sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "bin_path": "detector/model.bin",
      "bin_sha256": "cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34",
      "input_contract": {
        "image_format": "rgba8-unorm",
        "color_order": "rgb",
        "normalization": {
          "mean": [0.0, 0.0, 0.0],
          "norm": [0.0039215686, 0.0039215686, 0.0039215686]
        },
        "tensor_dtype": "fp16",
        "elempack": 4,
        "width": 320,
        "height": 320,
        "input_blob": "in0"
      },
      "output_contract": {
        "decoder": "rtmdet_nano_raw_v1",
        "output_blobs": ["cls", "bbox"]
      },
      "source": "Pinned upstream model and source revisions; see SOURCES.md",
      "license": "Record the exact model, code, and dataset terms",
      "conversion_recipe": "Record exact export, pnnx, and ncnnoptimize commands"
    }
  ]
}
```

Both paths must be confined to the pack root and both hashes must match before the
pack resolves. `image_format`, `color_order`, three-value `mean` and `norm`,
`tensor_dtype`, `elempack`, dimensions, `input_blob`, and nonempty unique
`output_contract.decoder` equal to `decoder_id`, and nonempty unique `output_blobs`
are mandatory. Schema 2 also requires `vulkan`, `fp16-storage`, and
`fp16-arithmetic`; initialization names a missing field or capability in its error.
