# Common body services

Semantic observations enter `BodyServices` on one coordinator thread. Fixed-size
Hungarian assignment combines boxes, position and common valid joints. Track IDs
survive observation ordering; region indices are independent occupancy slots.
Changing the region revision resets tracking and rejects old observations.

Raw snapshots retain observation timestamps. Sampled snapshots apply an adaptive
low-pass filter and at most 25 ms of velocity prediction; observations older than
200 ms are excluded. Sampling does not increase measured inference FPS. Missing
structural joints may be explicitly marked derived; hands are never fabricated.
Hand requests rotate across both sides of all active tracks; independently timed
hand observations merge only into the matching track and revision.

`MaskRegions` zeros pixels outside the configured union in a reusable inference
buffer before pipeline execution. The original camera image is preserved.

Acceptance: `tools/test/run_native_tests.ps1 -Filter CommonServices`.
Hardware latency and crossing/occlusion quality require physical acceptance.
