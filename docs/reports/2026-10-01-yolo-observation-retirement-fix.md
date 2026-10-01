# YOLO GPU observation retirement correction

The second integrated run, after the download-boundary correction, used APK
`f2a65fa091a66aab8a7116fd1fa1403faf277cd993c1f4c7f9ac1e09ecd70d8b`
and native `9532cc31423e23a7f9258683d0bf6d76eac97ec2303f0ef4bf611bb8909ce6fa`.
The verified seven-person video completed a75s Snapdragon888 capture. One
network execution completed, but zero observations were published: the host
reported `V3 GPU completion unproven; source generation quarantined`.
This is a failed integration gate, not a performance result.

`RunImage` installs a one-shot consumer completion callback. The YOLO success
path returned without invoking it; the host correctly rejected that active
lease. The pipeline now invokes the existing final-role dispatcher after valid
decode and before publishing a body count, including empty successful results.
The actual Android callback submits the external-image ownership release and
retires the slot. Missing or failed completion returns an error. Decode/run/
exception failures retain the existing host error-drain contract. No bridge
safety check, sync-fd, ownership transfer, input readback or fallback is changed.

The new regression uses a real `ConsumerFrame` and production one-shot
dispatcher, with seven success/error scenarios. Its RED run failed before the
production change; the final lifecycle suite passed92/92, including the existing
warmed zero-allocation check. Separate API26 ARM64 Android build and native
audit passed, resolving1813 strong imports. New native SHA256:
`f3ab016bbe1d5dda83142a0a8f07e1840fe6eecd8dd30bee5896ac5c25568289`.

Evidence is retained under `out/android-yolo/lease-retirement-regression/`.
Fresh independent GPT-6.1 Sol medium spec/quality review passed34 focused tests
and58 bridge/composition/V3 tests, plus architecture/public-surface guards.
`postprocess_ms` includes final ownership-release completion in this pipeline;
it must not be interpreted as pure CPU decode time.
Actual device recovery, visible joint alignment and25/30 fresh complete-frame
acceptance remain pending. YOLO-M3 remains active; no main merge or Release.
