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
pins the final 452-file/249-GUID source closure; its LF-stable SHA256 is
`56d1a8393f3dbe8da023446f1feda03088be70907bca2d20599ff70dcb019fcd`.
The earlier 450-file closure is retained in historical candidate receipts.

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
- Original 450-file candidates 5 and 6 contain eight byte-identical output files.
  Their combined package has 247 mapped assets; current candidate8 has 248.
  The historical package ZIP contains the
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

### Actual remote Git byte-gate correction

Main was fast-forwarded to release commit `ba8a92f`; the initial annotated
preview tag points to that commit. The Release remains an unpublished draft.
Actual remote Git installation resolves both packages to that commit and passes
20 installed hashes, three generated demos and Windows CPU initialization.
Strict cache verification nevertheless fails: SDK325 files are exact; 54 of
Input125 text files are converted LF to CRLF. Every differing pair is equal
after line-ending normalization; native/model bytes remain exact. The retained
`out/release-verification/remote-git-initial-byte-failure.json` records the FAIL
separately from runtime initialization PASS.

UPM's package subtree checkout does not retain root Git attributes. The minimal
fix adds Input `.gitattributes` (`* -text`) and its deterministic metadata, updates
only its asset index and the authority, and adds two regression tests. RED:
missing attribute and 54 byte changes with `core.autocrlf=true`; GREEN: 13/13,
all 452 package files preserved. Original runtime/native/model bytes are unchanged.
The root authority JSON is explicitly normalized to LF before commit, matching
its tracked attributes. Candidate8 package archives match candidate7 exactly;
only authority/provenance receipts changed. Evidence is under
`out/release-git-byte-fix-20261005/`.
Actual candidate7 Assets import and model initialization both exit 0 in
`out/release-unity-assets-fixed-7`; its archive equals current candidate8.

Correct the tag created during this unpublished draft only with an explicit
lease on its known old tag object, after the fix commit and nonforce main push.
Retain before/after references. Re-run actual remote Git cache verification in a
new isolated project before publishing; never treat normalized equality as
the strict byte gate passing. Final archives are rebuilt against the fix commit.

### Corrected remote source gate: PASS

The corrective release commit is `506f3bd54a577b2ff664010ced7a0071d88875ef`.
Main was fast-forwarded without force. While the Release remained an unpublished
draft, the locally created tag was corrected with a lease on the known old
object `10666954f072e4a3590e773df524a7f27fad9302`; its first corrected annotated object is
`8aca20f2946e0ebbb9775ce9348d994eddd530d4`, peeling to the corrective commit.
Both before/after references remain in the verification receipts.

The fresh `out/release-unity-remote-2` project resolves both tagged URLs as Git
dependencies at `506f3bd`, initializes successfully (exit 0), verifies all 20
installed runtime hashes and generates three demos. Every cache file is checked:
SDK325 + Input127 = 452 byte-exact files, no missing/extra files. Independent
review confirms PASS; `remote-git-import.json` and the refreshed review packet
bind the actual lock, cache and log evidence. The initial byte FAIL is preserved.

Earlier artifacts in `out/releases/v0.4.0-preview.4-final-2` have provenance bound
to `506f3bd` and the LF-stable authority. TGZs and `.unitypackage` match the
actual-imported candidate7/8 bytes. Upload and downloaded-artifact verification
are subsequent external gates; public publication is not yet asserted here.

### Final download shape: two complete Unity packages

Six earlier draft assets uploaded and downloaded with matching SHA256. The large
combined unitypackage/ZIP transfer later failed with an SSL write timeout. An
owned retry was stopped when switching to smaller complete import packages; no
partial transfer or initial six-file result is claimed as final publication.
The large combined files and failed transfer evidence remain local.

The final public output contains exactly eight files: SDK/Input TGZs,
`HumanVisionInput-0.1.0-preview.2.unitypackage`,
`HumanVisionSDK-0.4.0-preview.4.unitypackage`, README, asset index, source snapshot
and SHA256SUMS. There is no combined unitypackage or ZIP in the final public set.
Import Input first, then SDK. Input has 72 groups (56 files/16 folders), SDK176
(147 files/29 folders); GUID and path partitions are disjoint and their union is
the complete 248-group offline payload. Shared generated plugin folders occur
exactly once, in Input. Every actual asset/meta byte is retained; native, models,
C#, versions and GUIDs are unchanged. Only package instructions and their hash
indexes change, so historical TGZ hashes are not claimed for the new final set.

Split packaging RED: two expected failures (old combined output shape and absent
partition function). GREEN: 15/15 tests. Fresh candidates9/10: all eight files
byte-identical; exact extracted union, source/native/GUID closure and architecture
checks PASS. `out/release-verification/split-assets/receipt.json` binds the evidence.
Real sequential Input-only compilation/import, SDK import and native/model
initialization all exit 0 in `out/release-unity-assets-split`; all 20 installed
hashes, three demos and accepted defaults PASS. Next are postcommit provenance
rebuild, the refreshed remote
Git gate and final uploaded/downloaded asset verification before publication.
Independent split review: Spec PASS / Code quality PASS / actual local artifact
PASS, with no remaining blocking findings. Review and hash packet retain each
historical failed lane and qualify the subsequent external gates separately.

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
