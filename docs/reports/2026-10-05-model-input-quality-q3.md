# Q3 implementation evidence (2026-10-05)

Base commit:18f73aece0d5d99d455c874fbe9e196693b02fee. Implementation and R1 recovery are ready for final independent closure. Root-approved final verification is qualified independent-process coverage: existing142/142 plus new36/36, exact union178 with0duplicates/missing/skips on final24ownedbytes. The retained combined run is177/178PASS with1baseline-diagnostic timing failure; it is not reported as a combined full-suite pass. No staging, commit, push, merge, deployment, user Unity mutation, server/network configuration, or Release performed by this implementer.

## Owned scope and mirrors

24 paths in owned-files.txt; hashes in owned-sha256.json. Nine canonical/UPM C# pairs are byte exact. The six existing metadata pairs keep their original GUIDs; only three NEW script metadata pairs are added. UPM DemoModeSettings.cs was originally an untracked protected baseline, supplied SHA256b552d0667073052a88a414c3ee6f26828bfea8240a0442c37ce86f2950410203; final file is canonical baseline plus the single RtspComputerHost field. Existing untracked HumanVisionSettingsStore.cs, SharedSettingsPanel prefab/meta, and all unrelated dirty input/native/renderer/packaging/runtime settings remain outside this ownership.

## Behavior

Shared InputQuality persists Medium=0/High=1/Low=2; missing JSON retains Medium, invalid values fail settings validation. The same validated quality catalog resolves exact same-mode Profile/ModelPack geometry before consumer detach or manager Shutdown. Unsupported PC/ORT contracts remain fixed with no selector options and retain the saved Android choice. No capture/model geometry mixing, main-thread inference wait, per-frame catalog/JSON, CPU/GPU fallback, or stable ABI change.

Selection is a draft until explicit Apply. Save persists a draft without changing active Contract/ActiveShared; renderer is bound to the stable active settings object. Successful reinitialization uses the original source-copy retirement path, a5s wait bound, new session/result baseline and the still independently playing source. Apply/Retry/scene switches are guarded during initialization and operation tokens retire destroyed/closing workflows. Preflight failure retains active session, result sequence, consumer attachment and saved draft. Native initialization failure after preflight leaves recognition unavailable with independent preview; no rollback/fallback claim.

The shared panel height derives from UGUI layout instead of fixed570; Demo-only columns control child widths. Landscape1280x720 and portrait720x1280 behavioral tests verify actual buttons have dimensions and the Retry button is inside the wrapper and reachable by the real ScrollRect.

RTSP alone adds Computer camera/video plus computer LAN IPv4 override. Explicit buttons set exact Happytime paths/videodevice and/video-1.mp4 on554 through ordinary OpenSource/Input.Open; backend/quality/Regions are unchanged. Startup preserves manual full URL, including legacy empty default. Private LAN IPv4 validation rejects empty/loopback/APIPA/MetaTUN198.18/public/path/port hosts; arbitrary endpoints remain available in the manual fullURL field. Editor detects only locally configured up physical WLAN/Ethernet with a private default gateway, excludes virtual/VPN/TUN adapters, prefers physical WLAN then stable interface-ID/address ordering; no LAN scan. IProcessSceneWithReport bakes serialized PC host into transient official three Demo build scenes. Player resolves that scene component or explicit override, never its own NIC. Missing host fails action with feedback; no localhost fallback.

## RED/GREEN and commands

Scratch:out/input/quality-q3-project. Independent frozen Q2 SDK plus frozen Inputv13; only owned source overlays. Original Q2/final-exact142 evidence remains immutable. Unity:D:/Developer/2021.3.45f1/Editor/Unity.exe, graphics enabled, no-nographics and no-quit.

