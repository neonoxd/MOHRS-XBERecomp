# Short, input-free timing comparison in its own run folder; leaves the player's settings and saves alone.
# Ask before launching while the user is using the PC (HANDOVER.md).
# powershell -NoProfile -File tools/fps_probe.ps1 -Cap 144 -Unlocked -Seconds 14
param(
    [int]$Cap = 120,
    [switch]$Unlocked,
    [string]$Level = "1_1",
    [int]$Seconds = 14,
    [ValidateRange(45,140)][int]$Fov = 70,
    [ValidateRange(640,7680)][int]$Width = 1280,
    [ValidateRange(480,4320)][int]$Height = 720,
    [ValidateRange(25,200)][int]$RenderPercent = 100,
    [switch]$Coop,
    [switch]$NoPerf,
    # build folder under out\build to copy the executables from (profile = release with line tables)
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Build = "release",
    # a normal window, for tools/screenshot.ps1 (it brings the window to the front)
    [switch]$Visible,
    # quick saves, loads and frame captures (XBE_CAPTURE_DIR) at seconds after the first gameplay frame, as
    # "15:save,25:load,30:capture" (docs/quick-save.md)
    [ValidatePattern('^$|^[0-9.]+:(save|load|capture)(,[0-9.]+:(save|load|capture))*$')][string]$QuickSave = "",
    # an earlier probe's folder: its quicksave\ is copied in, so this run can load what that one saved
    [string]$QuickSaveFrom = "",
    # the original 30 FPS mode instead of the 60 FPS one (without -Unlocked)
    [switch]$Fps30,
    # virtual pad input, seconds after the first poll: "9/6:forward" holds the stick forward 6 s, "3:a" taps A
    [ValidatePattern('^$|^[0-9.]+(/[0-9.]+)?:[a-z]+(,[0-9.]+(/[0-9.]+)?:[a-z]+)*$')][string]$Pad = "",
    # timing report interval in seconds (below 1; default each second)
    [ValidateRange(0.01,1)][double]$Report = 1,
    # load a quick save made by another build of the executable (XBE_SNAPSHOT_ANY_BUILD)
    [switch]$AnyBuild,
    # game sound on (XBE_MUTE is set otherwise)
    [switch]$Sound,
    # play cutscenes (skipped otherwise)
    [switch]$Cutscenes,
    # copy the release folder's profiles (hdd\E\UDATA) in: the game then boots to its menus, ignoring -Level
    [switch]$Profiles
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if (Get-Process MOHRS,MOHRSCoop -ErrorAction SilentlyContinue) { throw "Close the running game before this test." }
if ($Cap -lt 0 -or $Seconds -lt 2 -or $Level -notmatch '^\d+_\d+$') { throw "Invalid probe arguments" }
$tag = "{0}_{1}{8}_{2}_fov{3}_{4}x{5}_scale{6}_{7}" -f $Cap, $Unlocked.IsPresent, $Level, $Fov, $Width, $Height, $RenderPercent, (Get-Date -Format 'yyyyMMdd_HHmmss_fff'), $(if ($Fps30) { '_30' } else { '' })
# every run starts the game from the same folder, so Windows keeps one mixer entry (and its volume) for it; each run's
# logs and settings are copied to re\fps-probes\<tag> afterwards
$dir = Join-Path $root "re\fps-probes\run"
$results = Join-Path $root "re\fps-probes\$tag"
New-Item -ItemType Directory -Path $dir -Force | Out-Null
foreach ($old in 'logs','quicksave','hdd') {
    $path = Join-Path $dir $old
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
}
$images = if ($Coop) { 'MOHRSCoop.exe','MOHRSCoop.dll' } else { 'MOHRS.exe','MOHRS.dll' }
foreach ($name in $images) {
    Copy-Item -LiteralPath (Join-Path $root "out\build\$Build\$name") -Destination $dir
}
Get-ChildItem -LiteralPath $dir -Filter '*.dll' | ForEach-Object { $_.LastWriteTime = Get-Date }
if ($Profiles) {
    $udata = Join-Path $root 'out\build\release\hdd\E\UDATA'
    if (-not (Test-Path -LiteralPath $udata)) { throw "No profiles to copy: $udata" }
    New-Item -ItemType Directory -Path (Join-Path $dir 'hdd\E') -Force | Out-Null
    Copy-Item -LiteralPath $udata -Destination (Join-Path $dir 'hdd\E') -Recurse
}
if ($QuickSaveFrom) {
    $saves = Join-Path $QuickSaveFrom 'quicksave'
    if (-not (Test-Path -LiteralPath $saves)) { throw "No quicksave folder in $QuickSaveFrom" }
    Copy-Item -LiteralPath $saves -Destination $dir -Recurse
}
$flag = $Unlocked.IsPresent.ToString().ToLowerInvariant()
@"
display_mode = 'windowed'
width = $Width
height = $Height
render_percent = $RenderPercent
vsync = false
fps_cap = $Cap
fps60 = $(if ($Fps30) { 'false' } else { 'true' })
unlocked_fps = $flag
fps_counter = true
fov = $Fov
skip_intros = true
skip_cutscenes = $(if ($Cutscenes) { 'false' } else { 'true' })
"@ | Set-Content -LiteralPath (Join-Path $dir 'settings.toml') -Encoding ascii
$oldLevel = $env:MOHRS_START_LEVEL
$oldTiming = $env:MOHRS_TIMING
$oldPerf = $env:XBE_PERF
$oldFovTrace = $env:MOHRS_FOV_TRACE
$oldQuickSave = $env:MOHRS_QUICKSAVE_PROBE
$oldPad = $env:MOHRS_MENU_PROBE
$oldAnyBuild = $env:XBE_SNAPSHOT_ANY_BUILD
$oldMute = $env:XBE_MUTE
if ($Sound) { Remove-Item Env:\XBE_MUTE -ErrorAction SilentlyContinue } else { $env:XBE_MUTE = '1' }
$env:MOHRS_MENU_PROBE = $Pad
if ($AnyBuild) { $env:XBE_SNAPSHOT_ANY_BUILD = '1' } else { Remove-Item Env:\XBE_SNAPSHOT_ANY_BUILD -ErrorAction SilentlyContinue }
$env:MOHRS_QUICKSAVE_PROBE = $QuickSave
$env:MOHRS_START_LEVEL = $Level
$env:MOHRS_TIMING = $Report.ToString([Globalization.CultureInfo]::InvariantCulture)
$env:XBE_PERF = '1'
if ($NoPerf) { Remove-Item Env:\XBE_PERF -ErrorAction SilentlyContinue }
$env:MOHRS_FOV_TRACE = '1'
$exe = Join-Path $dir $(if ($Coop) { 'MOHRSCoop.exe' } else { 'MOHRS.exe' })
try {
    $p = Start-Process -FilePath $exe -WorkingDirectory $dir -WindowStyle $(if ($Visible) { 'Normal' } else { 'Hidden' }) -PassThru
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
    $env:MOHRS_FOV_TRACE = $oldFovTrace
    $env:MOHRS_QUICKSAVE_PROBE = $oldQuickSave
    $env:MOHRS_MENU_PROBE = $oldPad
    $env:XBE_SNAPSHOT_ANY_BUILD = $oldAnyBuild
    $env:XBE_MUTE = $oldMute
}
New-Item -ItemType Directory -Path $results -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $dir 'settings.toml') -Destination $results
if (Test-Path -LiteralPath (Join-Path $dir 'logs')) { Copy-Item -LiteralPath (Join-Path $dir 'logs') -Destination $results -Recurse }
$log = Join-Path $results 'logs\mohrs.log'
if (Test-Path $log) {
    Select-String -LiteralPath $log -Pattern 'timing:|fov:|perf:|snapshot:|quick save|\[error\]' | ForEach-Object Line
} else { throw "No game log: $results" }
"results: $results"
