# Plugin recipes

Read [ABI README](../../runtime/include/README.md) first. ABI1 structures require
struct_size and api_version; validate both before touching trailing fields.
Never pass std::string, vector, exceptions or allocator ownership across a DLL.
Caller owns output buffers; tensor results are borrowed until the next Run.

## New pipeline

1. Add runtime/plugins/pipeline/NAME with README.md and component.json.
2. Implement create/destroy/process using the C table. Consume manifest and
   HostServices backend factory; emit semantic BodyObservation/HandObservation.
3. Add Query function and declare capabilities/max_people truthfully. Model-native
   index mapping belongs here, not Unity or common services.
4. Register the query in runtime/composition/session.cpp (static Android/default
   Windows build). Add the source to runtime/CMakeLists.txt.
5. Add a real-data fixture and failure tests to tests/runtime; register the test
   source in tests/native/CMakeLists.txt. Run its filter and RuntimeSession.
6. Add a data-only ModelPack and compatible profile, then regenerate the catalog.

## New backend

Follow runtime/plugins/backend/ort. Implement create/destroy/run/session_info.
Do not decode pose or assign identities. Report actual provider only after session
creation. A configured accelerator does not prove graph coverage. Creation failure
returns an actionable error and allows BackendFactory to try the next candidate.
Preserve partial-create cleanup and module lifetime tests. Publish accurate priority
and capabilities, register it at the composition root, then test fallback explicitly.

## Dynamic Windows deployment

Export `HV_QueryPlugin` with HV_PLUGIN_BUILD_DLL using exactly the ABI header and
calling convention. `PluginRegistry::Load(absoluteDllPath,error)` loads the table
and retains the module until all leases are gone. The dynamic fixture in
tests/runtime/fixture_plugin.c is the executable example. Default profiles use
statically registered built-ins; new dynamic modules must be explicitly loaded by
the composition/bootstrap before resolving the profile. No arbitrary DLL scanning.
Package the plugin and its private dependencies with platform import metadata.

Additive fields require size checks and a deliberate ABI version policy. Breaking
table/layout changes require a new ABI, never reinterpret V1. V1 public C structs
and Unity signatures remain frozen. Run plugin_c_abi, RuntimeHost, BackendFactory,
the plugin fixture, architecture guard and package isolation before publication.
