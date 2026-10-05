# Same-mode model input qualities: Q2 (2026-10-05)

Q2 introduces an indexed, hash-validated Android NCNN Vulkan quality family.
The common selector and RTSP quick presets are Q3; physical deployment is Q4.
This is configuration/profile integration, not a performance acceptance result.

| Quality | Profile | Actual input |
| --- | --- | --- |
| Low | android-ncnn-vulkan-quality-low | 512x288 |
| Medium (default) | android-ncnn-vulkan | 640x384 |
| High | android-ncnn-vulkan-quality-high | 960x576 |

The catalog resolves each actual ModelPack and rejects incorrect geometry,
pinned weights/provenance, FP32 options, capability requirements, duplicated or
unsafe identifiers, hash tampering and cross-backend choices. Original medium
and PC asset bytes remain unchanged. Legacy missing catalogs expose no extra
choices. Runtime Mode remains the build-selected backend, with an exact baked
profile allowlist; no automatic CPU/ORT or size fallback. Native ProfileManager,
RuntimeSession and managed frame submission admit exactly these three GPU IDs.
Vulkan/AHB synchronization, shared Skeleton API and ABI remain unchanged.

Runtime extraction publishes the source index atomically only after every asset
passes validation; failure retains the preceding index. Configuration JSON reads
are outside the frame hot path. No per-frame image readback was added.

## Executed verification

- Catalog/staging behavioral RED then Python GREEN4/4, including four rehashed
  provenance/capability tampering cases. Affected YOLO reference tests128/128.
- Catalog and extraction Unity behavioral RED then GREEN. Final graphics-enabled
  complete isolated EditMode suite 142/142 PASS,0fail,0skip:
  `out/input/quality-q2-project/final-exact.xml`.
- Native new route RED6 failures then GREEN12/12; full Release CTest380/380.
  Actual staged Profile/ModelPack validation24/24 at capacities1..8 passes up to
  the expected unavailable Android bridge on the host; this is not inference.
- Managed exact production route RED2 failures then GREEN11/11; full Runtime
  Windows/Android conditional compilation passed. Final Unity includes route tests.
- Architecture/public-surface and generated component catalog guards passed.
- Clean Android API26/ARM64 candidate native build/audit passed;506 strong imports
  resolved,45 compilation units and404 compiler dependency files recorded.
  Library SHA256: 71b15097f9fd56d1969a0a9a8eb5fb95aa3aeb88f6103b105006cbf06c5afa92.

The native candidate uses HEAD4169b6d archive plus only the three reviewed native
routing overlays. Its one explicit ncnn provenance LF-to-CRLF materialization
matches the original build receipt, with unchanged JSON. All source/dependency/
tool hashes were checked after build; no unrelated dirty source overlay. Candidate
receipt: `out/input/quality-native-q2candidate-20261005/native-build-receipt.json`.
After the scoped commit, the compiled native source is checked against commit blobs.

Scratch dependencies are frozen SDKv15/Inputv13. Final production Editor bytes
match raw UPM source. The pre-existing canonical-only development gate hook remains
outside UPM; no missing gate-class reference or build-validation bypass was added.
The original stronger UPM installer regressions are preserved and synchronized to
canonical tests; both match the final142 scratch fixture after the declared CRLF
to HEAD-LF source materialization (no semantic change). Only the Q2 HostOnly
fail-fast assertion changes their semantics. Original HEAD baseline1/1 proved the
expected fail-fast behavior. Other repaired fixtures match owned bytes.
Earlier logs retain environment-only compilation/duplicate-fixture/cross-volume
failures; these were fixed without suppressing tests or changing production logic.

Commands from the active worktree:

```powershell
py -3.13 -m unittest discover -s tests/reference -p test_model_input_qualities_stage.py -v
py -3.13 -m unittest discover -s tests/reference -p 'test_yolo*.py' -v
cmd /c .superpowers/sdd/2026-10-05-model-input-quality/task-q1/build.cmd
ctest --test-dir build/windows-test -C Release --output-on-failure
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
```

Unity command: Unity2021.3.45f1 `-batchmode -projectPath out/input/quality-q2-project
-runTests -testPlatform EditMode -testResults <absolute final-exact.xml>
-logFile <absolute final-exact.log>`; graphics enabled for renderer regressions.
Android configure/build arguments and audits are preserved in the native candidate.

## Bounds

Fresh real combined stage has20 indexed files; original11 PC/medium files unchanged.
Index SHA256 e1b157578c940e9f1850a0ec1ccb132b05b6dc18aa2f976904184c6afa1114a2;
catalog8b191ccf81be8a47ce5de993bc325f9ad6d218a5c1ec819e16455af844736730.
It still declares native deployment unqualified until package/physical gates.

Broader Python discovery has17 dependency/import errors (onnx/onnxruntime absent)
and is not claimed green. Whole dirty-worktree package isolation has a pre-existing
Demo assembly mismatch; Q4 must verify the actual qualified delivery payload.
The current user Unity project has not been imported or changed by Q2. No APK
installed, hardware FPS/motion/Region acceptance, accuracy promise, main merge,
push, Release or Task11/30fresh complete observation FPS completion is claimed.
