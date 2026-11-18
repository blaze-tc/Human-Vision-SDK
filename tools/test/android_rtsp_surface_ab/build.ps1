param(
    [string]$Output = 'out/diagnostics/hlk-rtsp-20261118/probe',
    [string]$AndroidPlayer = 'D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer',
    [string]$Ndk = 'D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK',
    [string]$Keystore = "$env:USERPROFILE/.android/debug.keystore"
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
if (-not [IO.Path]::IsPathRooted($Output)) { $Output = Join-Path $root $Output }
$Output = (New-Item -ItemType Directory -Path $Output -Force).FullName
$tools = "$AndroidPlayer/SDK/build-tools/34.0.0"
$androidJar = "$AndroidPlayer/SDK/platforms/android-34/android.jar"
$llvm = "$Ndk/toolchains/llvm/prebuilt/windows-x86_64/bin"
$deps = "$root/out/live-deps"
$env:JAVA_HOME = "$AndroidPlayer/OpenJDK"
$env:PATH = "$env:JAVA_HOME/bin;$env:PATH"

New-Item -ItemType Directory -Path "$Output/classes", "$Output/dex", "$Output/lib/arm64-v8a" -Force | Out-Null
& "$llvm/clang++.exe" --target=aarch64-linux-android26 -std=c++17 -O2 -Wall -Wextra -Werror -fPIC -shared -static-libstdc++ `
    -I "$deps/ffmpeg-headers" "$PSScriptRoot/probe.cpp" -L "$deps/ffmpeg-android" `
    -lavformat -lavcodec -lavutil -lmediandk -landroid -llog -ldl '-Wl,--no-undefined' -o "$Output/lib/arm64-v8a/libhv_surface_ab.so"
foreach ($dependency in 'libavformat.so', 'libavcodec.so', 'libavutil.so', 'libswresample.so') {
    Copy-Item -LiteralPath "$deps/ffmpeg-android/$dependency" -Destination "$Output/lib/arm64-v8a/$dependency" -Force
}
& "$llvm/llvm-readelf.exe" -h -d "$Output/lib/arm64-v8a/libhv_surface_ab.so" > "$Output/native-elf.txt"

& "$env:JAVA_HOME/bin/javac.exe" -encoding UTF-8 -source 8 -target 8 -classpath $androidJar -d "$Output/classes" "$PSScriptRoot/ProbeActivity.java"
$classes = @(Get-ChildItem -LiteralPath "$Output/classes" -Recurse -Filter '*.class' | ForEach-Object FullName)
& "$tools/d8.bat" --release --min-api 26 --lib $androidJar --output "$Output/dex" @classes
& "$tools/aapt2.exe" link -o "$Output/base.apk" -I $androidJar --manifest "$PSScriptRoot/AndroidManifest.xml"
& py -3.13 -c @'
import pathlib, sys, zipfile
root = pathlib.Path(sys.argv[1])
with zipfile.ZipFile(root / 'base.apk') as base, zipfile.ZipFile(root / 'unsigned.apk', 'w') as apk:
    for info in base.infolist():
        apk.writestr(info, base.read(info))
    apk.write(root / 'dex/classes.dex', 'classes.dex', zipfile.ZIP_STORED)
    for lib in sorted((root / 'lib/arm64-v8a').glob('*.so')):
        apk.write(lib, lib.relative_to(root).as_posix(), zipfile.ZIP_STORED)
'@ $Output
& "$tools/zipalign.exe" -f -p 4 "$Output/unsigned.apk" "$Output/aligned.apk"
& "$tools/apksigner.bat" sign --ks $Keystore --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out "$Output/SurfaceAB.apk" "$Output/aligned.apk"
& "$tools/apksigner.bat" verify --verbose --print-certs "$Output/SurfaceAB.apk" > "$Output/signature.txt"
& "$tools/aapt2.exe" dump badging "$Output/SurfaceAB.apk" > "$Output/badging.txt"
$identities = Get-FileHash -Algorithm SHA256 -LiteralPath @("$Output/SurfaceAB.apk", "$PSScriptRoot/probe.cpp", "$PSScriptRoot/ProbeActivity.java", "$PSScriptRoot/AndroidManifest.xml", "$PSScriptRoot/build.ps1")
$identities += Get-ChildItem -LiteralPath "$Output/lib/arm64-v8a" -Filter '*.so' | Get-FileHash -Algorithm SHA256
$identities | ForEach-Object { '{0}  {1}' -f $_.Hash.ToLowerInvariant(), $_.Path } | Set-Content -Encoding utf8 "$Output/SHA256SUMS.txt"
$identities[0] | Format-List
