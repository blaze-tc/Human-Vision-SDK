# Integrated YOLO output boundary correction

The first actual Unity/VideoPlayer/AHB/ncnn run used APK
`c8e8f944dce69f4c9f8452490ca99035beb1fe36bd5b870fee9ee56a7ec62fa0`,
native `fefaad0ae7186febcba781880bb0cc38a37aad5254e347671505249ca6aeeeb5`,
and the pinned video1 from37s on Snapdragon888 e7c07019. The75s collection
completed but produced zero bodies: `ncnn downloaded output violates shape/dtype
bound`. Video source and AHB imports succeeded; full-frame CPU readbacks were0.
This was a failed integration gate, not a performance result.

Pinned ncnn `command.cpp` recomputes destination packing in `record_download`.
For rank2 output height2100, internal `use_packing_layout=true` chooses CPU pack4
even after an explicit GPU FP32pack1 conversion. The same verified offline
runner already disables packing in its separate download options.

Both production output download sites now copy the inference options and
disable only download packing, FP16 storage and FP16 packed storage. Network
internal packing/precision and allocator references are retained. Strict output
shape, dtype and byte limits are retained; no full input readback or fallback.

Tests reproduce6 RED failures before the fix and6/6 GREEN after, compiling actual
production snippets with pinned ncnn destination-packing policy. Native focused
ncnn/YOLO31/31, plugin ncnn4/4 (legacy ModelPack2/2), existing submission/lifetime
8/8 and public surface/architecture checks PASS. Fresh independent GPT-6.1 Sol
medium spec/quality review repeated6/6 and8/8 plus guards: PASS, no findings.
The reviewed HEAD-only patch excludes all preexisting R4 changes.

Separate Android API26 ARM64 build succeeds with parity/gate/trace flags OFF.
New library SHA256:
`9532cc31423e23a7f9258683d0bf6d76eac97ec2303f0ef4bf611bb8909ce6fa`.
Root audit verifies API26, static ncnn/AHB/Vulkan symbols and1813 strong imports.
Actual device recovery, input alignment and25/30FPS acceptance remain pending;
the next package must bind this new library, not the failed artifact above.
