param([ValidateSet('Windows')][string]$Platform='Windows', [string]$Output='out/input/task4', [string]$Unity='D:/Developer/2021.3.45f1/Editor/Unity.exe', [switch]$BufferFailureOnly)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputPath=[IO.Path]::GetFullPath((Join-Path $repo $Output))
if(-not $outputPath.StartsWith(([IO.Path]::GetFullPath((Join-Path $repo 'out/input'))+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Output must be in checkout out/input.'}
$runId=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ')+'-'+[Guid]::NewGuid().ToString('N')
$runPath=Join-Path $outputPath $runId
$project=Join-Path $outputPath 'project'
foreach($d in @($runPath,"$project/Assets/StreamingAssets","$project/Assets/Plugins/x86_64","$project/Packages","$project/ProjectSettings")){New-Item -ItemType Directory -Force $d|Out-Null}
$lock=(& py -3.13 "$PSScriptRoot/rtsp_fixture_manifest.py")|ConvertFrom-Json
function CheckHash($path,$expected){if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected){throw "Fixture hash mismatch: $path"}}
$cache=Join-Path $repo 'out/tools/mediamtx-v1.12.3'
New-Item -ItemType Directory -Force $cache|Out-Null
if(-not(Test-Path "$cache/archive.zip")){Invoke-WebRequest $lock.mediamtx_url -OutFile "$cache/archive.zip"}
CheckHash "$cache/archive.zip" $lock.mediamtx_archive_sha256
if(-not(Test-Path "$cache/mediamtx.exe")){Expand-Archive -LiteralPath "$cache/archive.zip" -DestinationPath $cache}
CheckHash "$cache/mediamtx.exe" $lock.mediamtx_sha256
$ffmpeg='C:/ffmpeg/bin/ffmpeg.exe'
$video='E:/Project/Human Vision SDK/video-1.mp4'
CheckHash $ffmpeg $lock.ffmpeg_sha256; CheckHash $video $lock.video_sha256
CheckHash "$repo/out/input-native/windows/Release/humanvision_input.dll" $lock.native_sha256
$nativeProvenance=Get-Content "$repo/out/input-native/windows/ffmpeg-provenance.json" -Raw|ConvertFrom-Json
$nativeHashes=[ordered]@{}
foreach($dll in Get-ChildItem "$repo/out/input-native/windows/Release" -Filter '*.dll' | Where-Object Name -ne 'input_legacy_rtsp_fixture.dll'){
    if($dll.Name -ne 'humanvision_input.dll'){
        $expected=$nativeProvenance.dlls.($dll.Name)
        if(-not $expected){throw "Unqualified dependency: $($dll.Name)"}; CheckHash $dll.FullName $expected
    }
    Copy-Item -LiteralPath $dll.FullName -Destination "$project/Assets/Plugins/x86_64/$($dll.Name)" -Force
    $nativeHashes[$dll.Name]=(Get-FileHash $dll.FullName).Hash
}
$unexpected=@(Get-ChildItem "$project/Assets/Plugins/x86_64" -Filter '*.dll'|Where-Object{-not $nativeHashes.Contains($_.Name)})
if($unexpected.Count -ne 0){throw 'Unexpected native DLL in isolated fixture project.'}
$fixture=Join-Path $outputPath 'asymmetric-video-1-h264.mp4'
# Actual approved video content plus four asymmetric diagnostic corners; no substitute VideoPlayer URL.
$filter='scale=640:360,drawbox=x=0:y=0:w=48:h=48:color=red:t=fill,drawbox=x=592:y=0:w=48:h=48:color=green:t=fill,drawbox=x=0:y=312:w=48:h=48:color=blue:t=fill,drawbox=x=592:y=312:w=48:h=48:color=white:t=fill'
& $ffmpeg -hide_banner -nostdin -y -i $video -t 8 -an -vf $filter -c:v libx264 -preset ultrafast -pix_fmt yuv420p -g 25 -bf 0 $fixture 2> "$runPath/fixture-encode.log"
if($LASTEXITCODE -ne 0){throw 'Fixture encode failed'}
& $ffmpeg -version > "$runPath/ffmpeg-version.txt"
& "$cache/mediamtx.exe" --version > "$runPath/mediamtx-version.txt"
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
$config=Join-Path $runPath 'mediamtx.yml'
@"
logLevel: info
rtspAddress: 127.0.0.1:$port
rtspTransports: [tcp]
rtmp: no
hls: no
webrtc: no
srt: no
paths:
  fixture:
    source: publisher
"@|Set-Content -LiteralPath $config -Encoding utf8
@{server="$cache/mediamtx.exe";config=$config;ffmpeg=$ffmpeg;video=$fixture;evidence=$runPath;port=$port}|ConvertTo-Json|Set-Content "$project/Assets/StreamingAssets/rtsp-fixture.json" -Encoding utf8
$dependencies=@{'com.blazetc.humanvision.input'=('file:'+((Join-Path $repo 'upm/com.blazetc.humanvision.input') -replace '\\','/'))}
foreach($package in @('com.unity.test-framework@1.1.33','com.unity.ext.nunit@1.0.6')){
    $name,$version=$package -split '@'
    $cached=@("$repo/unity/HumanVisionDemo/Library/PackageCache/$package","E:/UnityProject/Human-Vision-SDK-Test/Library/PackageCache/$package")|Where-Object{Test-Path "$_/package.json"}|Select-Object -First 1
    $dependencies[$name]=if($cached){'file:'+($cached -replace '\\','/')}else{$version}
}
@{dependencies=$dependencies;testables=@('com.blazetc.humanvision.input')}|ConvertTo-Json -Depth 5|Set-Content "$project/Packages/manifest.json" -Encoding utf8
"m_EditorVersion: 2021.3.45f1`nm_EditorVersionWithRevision: 2021.3.45f1 (0da89fac8e79)"|Set-Content "$project/ProjectSettings/ProjectVersion.txt"
$receipts=@()
$missingProject=Join-Path $runPath 'missing-native-project'
foreach($d in @("$missingProject/Assets","$missingProject/Packages","$missingProject/ProjectSettings")){New-Item -ItemType Directory -Force $d|Out-Null}
Copy-Item "$project/Packages/manifest.json" "$missingProject/Packages/manifest.json"
Copy-Item "$project/ProjectSettings/ProjectVersion.txt" "$missingProject/ProjectSettings/ProjectVersion.txt"
$selections=@(@{platform='EditMode';label='EditMode';class='RtspSourceTests';names=@('NativeLayoutsMatchVersionOne','InvalidSettingsFailWithoutEchoingCredentials','FailedBufferGrowthPreservesOwnershipUntilClose');project=$project},@{platform='PlayMode';label='PlayMode';class='RtspPreviewTests';names=@('PreviewWithoutInferencePackage','ConnectionLossReconnectsWithoutBlockingControls','WarmedNativeUploadDoesNotAllocateOrWaitForConsumer','DestroyedSourceRetiresNativeWorkerWithoutTexture');project=$project},@{platform='EditMode';label='MissingNative';class='RtspSourceTests';names=@('MissingInputPluginIsActionable');project=$missingProject})
if($BufferFailureOnly){$selections=@(@{platform='EditMode';label='EditMode';class='RtspSourceTests';names=@('FailedBufferGrowthPreservesOwnershipUntilClose');project=$project})}
foreach($selection in $selections){
    $xml="$runPath/$($selection.label)-results.xml";$log="$runPath/$($selection.label)-unity.log"
    $filter=($selection.names|ForEach-Object{"HumanVision.Input.Tests.$($selection.class).$_"}) -join ';'
    $arguments="-batchmode -force-d3d11 -projectPath `"$($selection.project)`" -runTests -testPlatform $($selection.platform) -testFilter `"$filter`" -testResults `"$xml`" -logFile `"$log`""
    $info=[Diagnostics.ProcessStartInfo]::new($Unity,$arguments);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle='Hidden';$info.Environment['__COMPAT_LAYER']='RunAsInvoker'
    $process=[Diagnostics.Process]::Start($info)
    $timedOut=-not $process.WaitForExit(240000)
    if($timedOut){$process.Kill();$process.WaitForExit()}
    # Crash/timeout cleanup only for recorded helper identities created by this run.
    if(Test-Path "$runPath/owned-pids.txt"){
        foreach($identity in Get-Content "$runPath/owned-pids.txt"){
            $parts=$identity -split '\|',3
            $owned=Get-Process -Id ([int]$parts[0]) -ErrorAction SilentlyContinue
            if($owned -and $owned.StartTime.ToUniversalTime().Ticks -eq [long]$parts[1] -and [IO.Path]::GetFullPath($owned.Path) -eq [IO.Path]::GetFullPath($parts[2])){Stop-Process -Id $owned.Id -Force}
        }
    }
    $status='Failed';$names=@();$passed=0;$failed=0
    if(Test-Path $xml){
        [xml]$result=Get-Content $xml -Raw;$cases=@($result.SelectNodes('//test-case'))
        $names=@($cases|ForEach-Object{$_.fullname});$passed=@($cases|Where-Object result -eq Passed).Count;$failed=$cases.Count-$passed
        $expected=@($selection.names|ForEach-Object{"HumanVision.Input.Tests.$($selection.class).$_"})
        if(-not $timedOut -and $process.ExitCode -eq 0 -and $failed -eq 0 -and $result.'test-run'.result -eq 'Passed' -and @(Compare-Object ($expected|Sort-Object) ($names|Sort-Object)).Count -eq 0){$status='Passed'}
    }
    $receipt=@{status=$status;platform=$selection.platform;exitCode=$process.ExitCode;timedOut=$timedOut;pid=$process.Id;arguments=$arguments;names=$names;passed=$passed;failed=$failed;xml=$xml;log=$log}
    $receipts+=$receipt;$receipt|ConvertTo-Json -Depth 5|Set-Content "$runPath/$($selection.label)-summary.json"
    & py -3.13 "$PSScriptRoot/rtsp_fixture_manifest.py" $repo "$runPath/source-manifest.json" $selection.project
    if($LASTEXITCODE -ne 0){throw 'Fixture source/dependency provenance capture failed.'}
    $artifacts=[ordered]@{};Get-ChildItem $runPath -File|Where-Object Name -ne summary.json|ForEach-Object{$artifacts[$_.Name]=(Get-FileHash $_.FullName).Hash}
    foreach($assembly in Get-ChildItem "$($selection.project)/Library/ScriptAssemblies" -Filter 'HumanVision*.dll' -ErrorAction SilentlyContinue){$artifacts[$assembly.FullName]=(Get-FileHash $assembly.FullName).Hash}
    $packageAudit=@{manifest=(Get-Content "$project/Packages/manifest.json" -Raw|ConvertFrom-Json);nativeDlls=@($nativeHashes.Keys);recognitionAssemblies=@(Get-ChildItem "$project/Library/ScriptAssemblies" -Filter 'HumanVision*.dll'|Where-Object Name -notmatch '^HumanVision\.Input(\.|\.dll)')}
    if($packageAudit.recognitionAssemblies.Count -ne 0){throw 'Input fixture project unexpectedly compiled recognition assemblies.'}
    @{runId=$runId;status=$status;runs=$receipts;fixtureSha256=(Get-FileHash $fixture).Hash;nativeSha256=$nativeHashes;artifactSha256=$artifacts;project=$project;packageAudit=$packageAudit;lock=$lock}|ConvertTo-Json -Depth 8|Set-Content "$runPath/summary.json"
    $receipt|ConvertTo-Json -Depth 5
    if($status -ne 'Passed'){throw "RTSP $($selection.platform) failed: $log"}
}