- red.xml22/22 fail: feature absence plus two fixture mistakes. Corrected fixture errors before further work.
- red2.xml22total/20fail/2 existing behavior pass: persisted quality/preflight/helper/UI absent. red3.xml29total/27fail/2pass adds missing host-bake/interface tests.
- red4.xml31total/29fail/2pass includes meaningful actual Apply failure: current manager session Disposals=1 before a missing contract is checked. Tests use the actual UGUI Apply event and real manager/consumer lifecycle boundary; the diagnostic session/source doubles supply only controlled external state, no fake recognition result.
- First green31total29pass2 fixture failures (Unity Rect JSON backing field representation and EditMode scene creation). Corrected fixtures, not assertions. green2:30/31 with remaining Editor untitled additive-scene limitation, corrected by manipulating/restoring only isolated test scene name and owned host object.
- layout-red.xml2/2 expected width failures; layout-red2.xml2/2 expected fixed570 wrapper clipping after child-width repair. Original committed Canvas was overlaid only in new scratch to confirm the actual original layout fails. Restored current Canvas after RED.
- green3.log compilation error in new RecordingSource fixture (SourceCopyLease is value type); corrected to default.
- green4.xml34/34 PASS,0fail,0skip. Migration, invalid enum, shared save/load, three real catalog shapes, PC fixed mode, missing/tampered pack, actual quality/preset button events and Input.Open requests, preflight-before-retirement, active-vs-draft Save, busy guards, baked host/interface checks and full portrait/landscape scroll behavior.
- First full final-complete.xml176total/141pass/5fail/30skip. Four existing DemoObservationRate tests caught a new Shared-null HUD dereference before Navigator.Start; fixed owned Canvas with null-safe status, existing tests unchanged.30 skipped model-pack tests were due omitted Q2 top-level ApprovedRuntime/ApprovedTopDownRuntime directories in initial scratch copy; copied exact Q2 directories, no skip weakening. One delayed Native Collection leak error attributed to ActualApply after broader-suite allocation; focused34 clean. Retained all results/logs; final-complete2.xml176/176PASS,0fail,0skip after these repairs. Its six delayed Native Collection diagnostics also exist byte-identically in immutable Q2/final-exact.log637-642 (Q3lines626-631), with identical NativeArray11060940 bytes at shutdown. Root and independent reviewer verified baseline equality. No leak-free/no-diagnostics claim and no unrelated fixes or suppression.
- Android conditional frozen Runtime compile exit0 and Demo compile exit0 (demo-android-compile2.log), using Unity-bundled Roslyn/mono. Initial manual Demo rsp omitted UnityEngine.UI reference; fixed rsp only. Existing CS0649 AnalysisContract JSON DTO warnings and original Runtime initializeOnStart warning retained. No affected Android APK/device claim.
- py -3.13 tools/maintenance/check_architecture_boundaries.py:Public surface PASS; Architecture/documentation boundaries PASS.
- py -3.13 tools/maintenance/generate_component_catalog.py --check:PASS.
- Owned tracked diff whitespace check:PASS; Git notes existing line-ending normalization only.

Launch pattern:

```powershell
$q3 = (Resolve-Path 'out/input/quality-q3-project').Path
Start-Process -FilePath 'D:/Developer/2021.3.45f1/Editor/Unity.exe' -ArgumentList "-batchmode -projectPath `"$q3`" -runTests -testPlatform EditMode -testResults `"$q3/final-complete2.xml`" -logFile `"$q3/final-complete2.log`"" -WindowStyle Hidden -PassThru
& D:/Developer/2021.3.45f1/Editor/Data/MonoBleedingEdge/bin/mono.exe D:/Developer/2021.3.45f1/Editor/Data/MonoBleedingEdge/lib/mono/msbuild/Current/bin/Roslyn/csc.exe '@out/input/quality-q3-project/runtime-android.rsp'
& D:/Developer/2021.3.45f1/Editor/Data/MonoBleedingEdge/bin/mono.exe D:/Developer/2021.3.45f1/Editor/Data/MonoBleedingEdge/lib/mono/msbuild/Current/bin/Roslyn/csc.exe '@out/input/quality-q3-project/demo-android.rsp'
```

## Independent review R1 recovery

