function Get-AndroidGpuBridgeGateAnalysis {
    param([string]$RawLog, [double]$DurationMinutes, [string]$GateSource,
          [double]$CaptureStartEpoch, [double]$CaptureEndEpoch)
    $lines = @($RawLog -split "`r?`n" | Where-Object { $_ -match 'HV_GPU_GATE' })
    $statuses = @($lines | Where-Object { $_ -match ' converted=\d+ ' })
    $converted = @($statuses | ForEach-Object { if ($_ -match ' converted=(\d+)') { [long]$Matches[1] } })
    $imported = @($statuses | ForEach-Object { if ($_ -match ' imported=(\d+)') { [long]$Matches[1] } })
    $statusRecords = @($statuses | ForEach-Object {
        if ($_ -match '^\s*(\d{10}(?:\.\d+)?)\s+' -and
            $_ -match ' imported=(\d+) converted=(\d+) ') {
            $epochText = ([regex]::Match($_, '^\s*(\d{10}(?:\.\d+)?)\s+')).Groups[1].Value
            [pscustomobject]@{
                epoch = [double]::Parse($epochText, [Globalization.CultureInfo]::InvariantCulture)
                imported = [long]$Matches[1]
                converted = [long]$Matches[2]
                path = [int]([regex]::Match($_, ' path=(\d+)')).Groups[1].Value
                text = $_
            }
        }
    } | Sort-Object epoch)
    $firstEpoch = if ($statusRecords.Count) { $statusRecords[0].epoch } else { 0.0 }
    $lastEpoch = if ($statusRecords.Count) { $statusRecords[-1].epoch } else { 0.0 }
    $largestGap = 0.0
    for ($i = 1; $i -lt $statusRecords.Count; $i++) {
        $largestGap = [Math]::Max($largestGap, $statusRecords[$i].epoch - $statusRecords[$i - 1].epoch)
    }
    $lateBaseline = @($statusRecords | Where-Object { $_.epoch -ge ($CaptureEndEpoch - 90) -and $_.epoch -le ($CaptureEndEpoch - 45) } | Select-Object -Last 1)
    $lateFinal = @($statusRecords | Where-Object { $_.epoch -ge ($CaptureEndEpoch - 30) -and $_.epoch -le ($CaptureEndEpoch + 5) } | Select-Object -Last 1)
    $lateProgress = $lateBaseline.Count -gt 0 -and $lateFinal.Count -gt 0 -and
        $lateFinal[0].imported -gt $lateBaseline[0].imported -and
        $lateFinal[0].converted -gt $lateBaseline[0].converted
    $paths = @($statuses | ForEach-Object { if ($_ -match ' path=(\d+)') { [int]$Matches[1] } } | Select-Object -Unique)
    $orientations = @($statuses | ForEach-Object { if ($_ -match ' orientation=([^ ]+)') { $Matches[1] } } | Select-Object -Unique)
    $activeStatuses = @($statuses | Where-Object { $_ -match ' path=[12] ' })
    $uuidRows = @($activeStatuses | Where-Object { $_ -match 'unityDeviceUUID=([0-9a-f]{32}) ncnnDeviceUUID=\1 unityDriverUUID=([0-9a-f]{32}) ncnnDriverUUID=\2' -and $_ -notmatch 'UUID=0{32}' })
    $activePaths = @($paths | Where-Object { $_ -in @(1,2) })
    $selected = if ($activePaths.Count -eq 1) { $activePaths[0] } else { 0 }
    $lifecycleEpochs = @($lines | Where-Object { $_ -match 'HV_GPU_GATE source rebuilding ' } | ForEach-Object {
        if ($_ -match '^\s*(\d{10}(?:\.\d+)?)\s+') { [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture) }
    })
    $pendingValid = $true
    $previousActive = $null
    foreach ($record in $statusRecords) {
        if ($record.path -in @(1,2)) { $previousActive = $record; continue }
        if ($record.path -ne 0 -or $record.text -notmatch ' result=1 ' -or
            $record.text -notmatch ' ahbFormat=0 ahbUsage=0x0 formatFeatures=0x0 ' -or
            $record.text -notmatch 'unityDeviceUUID=0{32} ncnnDeviceUUID=0{32} unityDriverUUID=0{32} ncnnDriverUUID=0{32}') {
            $pendingValid = $false
            continue
        }
        if ($null -ne $previousActive) {
            $afterLifecycle = @($lifecycleEpochs | Where-Object { $_ -ge $previousActive.epoch -and $_ -le $record.epoch }).Count -gt 0
            if (!$afterLifecycle) { $pendingValid = $false }
        }
        if (@($statusRecords | Where-Object { $_.epoch -gt $record.epoch -and $_.epoch -le ($record.epoch + 30) -and $_.path -in @(1,2) }).Count -eq 0) {
            $pendingValid = $false
        }
    }
    $expectedAhbUsage = if ($selected -eq 1) { 256 } elseif ($selected -eq 2) { 768 } else { 0 }
    $expectedProducerUsage = if ($selected -eq 1) { 6 } elseif ($selected -eq 2) { 20 } else { 0 }
    $usageMatches = $selected -ne 0 -and $activeStatuses.Count -gt 0 -and @($activeStatuses | Where-Object {
        if ($_ -notmatch ' ahbFormat=(\d+) ahbUsage=0x([0-9A-Fa-f]+) formatFeatures=0x([0-9A-Fa-f]+)') { return $true }
        [int]$Matches[1] -ne 1 -or [Convert]::ToUInt64($Matches[2], 16) -ne $expectedAhbUsage -or
            [Convert]::ToUInt64($Matches[3], 16) -eq 0
    }).Count -eq 0
    $generationMarkers = @($lines | Where-Object { $_ -match 'HV_GPU_GATE source rebuilding generation=(\d+) ' })
    $activeGenerations = @($activeStatuses | ForEach-Object {
        if ($_ -match ' generation=(\d+) ') { [ulong]$Matches[1] }
    } | Select-Object -Unique)
    $statusGenerations = @($statuses | ForEach-Object {
        if ($_ -match ' generation=(\d+) ') { [ulong]$Matches[1] }
    } | Select-Object -Unique)
    $probeMatches = $activeGenerations.Count -gt 0 -and
        $activeGenerations.Count -eq $generationMarkers.Count -and
        $statusGenerations.Count -eq $activeGenerations.Count -and
        @($statuses | Where-Object { $_ -notmatch ' generation=\d+ ' }).Count -eq 0
    foreach ($generation in $activeGenerations) {
        $marker = @($generationMarkers | Where-Object { $_ -match "HV_GPU_GATE source rebuilding generation=$generation " })
        $probeLines = @($lines | Where-Object { $_ -match "HV_GPU_GATE probe generation=$generation " })
        $generationStatuses = @($activeStatuses | Where-Object { $_ -match " generation=$generation " })
        $probeText = ($probeLines | ForEach-Object { ($_ -split "HV_GPU_GATE probe generation=$generation ", 2)[-1] }) -join "`n"
        $candidateName = if ($selected -eq 1) { 'blit' } elseif ($selected -eq 2) { 'color_attachment' } else { '' }
        $candidate = @($probeText -split 'candidate=' | Where-Object { $_ -match "^$candidateName width=" } | Select-Object -Last 1)
        $markerEpoch = if ($marker.Count -eq 1 -and $marker[0] -match '^\s*(\d{10}(?:\.\d+)?)\s+') { [double]$Matches[1] } else { 0 }
        $firstActiveEpoch = if ($generationStatuses.Count -and $generationStatuses[0] -match '^\s*(\d{10}(?:\.\d+)?)\s+') { [double]$Matches[1] } else { 0 }
        $probeEpochs = @($probeLines | ForEach-Object { if ($_ -match '^\s*(\d{10}(?:\.\d+)?)\s+') { [double]$Matches[1] } })
        if ($marker.Count -ne 1 -or $candidate.Count -ne 1 -or
            @($probeLines | Where-Object { $_ -match "candidate=$candidateName width=" }).Count -ne 1 -or
            $probeEpochs.Count -ne $probeLines.Count -or
            @($probeEpochs | Where-Object { $_ -lt $markerEpoch -or $_ -gt $firstActiveEpoch }).Count -gt 0 -or
            $candidate[-1] -notmatch "^$candidateName width=\d+ height=\d+ layers=1 format=1 usage=$expectedAhbUsage stride=\d+" -or
            $candidate[-1] -notmatch "producer vk_format=37 [^\r\n]*image_usage=$expectedProducerUsage(?:\s|$)" -or
            $candidate[-1] -notmatch 'consumer vk_format=37 [^\r\n]*image_usage=4(?:\s|$)' -or
            $candidate[-1] -notmatch 'externalMemoryFeatures=\d+ compatibleHandleTypes=\d+ maxExtent=' -or
            $candidate[-1] -match 'failed:') { $probeMatches = $false }
    }
    $pauseStart = $RawLog.IndexOf('HV_GPU_GATE pause=True')
    $pauseEnd = if ($pauseStart -ge 0) { $RawLog.IndexOf('HV_GPU_GATE pause=False', $pauseStart + 1) } else { -1 }
    $pauseRecovery = if ($pauseEnd -ge 0) { $RawLog.IndexOf('HV_GPU_GATE source resumed after=pause', $pauseEnd + 1) } else { -1 }
    $restartStart = $RawLog.IndexOf('HV_GPU_GATE camera restart requested')
    $restartRecovery = if ($restartStart -ge 0) { $RawLog.IndexOf('HV_GPU_GATE source resumed after=restart', $restartStart + 1) } else { -1 }
    $focusLost = $RawLog.IndexOf('HV_GPU_GATE focus=False')
    $focusGained = if ($focusLost -ge 0) { $RawLog.IndexOf('HV_GPU_GATE focus=True', $focusLost + 1) } else { -1 }
    $recoveryProgress = $true
    foreach ($marker in @('source resumed after=pause', 'source resumed after=restart')) {
        $eventLine = @($lines | Where-Object { $_ -match [regex]::Escape($marker) } | Select-Object -Last 1)
        if ($eventLine.Count -eq 0 -or $eventLine[0] -notmatch '^\s*(\d{10}(?:\.\d+)?)\s+') {
            $recoveryProgress = $false
            continue
        }
        $eventEpoch = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
        $after = @($statusRecords | Where-Object { $_.epoch -gt $eventEpoch -and $_.epoch -le ($eventEpoch + 30) })
        if ($after.Count -lt 2 -or $after[-1].imported -le $after[0].imported -or
            $after[-1].converted -le $after[0].converted) { $recoveryProgress = $false }
    }
    $checks = [ordered]@{
        duration_at_least_10_minutes = $DurationMinutes -ge 9.9
        timestamped_status_coverage = $CaptureEndEpoch -gt $CaptureStartEpoch -and
            $statusRecords.Count -eq $statuses.Count -and $statusRecords.Count -gt 1 -and
            $firstEpoch -ge ($CaptureStartEpoch - 5) -and $firstEpoch -le ($CaptureStartEpoch + 30) -and
            $lastEpoch -ge ($CaptureEndEpoch - 30) -and $lastEpoch -le ($CaptureEndEpoch + 5) -and
            ($lastEpoch - $firstEpoch) -ge 570 -and $largestGap -le 60
        imported_and_converted_progress_near_end = $lateProgress
        gpu_conversion_observed = $converted.Count -gt 1 -and $converted[-1] -gt $converted[0]
        gpu_import_observed = $imported.Count -gt 1 -and $imported[-1] -gt $imported[0]
        selected_copy_path = $selected -in @(1,2) -and @($paths | Where-Object { $_ -notin @(0,$selected) }).Count -eq 0
        pending_source_measurements_recover = $pendingValid
        probe_for_each_configured_generation = $probeMatches
        selected_path_matches_actual_contract = $usageMatches -and $probeMatches
        exact_nonzero_device_and_driver_uuids = $activeStatuses.Count -gt 0 -and $uuidRows.Count -eq $activeStatuses.Count
        portrait_and_both_landscapes = @(@('Portrait','LandscapeLeft','LandscapeRight') | Where-Object { $orientations -notcontains $_ }).Count -eq 0
        pause_resume = $pauseStart -ge 0 -and $pauseEnd -gt $pauseStart
        background_foreground = $focusLost -ge 0 -and $focusGained -gt $focusLost
        camera_restart = @($lines | Where-Object { $_ -match 'camera restart requested' }).Count -gt 0
        pause_and_restart_recovered = $pauseRecovery -gt $pauseEnd -and $restartRecovery -gt $restartStart
        post_recovery_import_and_conversion_progress = $recoveryProgress
        measured_ahb_description = $RawLog -match 'candidate=\w+ width=\d+ height=\d+ layers=\d+ format=\d+ usage=\d+ stride=\d+'
        external_format_and_features = $RawLog -match 'producer vk_format=\d+ external_format=\d+ external_features=\d+' -and $RawLog -match 'consumer vk_format=\d+ external_format=\d+ external_features=\d+'
        external_image_query = $RawLog -match 'externalMemoryFeatures=\d+ compatibleHandleTypes=\d+ maxExtent='
        consumer_sampled_read_only_import = $RawLog -match 'consumer vk_format=[^\r\n]+image_usage=4(?:\s|$)'
        no_status_error = @($statuses | Where-Object { $_ -notmatch ' error=<none>\s*$' }).Count -eq 0 -and $statuses.Count -gt 0
        no_native_or_unity_fatal = $RawLog -notmatch 'FATAL EXCEPTION|Fatal signal|SIGSEGV|SIGABRT|AndroidRuntime.*FATAL|crash_dump|tombstoned|Abort message|(?m:^\s*\d{10}(?:\.\d+)?\s+[^\r\n]*\bE\s+Unity\s*:)|Unity[^\r\n]*\b(?:InvalidOperationException|NullReferenceException|DllNotFoundException|EntryPointNotFoundException)\s*:'
        gate_component_has_no_cpu_readback_api = $GateSource -notmatch 'AsyncGPUReadback|GetPixels\s*\(|ReadPixels\s*\('
    }
    [pscustomobject]@{
        result = if (@($checks.Values | Where-Object { $_ -eq $false }).Count -eq 0) { 'PASS_CANDIDATE_REQUIRES_USER_REVIEW' } else { 'FAIL' }
        checks = $checks
        selected_paths = $paths
        orientations = $orientations
        first_imported = if ($imported.Count) { $imported[0] } else { $null }
        last_imported = if ($imported.Count) { $imported[-1] } else { $null }
        first_converted = if ($converted.Count) { $converted[0] } else { $null }
        last_converted = if ($converted.Count) { $converted[-1] } else { $null }
        first_status_epoch = $firstEpoch
        last_status_epoch = $lastEpoch
        largest_status_gap_seconds = $largestGap
        last_gate_status = if ($statuses.Count) { $statuses[-1] } else { $null }
    }
}
