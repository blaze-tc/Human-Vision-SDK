# PC demo Git delivery — 2026-10-01

Delivery package version0.4.0-pc.1, immutable tested source:
`8c0bc5d612beb7ccebfbb476c55ebd05ff0499c5`.

```text
https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision.pc-demo#8c0bc5d612beb7ccebfbb476c55ebd05ff0499c5
```

Only dedicated `codex/unity-pc-demo` branch was pushed, main/Release unchanged.
Task1–3 code commits44ebb09/e5a09e8/8c0bc5d are separate and reviewed. This later
record is documentation only; the URL intentionally pins the exact remote-tested
package commit. Installation instructions: [PC guide](../PC_DEMO_INSTALLATION.md).

Fresh remote project `out/pc-demo/remote-import-8c0bc5d-20261001` did not reuse the
local project's Library/PackageCache or StreamingAssets. Unity2021.3.45f1 process
63708 started2026-10-01T09:24:33.4281413Z; successful check recorded
2026-10-01T09:25:11.5298334Z. Remote-only dependencies manifest pins the Git URL above.
`Packages/packages-lock.json` source=git and hash equals the full commit.
Fresh script compilation, PC scene creation, seven runtime index hash/GUID checks,
and actual explicit CPU native initialization all PASS (`pc-remote-import-pass.json`).
Default configured Git Schannel succeeded; no SSL verification weakening or global
Git settings change. Unity RunAsInvoker was process-scoped only.

Root then parsed remote PackageCache manifest and compared all151 indexed asset/
metadata hashes to the reviewed local package; every file byte matches, including
native libraries and model. `out/pc-demo/remote-payload-pass.json` records equality.
Remote manifestSHA256:
83b7674524b03fc71ec8fc50bf60e4d9bb8f4f754f9c0017964e2175df8d8e28.
`git ls-remote origin refs/heads/codex/unity-pc-demo` confirmed the delivery commit
at publication. No PR or release was created.

Real video/DirectML/visible skeleton and Win64 build were tested in the clean local
project, documented in [Task3](2026-10-01-pc-demo-task3.md). The final remote test
checks Git import, hashes/GUIDs, scene generation and CPU initialization; it does
not remeasure video FPS or execute a standalone player. The byte-identical payload
links those gates without claiming a second remote performance run.

Bounded result: seven-person video observed20.7474 positive unique result frames/s,
ageP50/P95104.13/176.44ms on25FPS input;414/417 observations containseven bodies,
three containeight. Different Windows RTMO model prevents hardware-only comparison
with Android YOLO.30FPS/full-joint anatomical accuracy, standalone execution and
real camera/RTSP device effects remain unaccepted, not falsely marked complete.
User's original test project and caches were retained.
