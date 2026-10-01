# Rectangle512x288 offline eligibility

The smaller rectangular input qualifies for an SDK integration experiment,
following independent spec and evidence review. This does not establish FPS.

The existing pinned FP32 ncnn graph, Android runner, output packing and accuracy
limits remain unchanged. The seven-person fixture uses the exact frame1500 RGB
pixels, annotations and video hash of the accepted640 experiment. Its512x288
input has scale0.5, no padding and3,024 anchors. The separate one-person offline
fixture has352x512 input; it does not authorize portrait production support.

Fresh root-owned Android executions:

```powershell
py -3 tools/models/ncnn/yolo_device_gate.py --adb 'D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe' --serial e7c07019 --runner out/android-yolo/runner-android/yolo_golden_runner --root out/android-yolo/resolution-feasibility --gpu-mode gpu-fp32 --fixtures seven-512 one-512
```

Both numerical/person/annotation/provenance gates pass. Seven-person raw maximum
error is0.000104904(out0) and0.000022054(out1), within the unchanged frozen limits.
Actual Vulkan and device CPU outputs each associate all seven people and pass
seven left-raised-arm checks: shoulder5/wrist9 confidence>=0.2, source bounds,
and wrist above shoulder. Lowest actual relevant confidence is0.8489666;
minimum annotation IoU is0.6107672. Original fixture/source/input files remain
unchanged. The reviewer reconstructed offline tensors from source pixels and
recomputed numerical and semantic results independently.

The frozen index is `tools/models/ncnn/yolo_rectangle512_gate_evidence.json`,
SHA256 `fe23f93f39a77ad8fc3f3b78a60dbb728e34749116fcd22478074b5d493dde74`.
It binds the unique device archives, runner, model, fixture, input, log and output
hashes, frozen limits and seven-person semantic evidence. Runner SHA256:
`87b20ad8289eb0b5b449683bb6b096297221195307b1d66b59502cf49b96a14d`.

This is same-converted-graph CPU/Vulkan parity with a single raised-arm frame,
not original PyTorch accuracy or proof of other gestures. Shared offline tensor
preparation does not exercise production GPU-AHB preprocessing. Continuous
integrated Android FPS, alignment and physical acceptance remain pending; the
>=25FPS gate and30FPS target have not passed. No model redistribution, main merge
or Release is authorized. Next: explicit512x288 local pack, exact16:9 geometry,
regressions preserving existing shapes, and current Unity GPU-AHB device test.

The tracked index uses LF line endings matching Git checkout bytes. Its original
ignored CRLF snapshot remains preserved with SHA256
`fd19312d0b80141f87e564aad8a2c97bc5bfd52afd9edfdc48d16a2f33382f74`;
all JSON values and bound archive hashes are unchanged.
