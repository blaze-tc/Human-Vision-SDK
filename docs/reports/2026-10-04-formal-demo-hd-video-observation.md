# Formal Android Demo: original HD seven-person video

This is a second physical Video-mode observation using the same installed
formal observer APK, unchanged approved YOLO640 FP32/native runtime and ordinary
enabled MeshRenderer/LineRenderer objects. It is a measured baseline, not an
optimization, identity/motion acceptance or30FPS pass.

Source: user's original `E:/Project/Human Vision SDK/video-2.mp4`,13,585,121
bytes, SHA256 `6abd4a523e9e0dc9961a3f037e0c33600271ff3a53d170e5f1dbd8c562480f53`.
FFprobe confirms H2641920x1080,30000/1001FPS,19.386033s. The official Video
source loops it normally. The phone's copied file hash matches the host.
The public requested capture size remains1280x720; actual1920x1080 is recorded
separately. Public Video mode uses line3/point9 reference canvas units,
MaxBodies8/no regions. After collection the original Video1 public settings
were restored and the separate test app was force-stopped.

APK `6e49568884eec09d659b871b8d234856b53e445bc2aa5a5c76c5c80337b207fe`,
SDK native `fef5fda3952d21e68b40052cc8804adcd99bc9d961429daf868afd3650bbbd96`,
Input native `d3968a4f12d1cf7882c04312c7f0ad095c4a18b894846e2beafb758e9f582628`,
runtime index `89703ad345f2b7a954b6711f8e65ffbd02e5f80cbf1c653d6815a53395ee3cfc`.
These identities are unchanged from the first measured formal Demo run.

Evidence root: `out/input/formal-android-oct04-v13-observer/video2/`.
Actual60.001886s run exported65,996,040 bytes. Footer records919 observations,
1,789 sources and1,803 samples; overflow/interrupted/copy-invalid are false.
Analysis uses the same half-open10-50s40s window and whole raw observations:

| Metric | Actual result |
| --- | --- |
| Complete manager observations |636 |
| Complete manager result delivery FPS |15.9 |
| Observed source publication FPS |29.8 |
| Local publication-to-receipt age P50/P95 |100.6115/134.0155ms |
| Body-count histogram |0:1,6:52,7:583 |
| Pose / Tracking / Total reported stage median |60.0349/2.3752/62.6337ms |
| Public runtime drops / bridge drops / CPU readbacks |0/0/0 |
| Maximum adjacent clock bracket uncertainty |7us |
| Manager measurement / native coverage |VALID / INCONCLUSIVE |
|30 fresh complete observations/s target |FAIL |

The screenshot captured after export shows seven upright, visible object
skeletons and actual1920x1080 preview. It does not establish correct arm motion,
continuous identity, all-person coverage or true hand landmarks.52 six-body
and one zero-body result are retained and included in FPS; they are not repaired
or omitted. The observed source rate exceeds result delivery, while reported
Pose time remains about60ms. This identifies the dominant reported stage, not
an independently profiled GPU layer or a causal resolution comparison.

Battery temperature snapshots38.5C before/41.8C after do not establish sustained
thermal performance. Recorder preallocation/overhead remains unqualified. The
source is29.970Hz, not30Hz, and cannot certify the strict30FPS source requirement.
Native fresh/GPU/decoder counters remain unavailable/null. The public native
inference_fps field was0 again and is excluded from result-rate measurement.

An external root control helper initially mixed UTC and Local DateTime when
printing wall elapsed time. It was corrected to DateTimeOffset in collection.json;
it never entered recorder timestamps or the analyzer. The actual60.001886s
footer and monotonic source/result joins independently qualify the run.

The independent reviewer recomputed raw counts, percentiles, stage medians,
identity hashes, exported footer and time-control separation in
`video2/independent-review.md`: evidence review PASS; performance target FAIL.
Task11 Camera/RTSP, observer overhead and final physical acceptance remain open.
No model/backend/renderer performance change, main merge or Release.
