# A short menu test in its own run folder: MOHRS_MENU_PROBE presses controller buttons inside the game (no keys or
# mouse of the PC's), "capture" saves frames under the folder's captures\. Leaves the player's settings and saves alone.
# Ask before launching while the user is using the PC (HANDOVER.md).
# powershell -NoProfile -File tools/menu_probe.ps1 -Level 1_1 -Script "8:start,10:right,11.5:right,13:capture" -Seconds 16
param(
    [string]$Level = "1_1",
    [Parameter(Mandatory)][string]$Script,
    [int]$Seconds = 20,
    [switch]$Coop
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
if (Get-Process MOHRS,MOHRSCoop -ErrorAction SilentlyContinue) { throw "Close the running game before this test." }
if ($Seconds -lt 2 -or ($Level -and $Level -notmatch '^\d+_\d+$')) { throw "Invalid probe arguments" }
$tag = "menu_{0}_{1}" -f $Level, (Get-Date -Format 'yyyyMMdd_HHmmss_fff')
$dir = Join-Path $root "re\menu-probes\$tag"
New-Item -ItemType Directory -Path $dir -Force | Out-Null
foreach ($name in 'MOHRS.exe','MOHRS.dll','MOHRSCoop.exe','MOHRSCoop.dll') {
    Copy-Item -LiteralPath (Join-Path $root "out\build\release\$name") -Destination $dir
}
Get-ChildItem -LiteralPath $dir -Filter '*.dll' | ForEach-Object { $_.LastWriteTime = Get-Date }
@"
display_mode = 'windowed'
width = 1280
height = 720
vsync = false
fps_cap = 60
fps60 = true
unlocked_fps = false
skip_intros = true
skip_cutscenes = true
"@ | Set-Content -LiteralPath (Join-Path $dir 'settings.toml') -Encoding ascii
$saved = @{}
foreach ($name in 'MOHRS_START_LEVEL','MOHRS_MENU_PROBE','XBE_CAPTURE_DIR') { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
$env:MOHRS_START_LEVEL = $Level
$env:MOHRS_MENU_PROBE = $Script
$env:XBE_CAPTURE_DIR = 'captures'
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
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
$log = Join-Path $dir 'logs\mohrs.log'
if (Test-Path $log) {
    Select-String -LiteralPath $log -Pattern 'MOHRS_MENU_PROBE|PC options|\[error\]' | ForEach-Object Line
} else { throw "No game log: $dir" }
Get-ChildItem -LiteralPath (Join-Path $dir 'captures') -Recurse -Filter 'final*' -ErrorAction SilentlyContinue | ForEach-Object FullName
"results: $dir"
