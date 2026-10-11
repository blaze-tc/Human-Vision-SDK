# RKNN tensor backend

## Purpose

`HV_QueryRknnPluginV1` exposes `backend.rknn` through the existing ABI1 tensor
backend table. It creates a real RKNN context and executes tensors on Android
through the privately pinned RKNN 2.3.2 runtime. The default build leaves the
vendor loader disabled. This component is experimental; connected RK3588
driver, numerical parity, graph coverage and sustained throughput remain
physical acceptance work.

## Consumes

An ABI1 backend config with a readable RKNN model and an optional create-time
JSON object:

```json
{"runtime_library":"librknnrt.so","core_mask":7,"input_layout":"nhwc","input_type":"uint8"}
```

Defaults are shown above. `core_mask` accepts only 1 or 7; unknown keys, malformed
JSON and other layouts/types fail. The optional requested provider must be empty
or `backend.rknn`. Configuration is parsed only at creation; the session retains
the model bytes until after context destruction.

Run accepts one tightly packed RGB UINT8 tensor `[1,H,W,3]`. Actual queried
NCHW or NHWC model attributes determine H and W; a mismatched shape/name/byte
count fails before any vendor execution. `rknn_inputs_set` receives UINT8 NHWC
with `pass_through=0`, allowing the runtime's compiled input conversion. The
baseline backend does not divide pixels by 255 or define model normalization.
The pipeline may supply an explicit private `input_transform` object containing
three `mean` and three `norm` values from its validated model contract. This is
parsed once; it does not change the public UINT8 input contract or model bytes.

## Qualified I/O optimization (private experiment)

`HV_RKNN_INPUT_MEMORY_EXPERIMENT` plus `input_memory:auto` (or the existing
device comparison override) creates an independent candidate context. It tries:

1. Queried native NHWC FP16 input with pipeline-supplied normalization and shared
   FP32 outputs. A 3×256 lookup table combines normalization and half conversion;
   row/channel padding follows the query, is zeroed, and stays within 4 MiB.
2. Shared UINT8 input plus shared FP32 outputs, retaining compiled normalization.
3. Shared UINT8 input with the original `outputs_get` path.

Each candidate must match every FP32 output bit on the same frame, including two
warm-ups, and improve total backend time by at least 5% over five measured pairs.
Qualification publishes only the baseline result. Unsupported layouts, binding
errors, output differences or inadequate gains try the next candidate. An error
after qualification reruns that same input on the independent baseline context.
This is bounded preferred-path selection, not an exhaustive fastest-route search.

Input/output memories are allocated and bound once per candidate; steady-state
runs use explicit TO_DEVICE/FROM_DEVICE cache sync and reusable buffers. Dense
FP32 shared outputs follow the vendor API example; blocked layouts are rejected.
The native input uses pass_through=1 only with validated FP16/NHWC/nonquantized
attributes. No camera/gralloc layout is guessed and RGA remains disabled.
Candidate transitions can allocate/recreate on the worker during qualification;
the stable path does not grow application buffers. Vendor allocations remain
outside the application's control.

`input_path` logs selected/testing path, current comparison counts/times and the
last rejected candidate/reason/times. `native-fp16-io-qualified`,
`shared-io-qualified` and `shared-input-qualified` identify accepted routes;
`legacy-*` identifies qualification or fallback, never a claimed optimization.

The PC RKNN simulator accepts an explicit FP32 normalization control, but rejects
native FP16 input. Its result cannot qualify the native memory path. RK3588
driver correctness and sustained per-person throughput require the device gate.
See [throughput evidence](../../../../docs/reports/2026-10-11-rknn-native-io.md).

## Produces

Caller-owned output view arrays referencing reusable backend-owned FP32 buffers.
Actual queried output names, dimensions and order are preserved. Output retrieval
uses `want_float=1` and `is_prealloc=1`; native FP16 tensors and unspecified
output layout do not require a guessed transposition. No model-specific output
shape is hard-coded. Output names must be unique; ranks are bounded to 1-8,
one output to 64 MiB and total FP32 output storage to 128 MiB.

Views are borrowed until the next run or destruction. An insufficient caller
capacity fails before input submission. Failures publish count zero and clear
the applicable output views. Changed returned index/pointer/byte count and
nonfinite values fail before publication. Every successful vendor output borrow
is released exactly once, including validation failures. A release error poisons
the instance until destruction; destruction does not repeat a failed release.
Context destruction precedes runtime unload and model-memory retirement.

Session info reports requested/actual `backend.rknn`, accelerated=1 only for a
successfully created context, and no fallback. This indicates the configured
provider; it does not prove every graph operation used the NPU. Compiled graphs
may contain CPU output operations. No CPU/Vulkan replacement occurs here.

Private `GetRknnDiagnostics` exposes runtime/driver versions, core mask, completed
runs and segmented initialization/input-set/run/output-get/output-release timing.
The same value snapshots are published through `common/backend_diagnostics.h`
for the generic host diagnostics registry without extending ABI1. Calls on one
instance must be serialized by its owning worker.

## Allowed dependencies

Plugin ABI, internal diagnostic values, standard C++17 facilities, create-time
JSON parsing and the private RKNN vendor header/Android dynamic loader.

## Forbidden dependencies

Pose decoding, body/hand semantics, identities, regions, Unity objects, Host
implementation classes and vendor types in the public C/Unity API.

## Primary implementation files

`rknn_backend.cpp` owns validation, reusable buffers, lifetime and ABI callbacks.
`rknn_android_vendor.cpp` owns actual `dlopen`/symbol resolution and RKNN calls.
`rknn_vendor_boundary.h` is an internal injectable resource-operation boundary.
`rknn_backend.h` declares the static query and private diagnostics.

## Focused tests

`RknnBackend.*:RknnLibrary.*` in `humanvision_native_tests`. These tests inject a
vendor resource fixture into the real backend to check validation, borrow/release,
partial creation cleanup and unload order. Fixture values establish resource
contracts, not device inference or performance. See
[implementation evidence](IMPLEMENTATION_EVIDENCE.md) for fresh commands/results.

## How to add/replace an implementation

Compile both backend sources and register `HV_QueryRknnPluginV1` at the composition
root. Private Android ARM64 builds explicitly enable `HV_ENABLE_RKNN=ON`, use
`HV_RKNN_INCLUDE` pointing to the pinned private header directory, and link `dl`.
The CMake gate verifies the vendor header SHA-256 and rejects non-Android builds.
Private vendor headers, runtime libraries and compiled models remain outside Git
and public release packages. The backend dynamically loads the configured runtime
and requires all eight RKNN resource symbols; missing libraries/symbols fail with
an actionable error. Initialization uses zero flags for current synchronous
results, without asynchronous previous-frame output or performance-collection
flags. The Host still schedules this work off the Unity main thread.

Change tensor execution here; change normalization, model-specific output
interpretation and pose observations in the pipeline/model-pack contract.
Register a different provider ID for a replacement execution technology rather
than changing the frozen ABI1 layout.

## Common symptoms

- Unavailable loader: inspect the explicit Android build gate and private assets.
- Missing symbol/dlopen failure: inspect the exact pinned runtime, ABI and path.
- Input mismatch: inspect queried format/dimensions and pipeline RGB byte count.
- Attribute or output validation failure: inspect model/runtime compatibility;
  do not accept malformed outputs or guess an output transpose.
- Release failure: retain the error and destroy/recreate the poisoned session.
- Slow results: compare input-set, run, output-get and output-release stages;
  verify real device graph coverage and completed-result speed separately.
