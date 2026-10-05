# Integration preview 4 release verification (2026-10-05)

## Authorization and scope

The user tested the visible demo, accepted the effect and explicitly requested
main merge, Git publication and Unity-importable/downloadable Release packages.
Release target: `v0.4.0-preview.4`; SDK `0.4.0-preview.4`, Input
`0.1.0-preview.2`. Original development checkout and caches remain untouched.
This is an integration preview. Task 11 and 30 fresh complete observation
frames/s remain OPEN; observations count complete frames, not accumulated bodies.
No new physical performance acceptance is inferred from package tests.

## Source and payload authority

The clean release branch starts at `b523f13`; its payload is reconstructed from
the immutable Q4 device-tested snapshot, not an older mutable UPM tree. Only
accepted half-thickness defaults, reviewed deferred RTSP reopen, and release
metadata/licenses are added. `tools/package/release-preview4-authority.json`
pins the complete 450-file/248-GUID source closure; its SHA256 is
`7c99e9db14e446aecbeeda21a78c4a3ff1926bb779c46528d991962918d80b8b`.

Twenty runtime-index entries, eight Android SOs and nine Windows DLLs were
validated, including strong native import dependency closure. Android SDK SO:
`71b15097f9fd56d1969a0a9a8eb5fb95aa3aeb88f6103b105006cbf06c5afa92`;
Android Input SO:
`d3968a4f12d1cf7882c04312c7f0ad095c4a18b894846e2beafb758e9f582628`.
The existing native binaries, model bytes, inference profiles and public APIs
are preserved. NCNN Vulkan never silently falls back to ORT. Current bundled
qualified profiles are Android NCNN Vulkan and Windows CPU/DirectML; other
runtime-mode interfaces are not certified by this release.

Low/medium/high Android model contracts are 512x288/640x384/960x576, independent
of capture resolution. Region assignment is inference-result postprocessing.
Current models do not infer genuine hand/finger points; canonical API fields
remain available. Historical `local_evaluation_only` and model distribution
license flags remain unchanged. Complete third-party license texts and notices
are included; commercial model authorization is not asserted. No user video,
photo or private RTSP credential is included in public artifacts.

## Fresh automated evidence

Commands run in `.worktrees/release-0.4.0-preview.4`:

- `out/release-verification/native-tests.cmd`: VS v143, Ninja Multi-Config,
  Release CTest **376/376 PASS**, 29.45 seconds. Shipping natives are not replaced
  by this test build. Missing real ignored golden/model fixtures caused earlier
  failures; those logs remain, and restoring actual fixtures produced the pass.
- Architecture Python regression **33 PASS / 24 conditional SKIP**, 0 failures;
  model-quality regression **4/4 PASS**. Architecture boundaries, component
  catalog and Git-newline (`autocrlf=true`) checks PASS. Logs are under
  `out/release-verification/`.
- Unity full test run **218/219 PASS**, with the real native plugin installed.
  `MissingInputPluginIsActionable` expects the plugin to be absent and fails in
  that installation. A separate native-absent installation passes that exact
  case **1/1**, with zero skips. The full failure is retained; this is not
  reported as a single 219/219 green run. `unity-test-coverage.json` records
  the complete passing union and hashes of both original logs/XML files.
- `py -3.13 tools/package/package_release_snapshot.py --verify-only`: complete
  authority, byte hashes, native dependencies and GUID closure PASS.
- `py -3.13 tools/package/test_package_release_snapshot.py`: **11/11 PASS**.
  Initial real `.unitypackage` import exposed invalid zero-byte folder asset
  entries. Unity's own exported format supplied the oracle; two expected RED
  tests preceded the fix. Folder entries now contain metadata/pathname only;
  regular files contain asset bytes. Failed candidates and import log remain.
- Final candidates 5 and 6 contain eight byte-identical output files. The
  combined package has 247 exact mapped assets. The package ZIP contains the
  two UPM TGZs, combined `.unitypackage`, README and receipts, not loose UPM trees.
- Clean local UPM installation: actual 20 installed runtime hashes verified,
  three official demos generated, defaults 4.5/13.5 checked, real Windows CPU
  native/model initialization PASS. Corrected final candidate 5 `.unitypackage`
  actually imports into a second clean Unity 2021.3.45f1 project, exit 0.
  That Assets installation also passes all 20 installed runtime hashes, three
  generated demos, defaults and real Windows CPU native/model initialization,
  exit 0; `out/release-unity-assets-fixed/release-import-pass.txt` is the receipt.

The isolated CLI projects are `out/release-unity-git` (local file UPM source)
and `out/release-unity-assets-fixed` (Assets import). The user's open Unity
project is not modified by these release verification lanes. GPU device/FPS
acceptance and RTSP reopen physical validation are not re-certified here.

## Publication sequence and receipts

Both isolated installations have passed offline model initialization. Independent
review verdict: Spec PASS / Code quality PASS / local release artifact PASS,
with no remaining blocking findings. The retained review and hash packet are
`out/release-verification/REVIEW.md` and `review-hash-packet.json`.
After the verified commit,
rebuild artifacts to record its
actual SHA in `source-snapshot.json`; verify TGZ/unitypackage byte identity against
the imported candidate. Push main without force and an annotated preview tag.
Then verify both packages using real remote Git URLs in a clean Unity project.
Create a prerelease draft, upload all eight files, download them into a separate
directory and compare SHA256, then publish. Final external publication receipts
belong in `out/release-verification/`; do not claim these future steps passed
before they execute.
