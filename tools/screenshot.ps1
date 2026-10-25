# Saves a PNG of a process's main window as it is on screen (brought to the front first). For boot bring-up.
#   powershell -NoProfile -File tools\screenshot.ps1 -Out re\shot.png [-Name MOHRS] [-Scale 0.5]
param([string]$Out = "re\shot.png", [string]$Name = "MOHRS", [double]$Scale = 0.5)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Wnd {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[Wnd]::SetProcessDPIAware() | Out-Null
$p = Get-Process -Name $Name -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { "no $Name window"; exit 1 }
[Wnd]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 400
$r = New-Object Wnd+RECT
[Wnd]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$sw = [int]($w * $Scale); $sh = [int]($h * $Scale)
$small = New-Object System.Drawing.Bitmap $bmp, $sw, $sh
$path = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Out))
$small.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
"saved $path ($w x $h window, ${sw} x ${sh} image)"
