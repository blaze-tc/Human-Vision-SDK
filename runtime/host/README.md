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

## Allowed dependencies
Plugin ABI, common frame buffer/latest-frame slot, C++ standard library, Windows loader.

## Forbidden dependencies
Concrete model/provider implementations, model-native decoder schemas, Unity APIs.

## Primary implementation files
plugin_registry.cpp; runtime_host.cpp; ../include/humanvision_plugin.h.

## Focused tests
`pwsh -File tools/test/run_native_tests.ps1 -Filter 'PluginRegistry|RuntimeHost|plugin_c_abi'`

## How to add/replace an implementation
Implement the versioned query API in a plugin. Register its query statically, or load
an absolute Windows DLL path. Required capabilities are checked before selection.
Do not add recognizer-name switches in Host. Registry module handles remain alive as
long as an instance retains its shared module. Pipeline instances are destroyed first.

## Common symptoms
Missing plugin IDs, API-version mismatch, missing callbacks, outdated frame metadata,
shutdown races, dropped pending images. A slow model belongs to its pipeline/backend.
