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

The existing coordinator waits for either input or Host completion notifications.
It alone mutates tracking, hand merging and region configuration after startup.
CPU and GPU completions use the same publication owner. Neither Copy nor Stats
performs semantic work, so the final inference publishes even with no later frame
and no Unity consumer. Copy retains the existing coherent raw/sampled contract;
sampling does not create a fresh observation.

`ReadObservations(cursor, caller_buffer, capacity)` reads an independent bounded
64-entry canonical journal. Each entry contains the original envelope, native
publication clock, body/hand kind and separate model sequences. The returned
`next_cursor` belongs to that reader; `lost_count` reports overwritten entries.
Journal storage is heap allocated once and slow readers never block processing.
An admitted source/generation/configuration epoch change invalidates old results
and resets per-epoch frame identity checks while preserving cumulative counters.
Region changes immediately invalidate external Copy, then the coordinator applies
the new tracking configuration. Closing stops producers before the coordinator;
no publication mutex is held across joins.

`ErrorCount()` snapshots actual native Host/coordinator failures recorded at
their production site. Reading errors or stats does not increment it. Expected
stale generation/revision drops are separate from execution/provenance faults.
