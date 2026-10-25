# Runs the game for a while and reports how it ended: still running (then stopped), exited, or crashed, with the
# new crash dumps and the log. For boot bring-up.
#   powershell -NoProfile -File tools\run_test.ps1 [-Config release|debug] [-Seconds 20] [-Keep] [-Lines 80]
param(
    [string]$Config = "debug",
    [int]$Seconds = 20,
    [switch]$Keep,      # leave the game running
    [int]$Lines = 80    # log lines to print from the end
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$dir = Join-Path $root "out\build\$Config"
$exe = Join-Path $dir "MOHRS.exe"
if (-not (Test-Path $exe)) { throw "$exe is not built" }
$dumps = Join-Path $dir "CrashDumps"
$before = @(Get-ChildItem $dumps -ErrorAction SilentlyContinue | ForEach-Object Name)
$started = Get-Date

$p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru
$deadline = $started.AddSeconds($Seconds)
while (-not $p.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }

if ($p.HasExited) {
    "exited after {0:N1}s with code {1} (0x{2:X8})" -f ((Get-Date) - $started).TotalSeconds, $p.ExitCode, $p.ExitCode
} else {
    $info = Get-Process -Id $p.Id
    "running after ${Seconds}s: window '{0}', responding {1}, cpu {2:N1}s, {3:N0} MB" -f $info.MainWindowTitle, $info.Responding, $info.CPU, ($info.WorkingSet64 / 1MB)
    if (-not $Keep) { Stop-Process -Id $p.Id -Force; "stopped" }
}
$new = @(Get-ChildItem $dumps -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.Name })
foreach ($d in $new) { "crash dump: $($d.FullName)" }

# the newest log of this run (release: mohrs.log, debug: mohrs_NNN.log)
$log = Get-ChildItem (Join-Path $dir "logs") -Filter "mohrs*.log" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime | Select-Object -Last 1
if ($log) {
    "log: $($log.FullName)"
    Get-Content $log.FullName -Tail $Lines
}
