# Human-Vision-SDK 0.4 Maintenance Documentation Contract

Codex must implement this contract before publishing `v0.4.0-preview.1`.

## Mandatory entry documents

Create:

```text
docs/maintenance/
  START_HERE.md
  CHANGE_MAP.md
  COMPONENT_INDEX.md
  PLUGIN_DEVELOPMENT.md
  MODEL_PACK_GUIDE.md
  PROFILE_GUIDE.md
  UNITY_STABLE_API.md
  DEBUGGING_GUIDE.md
  RELEASE_GUIDE.md
  DECISIONS/
```

### START_HERE.md

This is the first file for every future maintenance session. It must answer:

1. What category is my change?
2. Which directory should I modify?
3. Which directory must I not modify?
4. Which single component README should I read next?
5. Which focused tests should I run?
6. Which architecture guard verifies the stable boundary?

A maintainer should reach the correct component in no more than two documentation hops.

### CHANGE_MAP.md

Maintain a direct task-to-code map. Include at least:

| Need | Modify first | Usually do not modify |
|---|---|---|
| Swap RTMO weights | ModelPack | Unity, Host, tracker |
| Add RTMO-compatible model | ModelPack + Profile | Unity |
| Add new pose algorithm | Pipeline Plugin | Unity, common tracker |
| Add RKNN | Backend Plugin | Unity, pose decoder |
| Change tracking | tracking service | model plugin |
| Change smoothing | temporal service | model plugin |
| Change joint mapping | skeleton service | gameplay |
| Change skeleton appearance | Unity renderer | native recognition |
| Add project preset | Profile | source code |
| Release new runtime | packaging/release docs | gameplay |

### COMPONENT_INDEX.md

For every major Host/plugin/service/model-pack type, list:

- responsibility;
- inputs;
- outputs;
- primary files;
- allowed dependencies;
- forbidden dependencies;
- dependents;
- test target;
- common symptoms that point here.

### PLUGIN_DEVELOPMENT.md

Provide exact recipes for:

- adding a new Pipeline Plugin;
- adding a new Backend Plugin;
- Plugin ABI versioning;
- static Android registration;
- Windows dynamic discovery;
- capability declaration;
- focused tests;
- packaging/registration;
- failure diagnostics.

### MODEL_PACK_GUIDE.md

Provide exact recipes for:

- changing weights only;
- adding a same-family model;
- changing model resolution;
- changing normalization/input layout;
- introducing a new output schema;
- SHA-256 update rules;
- manifest schema version changes;
- determining when a ModelPack change is no longer sufficient and a Pipeline Plugin change is required.

### PROFILE_GUIDE.md

Explain profile composition, validation and examples for Windows realtime, Android realtime, Android precision and future RK3588.

### UNITY_STABLE_API.md

List the supported stable Unity public types/methods. Explicitly mark which API changes would be breaking. State that model filenames, model joint indices and provider classes may not become gameplay dependencies.

### DEBUGGING_GUIDE.md

Use a symptom-first table. Minimum symptoms:

```text
Unity FPS drops
Camera remains smooth but skeleton is slow
Detector is slow
Pose cost increases with people count
IDs swap
Wrong region
HandTip/Thumb missing
Skeleton topology wrong
Result age too high
Requested QNN but actual provider is CPU
Android build succeeds but phone is slow
RTSP behavior differs from WebCam
```

For each symptom list:

- first stats to inspect;
- first directory to inspect;
- focused tests;
- common root causes;
- what not to change first.

### RELEASE_GUIDE.md

Include exact build/test/package commands, version bump locations, artifact names, SHA-256 generation, tag/release process and the rule that hardware performance remains user manual acceptance.

## Component-local README rule

Every plugin and major service root must contain a short README with:

```text
Purpose
Consumes
Produces
Allowed dependencies
Forbidden dependencies
Primary implementation files
Focused tests
How to add/replace an implementation
Common symptoms
```

## Machine-readable component metadata

Registered components must expose metadata sufficient to generate an index:

```text
id
type
version
api_version
capabilities
dependencies
test_targets
```

Implement:

```text
tools/maintenance/generate_component_catalog.py
```

It generates:

```text
docs/maintenance/GENERATED_COMPONENT_CATALOG.md
```

and supports:

```bash
python tools/maintenance/generate_component_catalog.py --check
```

`--check` fails when committed docs are stale.

## Architecture documentation guard

Implement:

```text
tools/maintenance/check_architecture_boundaries.py
```

It must fail when at least these violations occur:

- public Unity source depends on concrete model/provider implementation names;
- public C ABI contains model-specific structs;
- Runtime Host branches on RTMO/RTMPose concrete names instead of capabilities/registered IDs;
- common services include algorithm-specific decoder logic;
- Backend Plugin performs pose-semantic decoding;
- Pipeline Plugin references Unity assemblies;
- ModelPack contains executable algorithm source;
- Profile references a missing/incompatible component;
- registered component lacks metadata or README;
- generated component catalog is stale.

## Required future-session workflow

Document this verbatim or equivalently in START_HERE:

```text
1. Read docs/maintenance/START_HERE.md.
2. Use CHANGE_MAP to identify the component.
3. Read only that component README and its focused tests.
4. Modify that bounded component.
5. Run its focused tests.
6. Run architecture boundary checks.
7. Run full regression/package checks only when preparing a release.
```

The purpose is to avoid requiring Codex or a human maintainer to reread the full repository for every change.
