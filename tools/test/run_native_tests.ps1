param([string]$Filter = '', [string]$VisualStudio = 'D:/Microsoft Visual Studio', [switch]$Fresh)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$cmake = "$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
$ctest = "$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe"
$filterArg = if ($Filter) { '-R "' + $Filter.Replace('"','') + '"' } else { '' }
$freshArg = if ($Fresh) { '--fresh' } else { '' }
$cleanArg = if ($Fresh) { '--clean-first' } else { '' }
$script = @"
@echo off
call "$VisualStudio/Common7/Tools/VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
if errorlevel 1 exit /b 1
set VSLANG=1033
chcp 65001 >nul
"$cmake" $freshArg -S "$taskRoot" -B "$taskRoot/build/windows-test" -G "Ninja Multi-Config" -DBUILD_TESTING=ON -DHV_USE_DIRECTML=OFF
if errorlevel 1 exit /b 1
"$cmake" --build "$taskRoot/build/windows-test" --config Release $cleanArg
if errorlevel 1 exit /b 1
"$ctest" --test-dir "$taskRoot/build/windows-test" -C Release --output-on-failure $filterArg
"@
New-Item -ItemType Directory -Force "$taskRoot/out" | Out-Null
$batchPath = "$taskRoot/out/run-native-tests.cmd"
$script | Set-Content $batchPath -Encoding ascii
& cmd /d /c "`"$batchPath`"" > "$taskRoot/out/native-tests.log" 2>&1
$testExit = $LASTEXITCODE
Get-Content "$taskRoot/out/native-tests.log" -Tail 14
if ($testExit -ne 0) { throw "Native verification failed ($testExit): out/native-tests.log" }
