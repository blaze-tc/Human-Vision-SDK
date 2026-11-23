# Runtime Host

## Purpose
Own plugin lifetime and latest-frame asynchronous scheduling. This foundation is not
itself a recognizer; production plugins are integrated in the subsequent migration.

## Consumes
Versioned C plugin tables, generic frame buffers and validated pipeline configuration.
Start/Stop are serialized control operations. Submit and CopyLatest can run concurrently.

## Produces
Coherent semantic observations with original source frame/time, dropped-frame count,
and actionable initialization/processing errors. Stop invalidates the current output.

## Copied-frame ownership and clocks
The Host copies caller pixels and ROI values before Submit returns. The latest pending
slot holds one complete job. A worker takes the pixels and its fixed-size
`FrameEnvelope` under the same lock. Replacing a pending job cannot change another
worker's active frame or its region revision. The worker returns its old pixel
allocation to the slot when taking another job, which reuses capacity at steady size.

Internal callers can supply source identity, generation, configuration epoch, ROI
revision, timestamps and the source-to-analysis transform. The Host records the
actual copied image layout and its local monotonic submit time. Legacy timestamps
keep their original value and have an unspecified clock domain. Time differences
require valid values from the same identified clock or a separately verified clock
mapping. The internal CopyLatest overload reads the accepted observation and its
envelope together. The C and plugin ABIs keep their existing layouts.

An all-zero crop defaults to the full copied image. Submit preserves explicit
in-bounds crops and rejects invalid rectangles before enqueueing a frame.

## Completion handoff
Composition may install a native completion notifier while a Host is stopped.
The Host reserves one of three completion credits before admitting inference.
Each successful completion owns its observation, envelope and pipeline diagnostics;
`TryTakeCompletion` copies that record and returns the credit. The notifier runs
after Host locks are released and only wakes the existing Session coordinator.
Full completion storage pauses native job admission; a Unity reader is never part
of this handoff. Stop wakes credit waiters before joining producers. Standalone
Hosts without a notifier keep the existing latest-result copy behavior.

GPU completions retain supplied image dimensions, explicit crop, image layout
and valid submission timestamps. Missing fields are filled from the admitted
image contract; a missing submission clock is stamped at actual Host admission.
The native capture-clock proof travels separately in the completion and feeds
age/provenance checks, so capture is never relabeled as submission time.

## Allowed dependencies
Plugin ABI, common frame buffer/latest-frame slot, C++ standard library, Windows loader,
vendored JSON parser and SHA-256 implementation for configuration validation only.

## Forbidden dependencies
Concrete model/provider implementations, model-native decoder schemas, Unity APIs.

## Primary implementation files
plugin_registry.cpp; runtime_host.cpp; ../include/humanvision_plugin.h.
model_pack_manager.cpp; profile_manager.cpp; ../common/config_io.h.

## ModelPack and Profile resolution
ModelPackManager confines manifest and asset paths to the configured pack root,
validates schema/identity/unique model roles and checks each asset SHA-256 before use.
Profiles resolve registered pipeline IDs, matching pack capabilities and configured
body capacity. Capacity branches use configured MaxBodies, never transient detections.
Backend preference lists preserve missing-component diagnostics and deduplicate entries;
`auto` considers backend plugins only, ordered by descending declared priority.
These checks run during initialization, not on the frame-processing hot path.
Model files and configuration strings remain data; Host has no decoder/provider switch.

## Focused tests
`pwsh -File tools/test/run_native_tests.ps1 -Filter 'FrameEnvelope|LatestFrameSlot|PluginRegistry|RuntimeHost|PluginAbi|api_contract|plugin_c_abi'`

## How to add/replace an implementation
Implement the versioned query API in a plugin. Register its query statically, or load
an absolute Windows DLL path. Required capabilities are checked before selection.
Do not add recognizer-name switches in Host. Registry module handles remain alive as
long as an instance retains its shared module. Pipeline instances are destroyed first.

## Backend sessions
BackendFactory exposes HostServices from an immutable ordered candidate list.
Creation failures try the next candidate; partial instances are destroyed and
the failed plugin IDs/reasons are included in session diagnostics. Returned leases
retain modules independently of factory/registry lifetime. Keep the factory alive
while callers may create more sessions. Release does not dereference factory state.
The generic dispatcher does not retry inference on another provider after a run
failure; provider-specific recovery remains inside the backend (currently NNAPI).
No model/provider ID switches belong in this dispatcher.

## Optional image preparation

An immutable PluginModule can carry a separately queried
`HV_PipelineImageExtensionV1`. Registration advertises capability and does not
enable another pixel copy. Start/StartPool keep legacy dispatch by default;
the control owner must explicitly opt into prepared image jobs. Only then does
the existing Host worker run DescribeTransform,
the selected image service and ProcessPrepared inside the same admitted job;
its envelope, completion credit and publication behavior remain unchanged.
Composition may supply any valid `HV_ImageServiceApiV1` table with a context that
outlives Stop. Once opted in, the default provider is the bounded synchronous CPU
oracle. The Host does
not select model geometry, normalization or a vendor implementation.

Legacy modules without an extension call their existing process function. Image
support failure is explicit, and does not skip legacy model preprocessing. A
prepared lease is always explicitly retired, also on pipeline errors/exceptions.
Cancellation cannot free an uncompleted operation: Stop remains on its native
owner until actual release is acknowledged, allowing the deferred controller to
report ErrorRetiring/BUSY instead of unloading resources still used by a device.
Profiles may explicitly disable fallback. In that mode BackendFactory passes the
single selected plugin identity as the requested provider and stops after its first
creation failure. RuntimeHost also copies internal pipeline metrics with each
accepted observation; these metrics do not alter the plugin ABI.

## Common symptoms
Missing plugin IDs, API-version mismatch, missing callbacks, outdated frame metadata,
shutdown races, dropped pending images. A slow model belongs to its pipeline/backend.
