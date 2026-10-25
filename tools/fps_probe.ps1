# Short, input-free timing comparison in its own run folder; leaves the player's settings and saves alone.
# Ask before launching while the user is using the PC (HANDOVER.md).
# powershell -NoProfile -File tools/fps_probe.ps1 -Cap 144 -Unlocked -Seconds 14
param(
    [int]$Cap = 120,
    [switch]$Unlocked,
    [string]$Level = "1_1",
    [int]$Seconds = 14,
    [switch]$Coop
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if (Get-Process MOHRS,MOHRSCoop -ErrorAction SilentlyContinue) { throw "Close the running game before this test." }
if ($Cap -lt 0 -or $Seconds -lt 2 -or $Level -notmatch '^\d+_\d+$') { throw "Invalid probe arguments" }
$tag = "{0}_{1}_{2}_{3}" -f $Cap, $Unlocked.IsPresent, $Level, (Get-Date -Format 'yyyyMMdd_HHmmss_fff')
$dir = Join-Path $root "re\fps-probes\$tag"
New-Item -ItemType Directory -Path $dir -Force | Out-Null
foreach ($name in 'MOHRS.exe','MOHRS.dll','MOHRSCoop.exe','MOHRSCoop.dll') {
    Copy-Item -LiteralPath (Join-Path $root "out\build\release\$name") -Destination $dir
}
Get-ChildItem -LiteralPath $dir -Filter '*.dll' | ForEach-Object { $_.LastWriteTime = Get-Date }
$flag = $Unlocked.IsPresent.ToString().ToLowerInvariant()
@"
display_mode = 'windowed'
width = 1280
height = 720
vsync = false
fps_cap = $Cap
fps60 = true
unlocked_fps = $flag
fps_counter = true
skip_intros = true
skip_cutscenes = true
"@ | Set-Content -LiteralPath (Join-Path $dir 'settings.toml') -Encoding ascii
$oldLevel = $env:MOHRS_START_LEVEL
$oldTiming = $env:MOHRS_TIMING
$oldPerf = $env:XBE_PERF
$env:MOHRS_START_LEVEL = $Level
$env:MOHRS_TIMING = '1'
$env:XBE_PERF = '1'
$exe = Join-Path $dir $(if ($Coop) { 'MOHRSCoop.exe' } else { 'MOHRS.exe' })
try {
    $p = Start-Process -FilePath $exe -WorkingDirectory $dir -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds $Seconds
    $live = @(Get-Process MOHRS,MOHRSCoop -ErrorAction SilentlyContinue | Where-Object { $_.Path -and (Split-Path $_.Path) -eq $dir })
    "probe $tag : $($live.Count) running after $Seconds seconds"
} finally {
    $owned = @(Get-Process MOHRS,MOHRSCoop -ErrorAction SilentlyContinue | Where-Object { $_.Path -and (Split-Path $_.Path) -eq $dir })
    if ($owned.Count) {
        $owned | Stop-Process -Force
        $owned | Wait-Process -ErrorAction SilentlyContinue
    }
    # Windows can briefly enumerate a terminated graphics process while its device tears down.
    for ($retry = 0; $retry -lt 20; $retry++) {
        $remaining = @(Get-Process MOHRS,MOHRSCoop -ErrorAction SilentlyContinue | Where-Object { $_.Id -in $owned.Id })
        if (-not $remaining) { break }
        Start-Sleep -Milliseconds 100
    }
    $env:MOHRS_START_LEVEL = $oldLevel
    $env:MOHRS_TIMING = $oldTiming
    $env:XBE_PERF = $oldPerf
}
$log = Join-Path $dir 'logs\mohrs.log'
if (Test-Path $log) {
    Select-String -LiteralPath $log -Pattern 'timing:|perf:|\[error\]' | ForEach-Object Line
} else { throw "No game log: $dir" }
"results: $dir"
