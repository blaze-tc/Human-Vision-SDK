# 0001: V2 semantic runtime with exact V1 compatibility

Accepted 2026-09-11 under the user-approved 0.4 v2 addendum.

Keep V1 structs/signatures/GUIDs and add a separate V2 handle/config/snapshot API.
The composition root explicitly registers built-ins; generic Host uses only C
tables and capabilities. ModelPack/profile are data. Native schema conversion is
pipeline-local; common services derive only semantic structural joints.

Default pipelines are statically linked on both platforms. Windows dynamic module
loading uses the same ABI and is fixture-tested. QNN is optional and dependency
limited. CPU fallback is observable, never advertised as measured acceleration.

Common services currently share one coordinator-owned implementation file to avoid
duplicate state ownership; identity, temporal, skeleton and region functions are
separate named operations. Splitting these into subcomponents later must retain
the same semantic contract/tests. Raw and sampled buffers remain distinct.

No cache cleanup after the user's explicit deferral. No fake performance pass.
