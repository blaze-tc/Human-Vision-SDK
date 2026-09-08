# Unity following and crash repair — 2026-09-08

User confirmed `4859224-uhd_3840_2160_25fps` follows the actors correctly on
2026-09-08. This is the acceptance checkpoint before live-camera development.

- Overlay invalidation cleared CanvasRenderer materials. The regression failed
  with expected material count 1 / actual 0 (job 50a5bc91); restoring material
  dirtiness passed. Presentation now selects the skeleton's actual source frame.
- GPU initialization crashed with Windows delay-load exception 0xc06d007e.
  The private `hv_dml.dll` was not on Unity's process DLL search path. Native
  initialization now resolves/preloads it beside the SDK module, before ORT's
  provider call. The original Azure DirectML remains untouched.
- `probe_gpu_loading.py` reproduced the loader exception before the fix;
  after the fix, initialization/destruction passed from TEMP with vendor
  DirectML preloaded. Missing private dependency now returns a normal error126.
- CPU Debug/Release CTest: 30/30 each. GPU CTest: 30/30, 10.85 seconds
  (`out/dml-crashfix-tests.log`). Unity final EditMode job 274e6e1d: 39/39,17s.
- Three Unity Play entries succeeded after the fix; no new crash folder and
  console errors0. Two original-PNG captures passed front-row coverage:
  `Screenshots/humanvision-20260908-125618.json` and `...125717.json` in the
  imported Unity project. IDs3,1,5,7 each have17 valid COCO joints; source and
  presentation frame0 match. Screenshot inspected visually. The image is PNG,
  SHA256 B56FFCBE8CA9B0A8072A9622487B6B6E197ABEE6208B08139D41EB85FFEB394F.
- Windows player build:491951811 bytes,9.8077633s; three private SDK/GPU DLLs
  included alongside vendor DirectML. A hidden standalone process was launched
  and stopped without interactive visual verification; no standalone pass claimed.
- Person-only NMS preserves the neural network, but changes top300 selection
  to person-only. Eight sampled clip frames matched original person outputs
  exactly. Python contract suite11/11 passed. Reproduce with
  `.venv-reference/Scripts/python.exe -m tools.reference.prepare_person_detector`.
- Prepare GPU dependencies: `python tools/setup/prepare_directml_runtime.py`.
  CMake options: HV_USE_DIRECTML=ON, HV_ONNXRUNTIME_ROOT=out/hv-ort-dml,
  HV_DIRECTML_ROOT=out/directml-1.15.4. Use v143/Ninja as TOOLCHAIN describes.
  Pinned official ORT1.23.0 and DirectML1.15.4 packages are hash checked.
  The private ORT delay-import modification creates an unsigned private copy;
  publisher packages are unchanged. CPU builds retain ORT1.29.0.

No eight-person dynamic30FPS or rich hand output pass is claimed. Single-image
timing includes cold first inference and is not a stream FPS measurement.
The later final_gpu_video probe used incorrect dimensions and failed with a
truncated raw frame; it contributes no performance evidence.

All tests above preceded the user's subsequent instruction to stop running tests.
