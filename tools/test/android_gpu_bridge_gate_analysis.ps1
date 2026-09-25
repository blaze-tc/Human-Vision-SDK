function Get-AndroidGpuBridgeGateAnalysis {
    param([string]$RawLog, [double]$DurationMinutes, [string]$GateSource)
    $lines = @($RawLog -split "`r?`n" | Where-Object { $_ -match 'HV_GPU_GATE' })
    $statuses = @($lines | Where-Object { $_ -match ' converted=\d+ ' })
    $converted = @($statuses | ForEach-Object { if ($_ -match ' converted=(\d+)') { [long]$Matches[1] } })
    $imported = @($statuses | ForEach-Object { if ($_ -match ' imported=(\d+)') { [long]$Matches[1] } })
    $paths = @($statuses | ForEach-Object { if ($_ -match ' path=(\d+)') { [int]$Matches[1] } } | Select-Object -Unique)
    $orientations = @($statuses | ForEach-Object { if ($_ -match ' orientation=([^ ]+)') { $Matches[1] } } | Select-Object -Unique)
    $uuidRows = @($statuses | Where-Object { $_ -match 'unityDeviceUUID=([0-9a-f]{32}) ncnnDeviceUUID=\1 unityDriverUUID=([0-9a-f]{32}) ncnnDriverUUID=\2' -and $_ -notmatch 'UUID=0{32}' })
    $selected = if ($paths.Count -eq 1) { $paths[0] } else { 0 }
    $candidateName = if ($selected -eq 1) { 'blit' } elseif ($selected -eq 2) { 'color_attachment' } else { '' }
    $candidate = @($RawLog -split 'candidate=' | Where-Object { $_ -match "^$candidateName width=" } | Select-Object -Last 1)
    $expectedAhbUsage = if ($selected -eq 1) { 256 } elseif ($selected -eq 2) { 768 } else { 0 }
    $expectedProducerUsage = if ($selected -eq 1) { 6 } elseif ($selected -eq 2) { 20 } else { 0 }
    $usageMatches = $selected -ne 0 -and $statuses.Count -gt 0 -and @($statuses | Where-Object {
        if ($_ -notmatch ' ahbFormat=(\d+) ahbUsage=0x([0-9A-Fa-f]+) formatFeatures=0x([0-9A-Fa-f]+)') { return $true }
        [int]$Matches[1] -ne 1 -or [Convert]::ToUInt64($Matches[2], 16) -ne $expectedAhbUsage -or
            [Convert]::ToUInt64($Matches[3], 16) -eq 0
    }).Count -eq 0
    $probeMatches = $candidate.Count -gt 0 -and $candidate[-1] -match "^$candidateName width=\d+ height=\d+ layers=1 format=1 usage=$expectedAhbUsage stride=\d+" -and
        $candidate[-1] -match "producer vk_format=37 [^\r\n]*image_usage=$expectedProducerUsage(?:\s|$)" -and
        $candidate[-1] -match 'consumer vk_format=37 [^\r\n]*image_usage=4(?:\s|$)' -and
        $candidate[-1] -notmatch 'failed:'
    $pauseStart = $RawLog.IndexOf('HV_GPU_GATE pause=True')
    $pauseEnd = if ($pauseStart -ge 0) { $RawLog.IndexOf('HV_GPU_GATE pause=False', $pauseStart + 1) } else { -1 }
    $pauseRecovery = if ($pauseEnd -ge 0) { $RawLog.IndexOf('HV_GPU_GATE source resumed after=pause', $pauseEnd + 1) } else { -1 }
    $restartStart = $RawLog.IndexOf('HV_GPU_GATE camera restart requested')
    $restartRecovery = if ($restartStart -ge 0) { $RawLog.IndexOf('HV_GPU_GATE source resumed after=restart', $restartStart + 1) } else { -1 }
    $focusLost = $RawLog.IndexOf('HV_GPU_GATE focus=False')
    $focusGained = if ($focusLost -ge 0) { $RawLog.IndexOf('HV_GPU_GATE focus=True', $focusLost + 1) } else { -1 }
    $checks = [ordered]@{
        duration_at_least_10_minutes = $DurationMinutes -ge 9.9
        gpu_conversion_observed = $converted.Count -gt 1 -and $converted[-1] -gt $converted[0]
        gpu_import_observed = $imported.Count -gt 1 -and $imported[-1] -gt $imported[0]
        selected_copy_path = $selected -in @(1,2)
        selected_path_matches_actual_contract = $usageMatches -and $probeMatches
        exact_nonzero_device_and_driver_uuids = $statuses.Count -gt 0 -and $uuidRows.Count -eq $statuses.Count
        portrait_and_both_landscapes = @(@('Portrait','LandscapeLeft','LandscapeRight') | Where-Object { $orientations -notcontains $_ }).Count -eq 0
        pause_resume = $pauseStart -ge 0 -and $pauseEnd -gt $pauseStart
        background_foreground = $focusLost -ge 0 -and $focusGained -gt $focusLost
        camera_restart = @($lines | Where-Object { $_ -match 'camera restart requested' }).Count -gt 0
        pause_and_restart_recovered = $pauseRecovery -gt $pauseEnd -and $restartRecovery -gt $restartStart
        measured_ahb_description = $RawLog -match 'candidate=\w+ width=\d+ height=\d+ layers=\d+ format=\d+ usage=\d+ stride=\d+'
        external_format_and_features = $RawLog -match 'producer vk_format=\d+ external_format=\d+ external_features=\d+' -and $RawLog -match 'consumer vk_format=\d+ external_format=\d+ external_features=\d+'
        external_image_query = $RawLog -match 'externalMemoryFeatures=\d+ compatibleHandleTypes=\d+ maxExtent='
        consumer_sampled_read_only_import = $RawLog -match 'consumer vk_format=[^\r\n]+image_usage=4(?:\s|$)'
        no_status_error = @($statuses | Where-Object { $_ -notmatch ' error=\s*$' }).Count -eq 0 -and $statuses.Count -gt 0
        no_native_or_unity_fatal = $RawLog -notmatch 'FATAL EXCEPTION|Fatal signal|SIGSEGV|SIGABRT|AndroidRuntime.*FATAL|Unity.*(NullReferenceException|DllNotFoundException|EntryPointNotFoundException)'
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
        last_gate_status = if ($statuses.Count) { $statuses[-1] } else { $null }
    }
}
