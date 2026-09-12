# Stable Unity runtime

Purpose: semantic gameplay API and reusable main-thread snapshots. Consumes runtime
profile/root and image frames; produces raw and sampled HumanVisionBody arrays.
Allowed: Unity primitives and private C interop. Forbidden: concrete model/provider
selection in gameplay. V1 config fields remain exact deprecated exceptions.
Primary files: HumanVisionManager, HumanVisionRuntimeSession, RuntimeBindings,
HumanVisionRuntimeData. New algorithms require no edits here.
Focused checks: compile_managed.ps1, check_public_surface.py, managed ABI/session tests.
Symptoms: marshaling/layout, extraction, stale snapshots. Packaging generates UPM
from this authoritative source; do not edit only the UPM copy.
