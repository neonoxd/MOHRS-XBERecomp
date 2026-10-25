# Starts the game straight into a level, with the intro and cutscene movies skipped. For testing a level.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\start_level.ps1 -Level 1_2 [-Config debug|release]
# MOHRS_START_LEVEL only acts on a fresh profile, so this uses the debug build (whose saves are throwaway) and
# empties its hdd folder first. The release build keeps its saves: -Config release starts there without wiping
# them, and then MOHRS_START_LEVEL is ignored once a profile exists.
param(
    [Parameter(Mandatory = $true)][string]$Level,
    [string]$Config = "debug"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$dir = Join-Path $root "out\build\$Config"
$exe = Join-Path $dir "MOHRS.exe"
if (-not (Test-Path $exe)) { throw "$exe is not built" }
if ($Level -notmatch '^\d+_\d+$') { throw "level is mission_level, e.g. 1_2" }

Get-Process MOHRS -ErrorAction SilentlyContinue | Stop-Process -Force
if ($Config -eq "debug") { Remove-Item -Recurse -Force (Join-Path $dir "hdd") -ErrorAction SilentlyContinue }

# both movie skips on, every other setting kept (one per line: an empty array out of an if is $null in PowerShell)
$settings = Join-Path $dir "settings.toml"
# first: the file ends in tables ([keys]), and a line after a table header belongs to that table
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("skip_intros = true")
$lines.Add("skip_cutscenes = true")
if (Test-Path $settings) { Get-Content $settings | Where-Object { $_ -notmatch "^skip_(intros|cutscenes)" } | ForEach-Object { $lines.Add($_) } }
$lines | Set-Content -Encoding ascii $settings

$env:MOHRS_START_LEVEL = $Level
Start-Process -FilePath $exe -WorkingDirectory $dir | Out-Null
"started $Level ($Config)"
