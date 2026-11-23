# Deferred runtime control

`HV_QueryRuntimeControlApiV1` returns the additive table declared in
`humanvision/humanvision_runtime_control.h`. Its opaque controller handle is
different from the historical `HV_RuntimeHandle`; never pass it to an old
`HV_Runtime*` entry point. All old structures and synchronous Create/Destroy
semantics remain unchanged.

CreateDeferred validates and copies bounded strings/configuration, and creates
one control owner. It performs no model load, device open or source I/O.
RequestStart/Source/Pause/Resume/Stop/Reconfigure/Close enqueue at most16 commands;
64 bounded request records preserve recent status. Control commands may allocate;
frame processing reuses its pixel buffers and the existing Session/Host workers.

The owner opens the source, starts the existing RuntimeSession, and starts the
single CPU ingestion owner when needed. Android GPU input is claimed directly by
GpuRuntimeHost through the source-neutral consumer adapter. Windows CPU uses
the independent Input DLL's RGBA copy API. Android CPU/RKNN uses the native
Vulkan producer's fence-proven staging copy, not Unity readback. An absolute
Windows DLL name is normalized before dependency resolution. The Input module
does not depend on Runtime or a model.

The input clock is sampled between two Runtime monotonic readings. Offset and
half-bracket uncertainty are reported. Decoded local time is mapped to Runtime
monotonic time; PTS and received/decode times keep explicit validity and domains.
Local decode time is not a verified sensor capture timestamp. The job envelope
carries source ID, source generation, frame ID and configuration epoch together.

An active native source rejects compatibility Submit. IndependentRequired fails
without a supported native source; it never silently starts Unity ingestion.
Capability level0 means uninitialized. Capabilities distinguish native input,
image service and actual backend. Native Vulkan image preparation still uses
the GPU even when inference runs on CPU or NPU.

Pause/Stop/Reconfigure/Close close public admission immediately. The control
owner retires ingestion, then the one-shot Session and its imports, then the
source and Input module. Resume/reconfigure create a new Session/epoch instead
of resetting a terminal latest-frame slot or overlapping RKNN contexts.
No publication/query lock is held during joins or driver waits. Copy and journal
read copy into caller-owned storage; they cannot hold source credits.

Request deadlines do not imply hardware completion. A blocked owner reports
ErrorRetiring/Timeout, keeps its handle and libraries, and prohibits conflicting
replacement contexts. TryRelease returns BUSY until Close has actually retired
the owner; only OK invalidates the handle. A permanently blocked vendor call
therefore requires process restart. The final TryRelease must be serialized
against every other API call.

The first source binding supports native RTSP only. Windows native orientation
changes are explicitly unsupported; Android requires the platform-selected
hardware AVC codec. Preview/snapshot table entries return Unsupported until a
separate owned preview mailbox is connected. Unsupported preview is not a claim
that a native decoder lease can be borrowed across Unity frames.

Focused checks: RuntimeControl, NativeSourceBinding and runtime_control_c_abi.
The controlled blocked-initialization fixture checks resource ownership, not a
real vendor driver's permanent-hang behavior. The optional Input DLL integration
test verifies single ingestion ownership on a disconnected source; it does not
establish RTSP throughput. A real source/harness and physical device checks are
separate acceptance gates.
