# PC evaluation Task 2 — 2026-10-01

New folder `upm/com.blazetc.humanvision.pc-demo`, package identity
`com.blazetc.humanvision`, version `0.4.0-pc.1`. Exclusive Windows evaluation
installation; original UPM and Android work unchanged. No main merge or Release.

Fresh implementer and independent spec/code quality review: PASS. Root rerun:
`py -3.13 tools/test/test_pc_demo_package.py`: 11/11 PASS, 4.379s.
Tests use real payloads and mutation: model/profile/hash tampering, duplicate GUID,
unsafe destination/path, absent native closure, non-PE/non-AMD64 input and unknown
actual PE import. Missing implementation and two review fixes recorded RED before
GREEN. Development Android GPU gate excluded; VC++ x64 prerequisite documented.

`py -3.13 tools/package/package_pc_demo.py --native-inputs out/pc-demo/native-inputs.json`
PASS: 149 manifest assets plus manifest, 82 unique metadata GUIDs, eight actual
AMD64 Windows libraries with ordinary/delay-load import closure, seven indexed
runtime files from RTMO-t416 and two explicit CPU/DirectML body-only profiles.
Installer is the existing narrow text-repair-to-exact-indexed-hash implementation;
model bytes remain strict. Metadata is independent from installed StreamingAssets.
`.gitattributes` preserves byte-pinned package data. Generation repeats identically.
Native model/DLL versions and hashes are in the public `PROVENANCE.json`; no personal
cache paths are published. Package manifest SHA256:
ff0b1d72ca0961a352b767af92501d15d96b5bcb6ea8dc9f9bff10f333689896.

Task 3 remains required: new clean Unity import, actual video/skeletons, Windows
player build and pinned remote Git import. Static packaging does not establish
runtime/performance, camera hardware, network or Android acceptance.
