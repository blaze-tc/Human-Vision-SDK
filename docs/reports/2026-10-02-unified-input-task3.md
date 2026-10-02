# Unified-input Task3: independent Windows RTSP runtime

2026-10-02. Base b6c1d3244f185c425ec0a7136617fcf8d973180f. Task3 implementation
and one review-fix round pass fresh SpecPASS/QualityPASS; no open findings remain.
Root architecture checks also pass. Existing R4 work and caches remain.

## Delivered boundary

`native/input` builds a standalone Windows x64 FFmpeg plugin without HumanVision,
Runtime Host, ORT, ncnn or model dependencies. Eight versioned C functions provide
Open, nonblocking Close, Release, State, LastError, PollFrame, exact-sequence CopyRgba
and an independent native clock query. Close cancels; Release returns BUSY until
actual worker termination and active copies retire. Caller serializes successful
Release against all other API calls. This Windows route decodes/scales to CPU RGBA
for later Unity upload; it is not the Android GPU route.

Only complete decoded frames overwrite the latest frame. Compressed P/B packets
are processed in order; decoder EAGAIN drains and retries the same packet. Actual
dimensions, sequence, generation, native packet-read/decode time, stream PTS and
clock identity remain separate. CopyRgba rejects overwritten sequences and copies
matching metadata and pixel bytes atomically. Color contract changes, including
primaries, advance generation. Only explicit sRGB transfer plus BT709 primaries
claim sRGB; unsupported/unspecified color stays Unknown. Native rows are top-left.

Public errors omit URLs. A nonprinting FFmpeg callback prevents credential-bearing
library output. This callback is process-global in the shared FFmpeg DLL; another
host logger must not replace it with an unsafe callback. Legacy HV_Rtsp sources,
signatures and exports remain untouched; the independent build hook defaults OFF.

## Actual checks

```powershell
pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Windows -RunTests
& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe' --test-dir out/input-native/windows -C Release -R input --output-on-failure
py -3.13 tools/maintenance/check_architecture_boundaries.py
```

Qualified MSVC14.44/v143 + Ninja Multi-Config. Final build PID23444 exit0, 8/8 tests;
explicit CTest exit0, 8/8. Source-only RED preceded implementation; behavioral RED
also demonstrated generation/Closing errors, then review RED demonstrated both
false-sRGB and primaries-only generation defects before fixing them. Tests cover
real stalled TCP cancellation/reconnect attempts, credentials, independent clocks,
initialized nonuniform exact pixel/metadata copy, primaries changes, actual active
copy retirement and four legacy ABI signatures/null argument behaviors.

Locked FFmpeg7.1 source archive and JavaCPP7.1-1.5.11 Windows JAR hashes match;
all cached FFmpeg DLLs and 937 source headers were verified. Binary import audit
contains no inference/SDK/Host library; eight new and four legacy exports checked.
Final DLL SHA256:
`9A3CB1446206F75D4B7102015164D44B5E81D24FCB9EFEBCAC1F56AA0AE5C66F`.
Source/artifact/receipt hashes: `out/input-native/receipts/task3-round1-hashes.json`.
Final raw build/CTest: `round1-green.log`, `round1-ctest.log` in that receipt folder.
Review evidence, reports and frozen diffs are in the plan-specific SDD ledger.

## Build incident and limits

Ninja initially parsed a mojibake localized include prefix and recorded zero
headers; changed Session layout rebuilt implementation objects but left the policy
test object stale. A bounded isolated stale-layout reconstruction crashed; replacing
only its test header/recompiling passed against unchanged implementation objects.
The original overwritten fault binary/dump is unavailable; its exact original stack
is not claimed. Corrected qualified2052 compiler prefix records header dependencies,
and a timestamp-only private-header change rebuilt all seven affected objects.
The script exposes MsvcIncludePrefix for a different compiler UI installation.

Loopback tests intentionally do not implement successful RTSP playback. Real H.264
TCP protocol success, decoded frame sequence after reconnection and Unity preview
belong to Task4. Policy frames are explicitly test-only. Full old SDK successful
stream regression, Android hardware decode/GPU synchronization and physical skeleton
acceptance are not established by this task. Current raw camera skeleton remains
approximately11FPS; the >=25 interim/30 fresh complete observation target is unmet.
No main merge or Release.

Root finalization removed only an extra blank line at build-script EOF to pass
Git whitespace checks; executable tokens are unchanged. The final script hash is
`0a3555c643f36b6b33ea5480ec6ae27f5a698d9055c5b658f258e4132ae32fb5`. Reviewed round1 manifests retain their original snapshot.
