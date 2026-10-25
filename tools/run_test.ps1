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
# the game relaunches itself into a new process when it loads an xbe (front end <-> missions): follow any MOHRS
$seen = @{ $p.Id = $true }
$last = $p
while ((Get-Date) -lt $deadline) {
    $live = @(Get-Process -Name MOHRS -ErrorAction SilentlyContinue)
    foreach ($q in $live) { if (-not $seen.ContainsKey($q.Id)) { $seen[$q.Id] = $true; "relaunched as pid $($q.Id) after {0:N1}s" -f ((Get-Date) - $started).TotalSeconds } }
    if ($live) { $last = $live[-1] } else { break }
    Start-Sleep -Milliseconds 250
}

$live = @(Get-Process -Name MOHRS -ErrorAction SilentlyContinue)
if (-not $live) {
    $code = if ($last.HasExited) { $last.ExitCode } else { "?" }
    "exited after {0:N1}s, last process exit code {1}" -f ((Get-Date) - $started).TotalSeconds, $code
} else {
    $info = $live[-1]
    "running after ${Seconds}s: window '{0}', responding {1}, cpu {2:N1}s, {3:N0} MB" -f $info.MainWindowTitle, $info.Responding, $info.CPU, ($info.WorkingSet64 / 1MB)
    if (-not $Keep) { $live | Stop-Process -Force; "stopped" }
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
