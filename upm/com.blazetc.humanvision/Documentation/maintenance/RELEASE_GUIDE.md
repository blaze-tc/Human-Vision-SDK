# v0.4.0-preview.16 Android RTSP 高清预览

SDK `0.4.0-preview.16` 和 Input `0.1.0-preview.9` 使用同一 Git 标签。先安装 Input，再安装 SDK。Android CPU 与 RK3588 NPU 使用匹配的本地资源。Android RTSP 高清与低档分析分路可同批提交，也可按各自槽位独立推进。Runtime Prefab 默认使用相机像素作为同帧底图。

```powershell
pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Android -ApiLevel 26 -RunTests
py -3.13 tools/package/check_input_package.py --root upm/com.blazetc.humanvision.input
py -3.13 tools/package/test_package_release_snapshot.py
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/test/verify_upm_git_newlines.py
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview16-authority.json --verify-only
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview16-authority.json --output out/releases/v0.4.0-preview.16-final
```

从最终提交生成八个公共附件。核对远端带注释标签指向最终提交，从草稿下载八个附件并逐个核验 SHA-256，再发布 prerelease。RKNN 资源和设备验收 APK 使用本地交付路径。OnePlus 的结果覆盖共享 Android 输入分路和 NCNN Vulkan 后端，RK3588 NPU 与业务实机验收仍待执行。见[公开验证报告](../reports/2026-11-18-preview16-rtsp-hd-preview.md)。

# v0.4.0-preview.15 RTSP 同帧画面与槽位跟踪

SDK `0.4.0-preview.15` 和 Input `0.1.0-preview.8` 使用同一 Git 标签。先安装 Input，再安装 SDK。Android RK3588 使用匹配的本地资源包。

```powershell
pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Android -ApiLevel 26 -RunTests
py -3.13 tools/package/check_input_package.py --root upm/com.blazetc.humanvision.input
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview15-authority.json --verify-only
py -3.13 tools/package/test_package_release_snapshot.py
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/test/verify_upm_git_newlines.py
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview15-authority.json --output out/releases/v0.4.0-preview.15-final
```

从最终提交生成八个公共附件。校验远端带注释的标签，下载草稿中的八个附件逐个核对 SHA-256，再发布 prerelease。本地 RKNN 资源和测试 APK 沿本地交付路径管理。见[同帧画面验收报告](../reports/2026-11-17-rtsp-preview-cadence.md)。

# v0.4.0-preview.14 RTSP 选流与独立识别输入

SDK `0.4.0-preview.14` 与 Input `0.1.0-preview.7` 使用同一 Git 标签。Android 与 Windows 的 Input 原生库为新版。Git UPM 或离线导入均先装 Input，再装 SDK。需运行 RK3588 NPU 的项目另装匹配新版 Input 哈希的本地资源。

```powershell
pwsh -NoProfile -File tools/package/build_input_native.ps1 -Platform Android -ApiLevel 26 -RunTests
py -3.13 tools/package/check_input_package.py --root upm/com.blazetc.humanvision.input
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview14-authority.json --verify-only
py -3.13 tools/package/test_package_release_snapshot.py
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/test/verify_upm_git_newlines.py
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview14-authority.json --output out/releases/v0.4.0-preview.14-final
```

从最终提交构建八个附件，下载草稿附件并核对 SHA-256 后发布。公共附件包含两个包、两个 Samples 和安装资料。私有 Android RKNN 模型、厂商库、本地补充包、测试 APK 与业务视频由原有本地路径管理。

# v0.4.0-preview.13 场地预览同帧修复

从包含 RK3588 三档接入的 SDK 主线发布 0.4.0-preview.13，Input 使用 0.1.0-preview.6。
`tools/package/release-preview13-authority.json` 固定两个 UPM 包的文件、GUID 和原生库校验范围。
由最终提交生成两个 `.tgz`、两个 `.unitypackage`、README、资产索引、源提交记录及 SHA-256 清单。
验证先装 Input 再装 SDK，以及两个 Git UPM 子目录按同一个发布标签安装。

公共 Release 发布 SDK 和 Input。Android CPU 独立 ONNX、RKNN 模型、`librknnrt.so`、
完整本地补充包、业务录像和本地测试 APK 留在原有本地交付范围内。
详细验证和设备边界见[本轮报告](../reports/2026-11-16-preview13-venue-overlay.md)。

