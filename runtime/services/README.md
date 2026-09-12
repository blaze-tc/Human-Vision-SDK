# Common body services

Semantic observations enter `BodyServices` on one coordinator thread. Fixed-size
Hungarian assignment combines boxes, position and common valid joints. Track IDs
survive observation ordering; region indices are independent occupancy slots.
Changing the region revision resets tracking and rejects old observations.

Raw snapshots retain observation timestamps. Sampled snapshots apply an adaptive
low-pass filter and velocity prediction only through observation age 25 ms. After
that horizon they hold the most recent filtered position without extrapolation.
Body render hold starts at 500 ms; after two accepted observation frames it is
`clamp(observation-period EWMA * 2.5, 300 ms, 800 ms)`. The cadence EWMA uses alpha
0.2 and includes accepted empty frames; rejected/stale frames do not affect it.
Track identity is retained for `max(800 ms, render hold)` and expires only when
processing a new observation. Body presentation expiry does not mutate Raw().
Hand point validity and hand request eligibility retain their independent 200 ms
expiry. Sampling does not increase measured inference FPS. Missing
structural joints may be explicitly marked derived; hands are never fabricated.
Hand requests rotate across both sides of all active tracks; independently timed
hand observations merge only into the matching track and revision.

Internal `Diagnostics(now_us)` exposes cadence, hold, track lifetime, sample age,
tracked/sample counts and Predicted/Held/Stale state. State/age describe the
freshest active track when multiple bodies have different observation times;
before any track exists the state is Stale and sample age is -1.

`MaskRegions` zeros pixels outside the configured union in a reusable inference
buffer before pipeline execution. The original camera image is preserved.

Acceptance: `tools/test/run_native_tests.ps1 -Filter CommonServices`.
Hardware latency and crossing/occlusion quality require physical acceptance.
