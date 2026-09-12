# Legacy core and reusable frame slot

Purpose: retain the exact V1 engine/API regression and provide reusable validated
frame buffers/latest-frame slot to the V2 Host. Consumes packed RGB frames and
legacy config; produces V1 snapshots or owned latest-frame buffers.
Primary files: latest_frame_slot.*, frame_buffer.*, humanvision_c.*, humanvision_engine.*.
Allowed: V1 implementation dependencies. Forbidden: Unity and edits that silently
change V1 layout/symbols. V2 pipeline selection does not belong in this legacy core.
Focused tests: LatestFrameSlot, CApi, api_contract, result snapshot tests.
Symptoms: validation failure, queue growth, old V1 regression. New implementations
must use the semantic composition API instead of expanding the legacy engine.
