# RKNN backend review evidence

Recorded 2026-10-09 in the `unity-sdk-api-settings` worktree. This is focused
non-hardware evidence. No connected RK3588 inference, numerical parity, NPU graph
coverage, complete skeleton throughput or sustained thermal performance is claimed.

## Review and change

The interrupted implementation already performed real vendor calls through a
private Android loader and had an injectable host resource-contract suite. Review
found one publication defect: a successful `outputs_get` could change a returned
output index, and Run would still publish it under the original queried name/shape.
Run now validates returned indices as well as pointer, bytes and finite FP32 values,
then releases the successful vendor borrow exactly once before returning an error.

Expanded tests cover output descriptor reset after invalid byte counts, zero or
excess IO counts, duplicate/unterminated names, zero dimensions, wrong queried
indices, create/run exceptions, retained model bytes after source-file removal,
16 subsequent buffer reuses and truncated/versioned error-buffer handling.
The host vendor fixture establishes these resource contracts only.

## RED / GREEN

Exact command, run from the worktree root:

```powershell
cmd.exe /d /c out\rknn-toolchain\run-backend-host-tests.cmd
```

That local script invokes the installed MSVC developer environment, compiles
`rknn_backend.cpp`, `rknn_android_vendor.cpp` and `test_rknn_backend.cpp` with
C++17 and the existing pinned GoogleTest libraries, then runs the standalone suite.

- RED before index validation: 13 tests, 12 passed, 1 failed;
  `RejectsReorderedOutputDescriptorsBeforePublicationAndReleasesThem` returned
  HV_OK, output count 2 and a nonnull view instead of rejecting the reordered index.
- GREEN after the fix: 13/13 passed.
- Final GREEN after contract-test expansion: 18/18 passed, process exit 0.

The shared native test CMake list includes this test source. The full native suite
is the integration owner's responsibility after the concurrent tensor pipeline
build completes; it was not run concurrently from this focused review.

## Actual Android compilation

The changed backend source compiled against the actual private RKNN 2.3.2 header
using the Unity-installed NDK; process exit 0 with no warnings or errors:

```powershell
& 'D:\Developer\2021.3.45f1\Editor\Data\PlaybackEngines\AndroidPlayer\NDK\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe' --target=aarch64-linux-android26 -DHV_ENABLE_RKNN=1 -fPIC -std=c++17 -Wall -Wextra -Werror -I runtime -I runtime/include -I native/include -I third_party -I out/rknn-toolchain/vendor/include -c runtime/plugins/backend/rknn/rknn_backend.cpp -o out/rknn-toolchain/rknn_backend_review_android.o
```

The inspected header SHA-256 matches the private CMake gate:
`c48e11a6f41b451a5fd1e4ad774ea60252d3d94f78bee9b21ea3d21b21deba9a`.
This review compile validates Android compilation, not linking, driver loading or
hardware execution. The Android vendor source was unchanged during this review.

## Architecture guard

Exact bundled interpreter used:

```powershell
& 'C:\Program Files\WindowsApps\OpenAI.CodexPrimaryRuntime.v26-1007-641-0_26.1007.641.0_x64__3k8sg7r9htsxt\dependencies\python\python.exe' tools/maintenance/check_architecture_boundaries.py
& 'C:\Program Files\WindowsApps\OpenAI.CodexPrimaryRuntime.v26-1007-641-0_26.1007.641.0_x64__3k8sg7r9htsxt\dependencies\python\python.exe' tools/maintenance/generate_component_catalog.py --check
```

At this focused checkpoint the public-surface contract passed. The architecture
guard returned exit 1 because the shared component catalog was stale and the
concurrent `pipeline.yolo.tensor` component lacked metadata. Catalog `--check`
also returned exit 1 for staleness. `backend.rknn` metadata and README were present.
The integration owner must regenerate the shared catalog after all component
metadata lands and repeat both checks. A plain `python` command resolved to the
Windows Store alias and exited 1 without useful output, so the real interpreter
above was used for the recorded results.

## Integration contract

The shared CMake configuration already includes both source files and the focused
test. Required private gate is `HV_ENABLE_RKNN=ON` for Android, private include
directory `HV_RKNN_INCLUDE`, exact header SHA validation and private `dl` linkage.
Default host builds keep the vendor loader disabled and execute the injected
resource tests. Register `HV_QueryRknnPluginV1` and retain strict profile
`allow_fallback=false` when selecting this backend.

Internal diagnostics publish by instance identity through
`common/backend_diagnostics.h`; the generic factory copies these values without
referencing vendor types. The detailed local `GetRknnDiagnostics` also includes
completed-run count. No V1 layout or public Unity provider type was changed.

Source review confirms create-only JSON/vector allocation, persistent model bytes,
preallocated FP32/native output descriptors, no successful Run buffer growth,
partial context cleanup, release before destroy and unload, and poison-on-release
failure. Vendor-internal allocations and physical driver behavior need device
verification; successful provider setup does not prove all graph nodes use the NPU.
