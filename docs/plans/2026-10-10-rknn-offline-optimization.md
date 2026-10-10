# RK3588 NPU optimization before physical acceptance

User authorizes completing optimizations that can be checked now. This is the
sole active follow-up; OnePlus batching is complete. Preserve model accuracy,
source-frame identity, public ABI, latest-frame semantics and shipping boundaries.

1. Extend the pinned conversion tool with graph-validated hybrid quantization.
   Test calibration/graph/node/receipt failures first. Run actual Toolkit2 2.3.2
   conversion and unchanged simulator gates for bounded pose-sensitive variants.
   Record failures; promote no candidate from successful conversion alone.
2. Optimize exact-size and 2:1 uint8 RGB preprocessing, retaining the general
   OpenCV-compatible interpolation. Verify all four formats, padded rows,
   independent pixel oracles, no allocations and before/after host timings.
3. Build native, run focused/full regressions and architecture guards. Update the
   private Android test project and verify its packaged native plus OnePlus
   Vulkan regression; OnePlus cannot certify RKNN execution.
4. Record device follow-up commands and stage budgets. Existing core_mask7,
   preallocated outputs and version diagnostics are retained. DMA/RGA binding,
   RK3588 graph coverage and sustainable performance need real driver evidence;
   do not introduce untested shared-memory synchronization or claim hardware FPS.
5. Update maintenance/status/performance documentation and commit/push verified
   scope. Preserve old candidates, caches and private vendor/model files.

No quantization threshold relaxation, blanket replacement of model contracts,
multi-context frame reordering, private public Release or fabricated NPU result.

## 2026-10-10 field-log steering: complete repair in one delivery

The user provided actual RK3588 NPU logs and requests a complete fix without
repeated device trials. Extend this active follow-up with regression-first
SettingsDemo pixel/preview geometry repair across 640/720p/1080p/4K, bounded
readback/feed improvements and detailed input timing. Preserve model precision
and source leases. Any GPU resize optimization must compare actual pixels
against the frozen uint8 CPU interpolation oracle before activation. Verify
Unity tests, packaged shader/native, OnePlus actual camera/video CPU/Vulkan
regressions and resolution changes. Deliver one sealed APK plus a Chinese
field analysis; distinguish actual provided RK3588 evidence from unmeasured
post-change RK3588 throughput. Never bypass unsupported NPU on OnePlus.

## Completion record

Verifiable software scope is complete; see the field report and DEVELOPMENT_STATUS
for actual commands/results, including unsuitable full-suite fixtures and their
index-mutation recovery. No unqualified hybrid model or vendor asset is promoted.
The delivered private APK is checked in OnePlus CPU/Vulkan, with exact 4K-source
resolution mapping and untouched model bytes. RK3588 post-change performance
remains a physical measurement gate; the user is not asked to iterate packages.
