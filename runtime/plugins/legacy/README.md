# Legacy recognition pipeline

Transitional adapter proving that the generic Host can execute the existing real
detector/pose implementation through the C plugin ABI. Register
`HV_QueryLegacyPipeline`; the registered ID is `pipeline.legacy`.

Consumes a validated manifest with `detector` and `body` asset roles and a borrowed
frame. Owns reusable frame/model buffers. Produces semantic body observations with
source timestamps; does not assign track IDs, regions or presentation indices.
COCO-17 joints are mapped inside this adapter; unsupported canonical joints remain
invalid. No fabricated hand points or temporal samples are emitted.

This migration adapter obtains both model sessions through HostServices; it no
longer instantiates a concrete execution backend. BackendFactory tries the configured
candidate order and leases the selected plugin. The adapter is not the final 0.4
default. Requested ROI processing is explicitly rejected; full-frame
region masking belongs to common services.

Allowed dependencies: plugin ABI, shared configuration utilities, existing native
recognizer/backend implementation. Forbidden: Runtime Host implementation, Unity,
tracking/region ownership. The Host must never include this adapter header.

Verification: `tools/test/run_native_tests.ps1 -Filter LegacyPlugin` runs real
checked-in models/image data through RuntimeHost and compares a semantic joint to
the locked reference. This is an offline functional test, not phone FPS acceptance.

Failures: missing model roles/assets fail creation with an error string; invalid
frames/capacity fail processing. Exceptions are contained at the C boundary.
