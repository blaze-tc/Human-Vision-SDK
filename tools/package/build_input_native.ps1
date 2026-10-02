param([ValidateSet('Windows')][string]$Platform='Windows',[switch]$RunTests,
      [string]$VisualStudio='D:/Microsoft Visual Studio',
      [string]$MsvcIncludePrefix='注意: 包含文件:  ')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$cmake="$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
$ninja="$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
$dev="$VisualStudio/Common7/Tools/VsDevCmd.bat"
$environment=& cmd.exe /d /c "call `"$dev`" -no_logo -arch=x64 -host_arch=x64 -vcvars_ver=14.44 >nul && set"
if($LASTEXITCODE -ne 0){throw 'v143 environment initialization failed'}
foreach($line in $environment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$env:VSLANG='1033'
# Default prefix is verified for this qualified installation's 2052-only UI.
# Another VisualStudio installation/UI must supply its actual /showIncludes
# prefix explicitly with -MsvcIncludePrefix to preserve header dependencies.
$build=Join-Path $repo 'out/input-native/windows'
New-Item -ItemType Directory -Force $build | Out-Null
$common=& git -C $repo rev-parse --path-format=absolute --git-common-dir
if($LASTEXITCODE -ne 0){throw 'Cannot locate cached dependency provenance'}
$main=Split-Path $common -Parent
$provenance=@'
import sys, pathlib, runpy, hashlib, zipfile, tarfile, json
repo, main, build = map(pathlib.Path, sys.argv[1:])
locked = runpy.run_path(str(repo/'tools/setup/prepare_live_dependencies.py'))['PACKAGES']
receipt = {'archives': {}, 'dlls': {}, 'headers_verified': 0}
archives = {}
for name in ('ffmpeg-windows.jar', 'live-deps/ffmpeg-7.1.tar.xz'):
    path = next((r/'out'/name for r in (repo,main) if (r/'out'/name).is_file()), None)
    if path is None: raise RuntimeError('Pinned cache missing: '+name)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != locked[name][1]: raise RuntimeError('Pinned archive hash mismatch: '+name)
    archives[name] = path
    receipt['archives'][name] = {'url': locked[name][0], 'sha256': digest}
with zipfile.ZipFile(archives['ffmpeg-windows.jar']) as jar:
    for name in jar.namelist():
        leaf = pathlib.PurePosixPath(name).name
        if leaf.endswith('.dll') and 'jni' not in leaf:
            digest = hashlib.sha256(jar.read(name)).hexdigest()
            actual = repo/'out/live-deps/ffmpeg-windows'/leaf
            if not actual.is_file() or hashlib.sha256(actual.read_bytes()).hexdigest()!=digest:
                raise RuntimeError('Cached DLL differs from pinned archive: '+leaf)
            receipt['dlls'][leaf] = digest
with tarfile.open(archives['live-deps/ffmpeg-7.1.tar.xz']) as tar:
    for member in tar.getmembers():
        parts = pathlib.PurePosixPath(member.name).parts
        if len(parts)==3 and parts[1].startswith('lib') and parts[2].endswith('.h') and member.isfile():
            actual = repo/'out/live-deps/ffmpeg-headers'/parts[1]/parts[2]
            if actual.read_bytes()!=tar.extractfile(member).read():
                raise RuntimeError('Cached header differs from pinned archive: '+member.name)
            receipt['headers_verified'] += 1
(build/'ffmpeg-provenance.json').write_text(json.dumps(receipt,indent=2))
print('Pinned FFmpeg archives/DLLs verified; headers:', receipt['headers_verified'])
'@
& py -3.13 -c $provenance $repo $main $build
if($LASTEXITCODE -ne 0){throw 'Pinned FFmpeg provenance failed'}
Write-Output "Input build PID=$PID; compiler=$env:VCToolsVersion; source=$repo; output=$build"
& $cmake -S "$repo/native/input" -B $build -G 'Ninja Multi-Config' "-DCMAKE_MAKE_PROGRAM=$ninja" '-DBUILD_TESTING=ON' "-DCMAKE_C_COMPILER=$VisualStudio/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DCMAKE_CXX_COMPILER=$VisualStudio/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" "-DHV_INPUT_MSVC_INCLUDE_PREFIX=$MsvcIncludePrefix"
if($LASTEXITCODE -ne 0){throw 'Standalone input configure failed'}
& $cmake --build $build --config Release
if($LASTEXITCODE -ne 0){throw 'Standalone input build failed'}
$dumpbin="$VisualStudio/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe"
$audit=@()
foreach($dll in Get-ChildItem "$build/Release" -Filter '*.dll'){
    $output=& $dumpbin /dependents $dll.FullName
    if($LASTEXITCODE -ne 0){throw "Dependency audit failed: $($dll.Name)"}
    $imports=$output | Where-Object { $_.Trim() -match '^\S+\.dll$' }
    if(($imports -join "`n") -match '(?i)(onnxruntime|ncnn|humanvision\.dll|runtime_host)'){throw "Inference dependency found: $($dll.Name)"}
    $audit+= $output
}
$audit | Set-Content "$build/dependency-audit.txt"
if($RunTests){
    & "$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe" --test-dir $build -C Release -R input --output-on-failure
    if($LASTEXITCODE -ne 0){throw 'Input tests failed'}
}
Get-FileHash "$build/Release/humanvision_input.dll" -Algorithm SHA256
