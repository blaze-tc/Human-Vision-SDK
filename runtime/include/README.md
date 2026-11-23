# Versioned plugin ABI

## Purpose
Share one C-compatible contract between statically registered Android plugins and
optional Windows DLL plugins. No C++ object, allocator ownership or exception crosses it.

## Consumes
A requested API version and caller-owned structures carrying struct_size/api_version.

## Produces
Immutable plugin metadata and pipeline/backend callback tables. Pipeline observations
use canonical semantic joints; tensor views carry no pose-specific meaning.

## Allowed dependencies
Stable HumanVision public types and C integer types only.

## Forbidden dependencies
C++ STL/classes, platform inference libraries, model decoding, Unity.

## Primary implementation files
humanvision_plugin.h; ../../native/include/humanvision/humanvision_v2.h.

## Focused tests
plugin_c_abi; PluginRegistry.*; RuntimeHost.*.

## How to add/replace an implementation
Export HV_QueryPlugin with HV_PLUGIN_BUILD_DLL for a Windows DLL. For static builds
use a unique query function and register its pointer. Keep returned tables/metadata
alive for the module lifetime. Copy retained configuration strings during create.
Create/destroy an instance on its owning plugin heap. Each instance has one worker;
different instances may execute concurrently. Backend outputs are borrowed until the
next run/destroy. Caller owns frame/input/output view arrays. Never retain frame pixels.

## Common symptoms
Calling convention mismatch, truncated tables, stale tensor pointers, cross-heap frees.
Changing an existing field requires a new ABI version rather than silently reordering.

## Optional image service and prepared pipeline extension

`humanvision_image_service_v1.h` is an independent C table; it adds no fields to
Plugin ABI1/2/3 or HostServices. An image descriptor declares memory, actual packed
layout, completion, source/generation/frame/configuration/region identity and clock
value. PRIVATE/opaque memory must never be guessed to be readable RGB.

`HV_ImageServiceApiV1` provides QuerySupport, SubmitTransform, WaitTransform,
CancelTransform and ReleasePrepared. Source pointers are borrowed only during
Submit; prepared storage stays owned by its service until explicit Release.
Cancellation is a request: a pending operation cannot release/reuse its slot until
actual producer completion. A selected service context must outlive Host Stop,
including a retiring device fault. The CPU oracle executes synchronously on the
existing Host worker and owns three bounded reusable output buffers.

`HV_QueryPipelineImageExtensionV1` is an optional dynamic-module symbol. Static
composition registers each unique query against a pipeline ID before immutable
plugin use. Registration does not enable prepared dispatch; the Host's default
remains legacy processing until its control owner explicitly opts in.
DescribeTransform and ProcessPrepared use the same existing pipeline
instance and worker. A prepared lease binds plan/model-profile fingerprints,
source identity, inverse transform and explicit normalization counts. These are
compatibility fingerprints, not cryptographic authentication. A mismatch or
uncompleted/cancelled lease is rejected before inference.

Focused tests: `ImageServiceAbi`, `PreparedImageContract`, `image_service_c_abi`.
