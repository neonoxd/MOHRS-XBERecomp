# A crash dump as a report: the host call stack with the game functions and lines, the crashed guest thread's
# registers and stack, and any guest memory asked for. Needs cdb (Windows SDK Debugging Tools).
#   powershell -NoProfile -File tools/crash_report.ps1 out\build\release\CrashDumps\MOHRS.<time>.dmp
#   ... -Guest 83EA2A10:40,00399758:4     guest address:bytes (hex), read from the guest memory in the dump
# The symbols are taken from the folder holding CrashDumps (the executable's <exe>.pdb and the game code's
# <exe>_code.pdb), or -Symbols. A dump from before guest memory was written still gives the host stack.
param(
    [Parameter(Mandatory = $true)][string]$Dump,
    [string]$Symbols = "",
    [string]$Guest = ""
)
$ErrorActionPreference = "Stop"
$cdb = @("${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\cdb.exe", "$env:ProgramFiles\Windows Kits\10\Debuggers\x64\cdb.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $cdb) { throw "cdb.exe not found: install the Debugging Tools for Windows (Windows SDK)" }
$Dump = (Resolve-Path $Dump).Path
if (-not $Symbols) { $Symbols = Split-Path (Split-Path $Dump) }

# dd lines: an address and four dwords
$words = '^[0-9a-f`]{17}( +[0-9a-f]{8}){1,4}\s*$'

function Run([string]$commands) {
    $out = & $cdb -z $Dump -y $Symbols -lines -c "$commands; q" 2>&1 | ForEach-Object { "$_" }
    # cdb's banner and module loading noise end at the first command's output
    return $out
}

$first = Run ".ecxr; kn 40"
$comment = $first | Select-String "^Comment: '(.*)'" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -First 1
"== dump: $Dump"
if ($comment) { "== $comment" } else { "== no guest description (a dump from before guest memory was written)" }
"== host call stack (game code as __imp__sub_<guest address>)"
$first | Where-Object { $_ -match '^\s*[0-9a-f]{2} [0-9a-f`]+ [0-9a-f`]+ ' -or $_ -match '^\s*#' } | ForEach-Object { "  $_" }

if ($comment -match 'guest base ([0-9A-Fa-f]+); guest thread ([0-9A-Fa-f]+), X86Context at ([0-9A-Fa-f]+)') {
    $base = [Convert]::ToUInt64($Matches[1], 16)
    $thread = $Matches[2]
    $ctx = [Convert]::ToUInt64($Matches[3], 16)
    $regs = Run ("dd {0:x} L8" -f $ctx) | Where-Object { $_ -match $words }
    $values = ($regs -join ' ') -split '\s+' | Where-Object { $_ -match '^[0-9a-f]{8}$' }
    $names = 'eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi'
    if ($values.Count -ge 8) {
        "== guest thread $thread registers"
        "  " + ((0..7 | ForEach-Object { "$($names[$_]) $($values[$_])" }) -join '  ')
        $esp = [Convert]::ToUInt32($values[4], 16)
        "== guest stack from esp (values that look like code addresses are game functions' return addresses)"
        Run ("dd {0:x} L40" -f ($base + $esp)) | Where-Object { $_ -match $words } | ForEach-Object {
            $parts = $_ -split '\s+'
            $at = [Convert]::ToUInt64(($parts[0] -replace '`', ''), 16) - $base
            "  {0:X8}: {1}" -f $at, ($parts[1..4] -join ' ')
        }
    }
    foreach ($item in ($Guest -split ',' | Where-Object { $_ })) {
        $addr, $bytes = $item -split ':'
        $count = [Math]::Max(1, [Convert]::ToUInt32($(if ($bytes) { $bytes } else { '40' }), 16) / 4)
        "== guest $addr"
        Run ("dd {0:x} L{1:x}" -f ($base + [Convert]::ToUInt32($addr, 16)), $count) | Where-Object { $_ -match $words } | ForEach-Object {
            $parts = $_ -split '\s+'
            $at = [Convert]::ToUInt64(($parts[0] -replace '`', ''), 16) - $base
            "  {0:X8}: {1}" -f $at, ($parts[1..4] -join ' ')
        }
    }
}
