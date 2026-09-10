# Cleanup audit result — 2026-09-10

Read-only audit identified 299,594,834 bytes (about 286 MiB) in the following exact
regenerable directories. Checked paths remain inside the project, contain no tracked
files/reparse points, and were not referenced by active Unity/CMake/compiler processes.

- out/package-inspection
- out/missing-gpu-dependency
- out/unity-native-before-gpu
- out/live-managed-build
- build/windows-dml

Deletion was NOT performed: automatic approval review rejected both the checked batch
operation and the narrower single-directory Remove-Item operation with `blocked by policy`.
No more specific rejection reason was returned. These files remain intact; do not record
space as reclaimed. Active build/dependency/model environments and all user documents,
archives/media were preserved. Continue development without destructive workarounds.
