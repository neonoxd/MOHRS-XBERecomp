# Starts each campaign level on a fresh profile (debug build, MOHRS_START_LEVEL) with the movies skipped, takes
# screenshots and collects the log's warnings and errors, into re\tour\<level>\. For checking rendering level by
# level (M5). A level is in play about 2 s after start.
# Wipes out\build\debug\hdd before every level: the debug build's saves are throwaway. Turns skip_intros and
# skip_cutscenes on in out\build\debug\settings.toml. The game window comes to the front for each screenshot.
#   powershell -NoProfile -File tools\level_tour.ps1 [-Levels 1_1,1_2] [-Shots 12,25]
param(
    [string[]]$Levels = @("1_1", "1_11", "1_2", "1_3", "1_4", "1_5", "1_7", "1_8", "1_9"),
    [int[]]$Shots = @(12, 25)
)
$ErrorActionPreference = "Stop"
# -File passes "1_11,1_2" as one string
$Levels = @($Levels | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$Shots = @($Shots | ForEach-Object { "$_" -split "," } | Where-Object { $_ } | ForEach-Object { [int]$_ })
$root = Split-Path $PSScriptRoot
$dir = Join-Path $root "out\build\debug"
$out = Join-Path $root "re\tour"
New-Item -ItemType Directory -Force $out | Out-Null
$settings = Join-Path $dir "settings.toml"
# one setting per line (an empty array out of an if expression is $null, and $null + "a" joins strings)
# first: the file ends in tables ([keys]), and a line after a table header belongs to that table
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("skip_intros = true")
$lines.Add("skip_cutscenes = true")
if (Test-Path $settings) { Get-Content $settings | Where-Object { $_ -notmatch "^skip_(intros|cutscenes)" } | ForEach-Object { $lines.Add($_) } }
$lines | Set-Content -Encoding ascii $settings
$summary = @()
foreach ($lvl in $Levels) {
    $ld = Join-Path $out $lvl
    New-Item -ItemType Directory -Force $ld | Out-Null
    Get-Process MOHRS -ErrorAction SilentlyContinue | Stop-Process -Force
    Remove-Item -Recurse -Force (Join-Path $dir "hdd") -ErrorAction SilentlyContinue
    $env:MOHRS_START_LEVEL = $lvl
    $before = @(Get-ChildItem (Join-Path $dir "CrashDumps") -ErrorAction SilentlyContinue | ForEach-Object Name)
    $started = Get-Date
    Start-Process -FilePath (Join-Path $dir "MOHRS.exe") -WorkingDirectory $dir | Out-Null
    $state = "ran"
    try {
        foreach ($t in $Shots) {
            while (((Get-Date) - $started).TotalSeconds -lt $t) { Start-Sleep -Milliseconds 500 }
            if (-not (Get-Process MOHRS -ErrorAction SilentlyContinue)) { $state = "exited before ${t}s"; break }
            try {
                & (Join-Path $PSScriptRoot "screenshot.ps1") -Out (Join-Path $ld "shot_${t}s.png") -Scale 0.4 | Out-Null
            } catch {
                $state += ", screenshot at ${t}s failed: $($_.Exception.Message)"
            }
        }
    } finally {
        Get-Process MOHRS -ErrorAction SilentlyContinue | Stop-Process -Force
    }
    $env:MOHRS_START_LEVEL = $null
    $dumps = @(Get-ChildItem (Join-Path $dir "CrashDumps") -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.Name })
    if ($dumps) { $state += ", crash dump $($dumps[0].Name)" }
    $log = Get-ChildItem (Join-Path $dir "logs") -Filter "mohrs*.log" | Sort-Object LastWriteTime | Select-Object -Last 1
    Copy-Item $log.FullName (Join-Path $ld "mohrs.log")
    # warnings and errors, repeats folded, without the known harmless ones
    $issues = Get-Content $log.FullName | Where-Object { $_ -match "\[(warn|error)\]" -and $_ -notmatch "wbinvd" } |
        ForEach-Object { ($_ -replace "^\[\s*[\d.]+\] \[\w+\] \[t[0-9a-f]+\] ", "") -replace "[0-9A-F]{8}", "XXXXXXXX" } |
        Group-Object | Sort-Object Count -Descending | ForEach-Object { "{0,5}x {1}" -f $_.Count, $_.Name }
    $issues | Set-Content (Join-Path $ld "issues.txt")
    $summary += "{0,-5} {1}; {2} distinct warnings/errors" -f $lvl, $state, @($issues).Count
}
$summary | Tee-Object (Join-Path $out "summary.txt")
