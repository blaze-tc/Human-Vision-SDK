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

The bounded observation journal belongs to the controller for its entire
lifetime. Session rebuilds for Pause/Resume, source changes and reconfiguration
share that journal; its monotonic cursor never resets. Retained observations
remain readable while paused/stopped and carry their original configuration
epoch. Independent readers retain their own cursors and receive explicit
overrun/lost counts when more than64 entries have replaced their history.

Request deadlines do not imply hardware completion. A blocked owner reports
ErrorRetiring/Timeout, keeps its handle and libraries, and prohibits conflicting
replacement contexts. TryRelease returns BUSY until Close has actually retired
the owner; only OK invalidates the handle. A permanently blocked vendor call
therefore requires process restart. The final TryRelease must be serialized
against every other API call.

The first source binding supports native RTSP only. Windows native orientation
changes are explicitly unsupported; Android requires the platform-selected
hardware AVC codec. The optional `HV_NATIVE_SOURCE_LIVE_PREVIEW` flag enables
native-owned preview copies. The pool has three immutable buffers; readers copy
into caller memory and never retain a decoder/analysis lease. A full or slow
reader pool skips new preview work. A copy reservation is not retired by cancel:
the owner must observe the actual completion before reuse.

Windows fans out its already-owned analysis-size RGBA at source cadence; each
ready frame may attempt a bounded three-buffer copy, and pool/copy pressure
skips optional preview work. There is no fixed 5 FPS throttle on this path. These
pixels are not original-resolution HD, and original coded dimensions remain
unavailable (zero). Android's original-color HD path caps live copies at 5 FPS,
reducing them to 1 FPS under admission pressure. It requests one full visible-color
capture from the image already admitted for analysis. It does not acquire an
extra decoder image. The native Input image service submits analysis first and
owns capture staging/completion independently of Unity. The conservative
20 ms CPU-copy-call limit applies to both paths; Android additionally has a
200 ms request-to-ready limit. A single exceeded limit cools live copies for one
second, then permits at most one recovery copy per second. Three consecutive
healthy live copies restore the original cadence; another exceeded limit starts
the cooldown again. Historical budget flags and counts remain cumulative.
Exact requests do not count as recovery probes. Optional metadata/allocation or
driver failures still disable live preview until the source is replaced.
Cooldown only gates future optional copies; it never retires an unfinished
capture, clears a held reader or blocks the analysis owner. Stop interrupts the
existing wait and joins the preview owner without starting a recovery worker.
They are not a measured GPU-copy profile or a throughput claim.
These candidate limits still require actual device qualification.
The unqualified candidate also limits a capture to8,294,400 RGBA bytes. The last
admitted visible crop is classified before requesting capture, and actual ready
metadata is checked again before mailbox growth. Buffer replacement is
transactional: failure after partial allocation leaves the complete old pool
unchanged. Optional allocation/growth failure disables preview and records a
diagnostic; it neither fails source initialization nor escapes its owner thread.
Allocation, zeroing and disposal of replaced storage run outside the admission
mutex. A short commit rechecks capacity/source epochs and all active owners.
An unchanged-capacity call is a no-op even with readers or an in-flight writer;
it neither retires that owner nor prevents the other free slots from being used.

`RequestSnapshot` permits one pending next-image request. Android captures are
confirmed only against the same source/generation/frame/configuration epoch in
the accepted canonical journal, retrying overwritten admissions within5seconds.
Request ID and deadline are one locked record; deadline expiry is checked before
confirmation as well as before candidate access. Confirmed captures can be
discarded with `CancelSnapshot` without fetching HD pixels. Cancellation permits
a subsequent logical request but never retires an in-flight Input/GPU operation.
Failure/timeout never substitutes the current image. Successful CopySnapshot
consumes the request; a null/short destination returns BUFFER_TOO_SMALL plus
metadata. Windows reports Unsupported because the current Input API cannot
establish original-color geometry. A source generation change invalidates both
preview and exact identity at source admission, even when live preview is off
or suspended. Late old-generation completions cannot restore it. Native pending
hardware capture remains Input-owned
through Stop/Close; mailbox cancellation never pretends that hardware completed.

StatsV3 separately reports coded source and delivered analysis dimensions. Zero
source geometry means unavailable and is not filled from requested settings or
from the known test fixture. Preview visible-crop size is returned with its copy.

CancelSnapshot is the appended21st control-table member. The first20 entries
retain their offsets; the original168-byte prefix remains queryable while a
176-byte caller additionally receives cancellation. No older public ABI table
or structure is changed.

Focused checks: RuntimeControl, NativeSourceBinding and runtime_control_c_abi.
The controlled blocked-initialization fixture checks resource ownership, not a
real vendor driver's permanent-hang behavior. The optional Input DLL integration
test verifies single ingestion ownership on a disconnected source; it does not
establish RTSP throughput. A real source/harness and physical device checks are
separate acceptance gates.