```powershell
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview13-authority.json --verify-only
py -3.13 tools/package/test_package_release_snapshot.py
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/test/verify_upm_git_newlines.py
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview13-authority.json --output out/releases/v0.4.0-preview.13-final
```

核对远端主线后快进，使用 annotated tag 指向最终提交。在 draft prerelease 上传并下载核验八个附件的 SHA-256，再发布。

# v0.4.0-preview.8 发布

用户于 2026-10-14 明确要求核验后合并 main、发布 GitHub Release 并完善文档。
SDK 0.4.0-preview.8 / Input 0.1.0-preview.5；发布为预发布评估版。
`release-preview8-authority.json` 只批准八个标准公共附件，不批准完整本地资源补充包、
CPU YOLO ONNX、RKNN 模型、librknnrt.so、用户视频或测试 APK 的公共上传。
保留原评估/分发/硬件资格标记、旧 authority、旧 Release 和本地测试证据。

重新运行原生回归、Unity 编译/相关测试、打包工具、架构/公共 API/目录索引/换行检查。
缺少原始 golden 的结果单独记录；不得生成替代数据或将缺失/跳过计为通过。
同步仓库和随包安装/API/模型/测试说明，重新生成完整文件哈希 authority 后提交。

```powershell
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview8-authority.json --verify-only
py -3.13 tools/package/test_package_release_snapshot.py
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/test/verify_upm_git_newlines.py
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview8-authority.json --output out/releases/v0.4.0-preview.8-final
```

输出必须是新的不可覆盖目录，八个附件必须绑定最终干净提交。
origin/main 祖先检查后快进，不强推；annotated tag 固定该提交，创建 draft prerelease。
上传八附件后下载至独立目录，逐项验证 SHA256、文件/GUID/native/resource 闭包，再发布。
Windows 实际导入/初始化与 OnePlus 原 APK 的证据和 SDK 字节一致性分别记录；
仅修改文档/测试不代表重新获得设备性能数据。见[本次发布核验](../reports/2026-10-14-preview8-release.md)。


# v0.4.0-preview.7 发布

用户在知晓回归数据缺失和设备限制后明确要求合并 main 并发布。本次按预发布评估版交付；
不把既有硬件验收标记提升为 true，也不打包私有厂商库、RKNN 模型、用户视频或 APK。
当前两包版本为 SDK 0.4.0-preview.7 / Input 0.1.0-preview.5。
使用 release-preview7-authority.json，干净 Git 文件快照和新的不可覆盖输出目录运行
package_release_snapshot.py --authority；保留旧版本 authority，不重新标记旧证据。

必须检查两包文件/meta/GUID、原生依赖闭包、RuntimeData 哈希、真实 Input-first 离线导入、
Settings/preset/示例编译与 Windows CPU 初始化；对最终 Git 提交验证 UPM 安装。
main 只做祖先检查后的快进合并，禁止强推。创建 annotated tag 和 draft prerelease，
上传八个标准附件，下载到独立目录逐文件核对 SHA256 后再发布。
全量原生测试与 Unity 测试的实际通过/失败/缺失情况写入发布说明，不隐藏或改成通过。

# Unity managed API/settings preview.5 update

This additive update uses `tools/package/release-preview5-authority.json` and
`--authority` for snapshot validation/build. SDK 0.4.0-preview.6 depends on Input
0.1.0-preview.4; existing preview.4 native/model/SDK managed bytes remain pinned.
See `docs/reports/2026-10-08-unity-sdk-api-settings.md` for exact passing evidence
and the broad-regression limitations. Build a fresh immutable directory, verify
real Input-first/SDK offline imports and remote Git packages, push main without
force after ancestry checks, annotate tag v0.4.0-preview.6, upload/download and
hash-check all eight draft assets, then publish as a prerelease.

# Release gates

## 0.4.0-preview.4 admitted snapshot

The preview.4 release consumes the byte-qualified two-package snapshot in `upm/`.
Do not reconstruct it from the unrelated development worktree or an older live
build directory. `tools/package/release-preview4-authority.json` binds the complete
SDK/Input file/meta closure, reviewed RTSP reopen fix and accepted half-size defaults.
The native/model bytes remain those of the qualified Q4 payload.

```powershell
py -3.13 tools/package/package_release_snapshot.py --verify-only
py -3.13 tools/package/test_package_release_snapshot.py
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/test/verify_upm_git_newlines.py
py -3.13 tools/package/package_release_snapshot.py --output out/releases/v0.4.0-preview.4-final
```