Root-approved ruling keeps missing NCNN quality family with saved High/Low as explicit failure, without automatic Medium fallback. Add a reachable Reset quality draft to Medium (default) button; it changes only pending UI draft, no automatic Apply/Save/geometry/backend/source switch. Availability refresh precedes requested resolution, so the error never leaves unreachable instructions. PC/ORT saved Android choice stays unchanged unless explicitly reset.

Actual reset-red.xml2/2 fails High/Low because recovery action absent. Added focused real button test with a real legacy192x256 manifest: explicit reset resolves Medium base contract without fake640 label or disposing current session/altering capture/mirror; saved choice remains until Save/Apply. reset-green.xml36/36PASS,0fail,0skip on final source (2026-10-05 09:24:55Z–09:25:28Z). Latest owned source change precedes that result, and all frozen shipping files match final manifest. Final Android Demo conditional compile demo-android-final.log exit0, existing DTO CS0649 warnings only.

## Final qualified closure

Root explicitly accepted bounded split closure on the same final source bytes. No LogAssert suppression, renderer/Input source changes, or repeated combined-suite retries were made.

- split-existing.xml:142/142PASS,0fail,0skip, fresh Unity PID2288;2026-10-05 09:32:35Z–09:35:22Z (166.584s). The filter contains the exact12 original fixture classes from immutable Q2/final-exact.xml.
- reset-green.xml:36/36PASS,0fail,0skip, independent fresh Unity process;2026-10-05 09:24:55Z–09:25:28Z (32.512s). It covers the final SharedQualityUiTests including R1; root authorized reuse after final byte binding.
- split-coverage.json verifies actual testcase names against immutable Q2 existing142 and final new36, exact union178,0duplicates,0missing,0skips. Source hashes24/24 and frozen shipping hashes12/12 equal owned-sha256.json; all owned mtimes precede reset-green.xml.
- Retained final-exact.xml combined run:178total/177pass/1fail/0skip. Sole failure ActualApplyRejectsMissingContractBeforeSessionRetirementAndDraftMutation receives known Native Collection diagnostics through broader-suite GC timing. Six identical diagnostics are present in Q2/final-exact.log637-642, Q3/final-complete2.log626-631, and latest final-exact.log612-617. This evidence supports qualified split coverage, not a clean combined-run or leak-free claim. split-existing.log also retains the baseline shutdown NativeArray11060940 bytes.
- Final Android conditional Demo compile demo-android-final.log exit0; architecture/documentation/public-surface and component-catalog checks PASS. No Android APK/device acceptance performed here.

Exact partition launches (graphics enabled):

```powershell
$q3 = (Resolve-Path 'out/input/quality-q3-project').Path
$existingFilter = (Get-Content "$q3/split-existing-filter.txt" -Raw).Trim()
Start-Process -FilePath 'D:/Developer/2021.3.45f1/Editor/Unity.exe' -ArgumentList "-batchmode -projectPath `"$q3`" -runTests -testPlatform EditMode -testFilter `"$existingFilter`" -testResults `"$q3/split-existing.xml`" -logFile `"$q3/split-existing.log`"" -WindowStyle Hidden -PassThru
# Earlier independent final-source new36 process, reused under root ruling:
Start-Process -FilePath 'D:/Developer/2021.3.45f1/Editor/Unity.exe' -ArgumentList "-batchmode -projectPath `"$q3`" -runTests -testPlatform EditMode -testFilter HumanVision.Tests.SharedQualityUiTests -testResults `"$q3/reset-green.xml`" -logFile `"$q3/reset-green.log`"" -WindowStyle Hidden -PassThru
```

## Limits

Q4 owns qualified payload/package/import/APK closure, actual Happytime computer camera/video retrieval and Android three-quality/Region/source switching. Root freshly DESCRIBE-verified both real paths200/H264; unit URLs use controlled private fixture addresses and do not claim physical transport/inference success. No FPS/accuracy/monotonic speed, continuous motion, hand output,30fresh FPS, device acceptance or Task11 completion claim. Current user Unity E:/UnityProject/Human-Vision-SDK-Test and physical device were not modified.
