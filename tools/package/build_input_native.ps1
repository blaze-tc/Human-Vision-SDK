param([ValidateSet('Windows','Android')][string]$Platform='Windows',[switch]$RunTests,
      [ValidateSet(26)][int]$ApiLevel=26,
      [string]$AndroidNdk='D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK',
      [string]$VisualStudio='D:/Microsoft Visual Studio',
      [string]$MsvcIncludePrefix='注意: 包含文件:  ')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$cmake="$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
$ninja="$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
if($Platform -eq 'Android') {
    $build=Join-Path $repo 'out/input-native/android'
    New-Item -ItemType Directory -Force $build | Out-Null
    $common=& git -C $repo rev-parse --path-format=absolute --git-common-dir
    $main=Split-Path $common -Parent
    $verify=@'
import sys,pathlib,runpy,hashlib,zipfile,json
repo,main,build=map(pathlib.Path,sys.argv[1:])
lock=runpy.run_path(str(repo/'tools/setup/prepare_live_dependencies.py'))['PACKAGES']
path=next((r/'out/ffmpeg-android.jar' for r in (repo,main) if (r/'out/ffmpeg-android.jar').is_file()),None)
if path is None or hashlib.sha256(path.read_bytes()).hexdigest()!=lock['ffmpeg-android.jar'][1]: raise RuntimeError('Locked Android FFmpeg cache missing or invalid')
receipt={'archive_sha256':lock['ffmpeg-android.jar'][1],'libraries':{}}
with zipfile.ZipFile(path) as jar:
 for entry in jar.namelist():
  leaf=pathlib.PurePosixPath(entry).name
  if leaf.endswith('.so') and 'jni' not in leaf:
   target=repo/'out/live-deps/ffmpeg-android'/leaf
   digest=hashlib.sha256(jar.read(entry)).hexdigest()
   if not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest()!=digest: raise RuntimeError('Unqualified Android library: '+leaf)
   receipt['libraries'][leaf]=digest
(build/'ffmpeg-provenance.json').write_text(json.dumps(receipt,indent=2))
'@
    & py -3.13 -c $verify $repo $main $build
    if($LASTEXITCODE -ne 0){throw 'Android dependency provenance failed'}
    & $cmake --fresh -S "$repo/native/input" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$AndroidNdk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a "-DANDROID_PLATFORM=android-$ApiLevel" -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    if($LASTEXITCODE -ne 0){throw 'Input Android configure failed'}
    & $cmake --build $build
    if($LASTEXITCODE -ne 0){throw 'Input Android build failed'}
    $readelf="$AndroidNdk/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe"
    & $readelf -h -d -Ws "$build/libhumanvision_input.so" > "$build/dependency-symbol-audit.txt"
    if($LASTEXITCODE -ne 0){throw 'Android audit failed'}
    $audit=Get-Content "$build/dependency-symbol-audit.txt" -Raw
    if($audit -notmatch 'AArch64' -or $audit -match 'onnxruntime|ncnn|libhumanvision\.so|runtime_host|sws_scale|AImage_getPlaneData|AHardwareBuffer_lock'){throw 'Input Android independence/CPU-path audit failed'}
    $nm="$AndroidNdk/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-nm.exe"
    $required=& $nm -D --undefined-only "$build/libhumanvision_input.so"
    $system=@{}
    foreach($library in @('libc.so','libm.so','libdl.so','libandroid.so','libmediandk.so','liblog.so')){
      foreach($line in (& $nm -D --defined-only "$AndroidNdk/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/$ApiLevel/$library")){
        if($line -match '\s([A-Za-z_]\w*)(?:@.*)?$'){$system[$matches[1]]=$true}
      }
    }
    foreach($library in @('libavformat.so','libavcodec.so','libavutil.so','libswresample.so')) {
      foreach($line in (& $nm -D --defined-only "$repo/out/live-deps/ffmpeg-android/$library")) {if($line -match '\s([A-Za-z_]\w*)(?:@.*)?$'){$system[$matches[1]]=$true}}
    }
    $unresolved=@($required | ForEach-Object {if($_ -match '\sU\s+(\w+)'){if(-not $system.ContainsKey($matches[1])){$matches[1]}}})
    if($unresolved.Count){throw "API26 unresolved imports: $($unresolved -join ',')"}
    $closure=@("$build/libhumanvision_input.so")+@('libavformat.so','libavcodec.so','libavutil.so','libswresample.so' | ForEach-Object {"$repo/out/live-deps/ffmpeg-android/$_"})
    $closureReceipt=@()
    foreach($library in $closure) {
      $required=& $nm -D --undefined-only $library
      $unresolved=@($required | ForEach-Object {if($_ -match '\sU\s+(\w+)'){if(-not $system.ContainsKey($matches[1])){$matches[1]}}})
      if($unresolved.Count){throw "API26 dependency unresolved imports ($library): $($unresolved -join ',')"}
      $elf=& $readelf -h -d $library
      if($LASTEXITCODE -ne 0 -or ($elf -join "`n") -notmatch 'AArch64'){throw "Invalid ARM64 dependency: $library"}
      $closureReceipt+=@{path=$library;sha256=(Get-FileHash $library).Hash;strongImportsResolvedApi=26;elf=($elf -join "`n")}
    }
    $closureReceipt | ConvertTo-Json -Depth 5 | Set-Content "$build/api26-closure-audit.json"
    if($RunTests){
      # Build and execute actual Windows policy tests plus all unchanged ABI/session regressions.
      & pwsh -NoProfile -File $PSCommandPath -Platform Windows -RunTests -VisualStudio $VisualStudio -MsvcIncludePrefix $MsvcIncludePrefix
      if($LASTEXITCODE -ne 0){throw 'Input host tests failed'}
    }
    Get-FileHash "$build/libhumanvision_input.so" -Algorithm SHA256
    exit 0
}
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
# Host declaration tests use the same qualified NDK Vulkan type declarations.
# Copy only its two Vulkan headers, so NDK libc headers cannot shadow MSVC's.
$vulkanHeaders=Join-Path $repo 'out/input-native/vulkan-host-headers/vulkan'
New-Item -ItemType Directory -Force $vulkanHeaders | Out-Null
foreach($header in @('vulkan_core.h','vk_platform.h')){
    Copy-Item -LiteralPath "$AndroidNdk/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include/vulkan/$header" -Destination $vulkanHeaders -Force
}
Get-FileHash "$vulkanHeaders/vulkan_core.h","$vulkanHeaders/vk_platform.h" -Algorithm SHA256 | ConvertTo-Json | Set-Content "$build/vulkan-header-provenance.json"
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