The output directory must be new. The eight public files are two `.tgz` archives,
`HumanVisionInput-0.1.0-preview.2.unitypackage`,
`HumanVisionSDK-0.4.0-preview.4.unitypackage`, README, asset index, source snapshot
and SHA256SUMS. Import Input first, then SDK. The two unitypackage GUID partitions
must be disjoint and their union must contain every reviewed asset byte exactly.
The historical combined unitypackage/ZIP remain local evidence, not public assets.
Both installation forms contain the same reviewed source/native/models.
Offline directory groups must match
Unity ExportPackage: folder `asset.meta` plus `pathname`, without a file `asset`.
Test real Unity imports; a serializer checking its own archives is insufficient.
`tools/test/ReleaseSnapshotImportCheck.cs` verifies installed runtime hashes, generates
the three demos and initializes the actual Windows CPU native/model route in a
separate project. Validate local/Git UPM and actual sequential imports of the two
downloaded unitypackages. Input-only compilation must succeed before adding SDK.

The existing package_live_sdk/package_upm/verify_package_isolation scripts below
describe the older preview.3 reconstruction. They are not authoritative for this
two-package preview.4 snapshot. Package versions, provenance and SHA256SUMS must
match the final commit; rebuild artifacts after that commit.

Merge/push only after checking origin/main ancestry. Create an annotated
`v0.4.0-preview.4` tag on the verified commit, a draft Release, upload all artifacts,
download into a separate directory and compare every SHA256 before publishing.
Record the actual user acceptance separately from the unmet 30 fresh complete
observation FPS goal and retained model evaluation/distribution markers.

## Historical preview.3 tooling

Use PowerShell7 (`pwsh`) on this machine. Native scripts set up MSVC v143 and UTF-8
Ninja include dependencies. Android uses NDK23 (API24, ARM64); NDK21 lacks
the filesystem implementation required by ModelPack/Profile paths. On this
machine use `.venv-reference/Scripts/python.exe` for Python commands below.
Preserve dependency caches and user archives.

This workstation marks Unity 2021.3 `Unity.exe` as `RUNASADMIN` in the current-user
compatibility registry. For hidden batch validation set
`$env:__COMPAT_LAYER='RunAsInvoker'` in that command's process before invoking the
Unity test/import scripts. This avoids a hidden UAC prompt without changing the
registry or the user's open Editor process.

```powershell
pwsh -File tools/test/run_native_tests.ps1
pwsh -File tools/package/build_live_native.ps1
pwsh -File tools/package/compile_managed.ps1
pwsh -File tools/test/run_unity040_tests.ps1
python tools/maintenance/generate_component_catalog.py
python tools/package/package_live_sdk.py
python tools/package/package_upm.py
python tools/maintenance/generate_component_catalog.py --check
python tools/maintenance/check_architecture_boundaries.py
python tools/package/verify_package_isolation.py
python tools/test/verify_upm_git_newlines.py
pwsh -File tools/test/run_upm040_import.ps1
```

Version locations: CMakeLists.txt numeric version, package_live_sdk.py VERSION,
package_upm.py imported VERSION, component metadata, installation/release docs.
Default artifacts in out/releases/0.4.0-preview.3:
HumanVisionSDK-0.4.0-preview.3.unitypackage, HumanVisionSDK-0.4.0-preview.3.zip,
com.blazetc.humanvision-0.4.0-preview.3.tgz, asset-sha256.json, README.md.
Unity tar GUID directories and gzip inner name archtemp.tar are compatibility
requirements. UPM data GUIDs must differ from StreamingAssets copies.

Record exact results in DEVELOPMENT_STATUS. Stage tracked changes plus explicitly
declared new SDK files (never user media/archives). Commit verified artifacts,
push main after checking remote ancestry, create annotated tag v0.4.0-preview.3,
create a draft GitHub Release in blaze-tc/Human-Vision-SDK, upload artifacts, download
to a separate verification directory and compare SHA-256. Verify remote main/tag
commits before publishing the draft. Do not publish the superseded preview.5 draft.

QNN optional code is not an enabled/link/device pass without authorized dependencies.
Windows/Android physical camera/RTSP, 1/2/4/6/8-person fresh complete FPS, accuracy,
latency and thermal acceptance belong to the user and must be listed as pending.
