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
