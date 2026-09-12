# ModelPack changes

Each `modelpacks/ID` has manifest.json, assets, README, SOURCES and component.json.
ModelPackManager checks schema=1, identity, capabilities, capacity, confined paths,
unique roles and SHA-256. Profiles reference the pack ID, never an absolute model.

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
