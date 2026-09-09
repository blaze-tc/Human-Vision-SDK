# 0.3.0 preview delivery plan

User scope 2026-09-09 supersedes older milestone ordering and authorizes main/Release publication.
Existing rectangle dragging/resizing stays unchanged. No new runtime/unit/camera tests are run.

1. Fix Android live preview coupling and unnecessary readback/history work; bound ORT CPU threads and disable spinning. Preserve MP4 synchronized rendering. Expose source age and true inference throughput; do not claim measured phone performance.
2. Add same-snapshot six hand endpoints from existing wholebody small model. Preserve COCO-17 ABI; reject mismatched tensors. Palm is derived from hand-model root/MCPs, endpoints direct, invalid remains invalid.
3. Independent pooled joint/line renderer with thickness controls, video separate. Independent settings scene/controller with existing region editing logic.
4. Build Windows/Android native and managed assemblies. Produce importable unitypackage and Git UPM package with deterministic model preparation, metadata and license provenance.
5. Inspect package with Unity importer, review source, publish declared changes to main and versioned GitHub Release; verify remote ref and downloaded assets.

Progress: implementation, native/managed compilation and archive/hash checks complete. Fresh Unity import and phone performance remain unverified. Publication to main/Release is next.
