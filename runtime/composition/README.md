# Runtime composition

Purpose: connect built-in registrations, Host workers and common services to the
additive semantic C ABI. Consumes runtime root/profile ID, copied image and region
revision; produces coherent canonical raw/sampled snapshots and diagnostics.
Allowed: Host, ABI, common services, explicit registration functions. Forbidden:
model preprocessing/decoding and Unity types. Algorithms are selected in profiles.
Primary files: session.cpp, runtime_c.cpp. Add a registration here, implementation
inside its plugin. Focused tests: RuntimeSession, RuntimeHost, CommonServices.
Symptoms: whole-session initialization, revision invalidation, worker shutdown.
The masking worker precedes body/hand submissions; it never alters the preview.
Existing diagnostic text now reports profile/pipeline/provider identity, raw,
tracked and sampled counts, body stage timing, result/sample age, adaptive hold,
separate input/body drops and pipeline-specific bottleneck metrics. It is sampled
by the Unity HUD at 4 Hz and does not emit per-frame logs.
