# Prints the text of every window a process owns, child controls included, such as an "Assertion Failed" dialog,
# which names the file, line and expression. For boot bring-up.
#   powershell -NoProfile -File tools\dialog_text.ps1 -Name MOHRS
param([string]$Name = "MOHRS")
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class Win {
    delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc f, IntPtr p);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, StringBuilder l);
    static string Text(IntPtr h) {
        var s = new StringBuilder(8192);
        SendMessage(h, 0x000D, (IntPtr)8192, s);  // WM_GETTEXT reads controls of other processes too
        if (s.Length == 0) GetWindowText(h, s, 8192);
        return s.ToString();
    }
    static string Class(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
    public static List<string> Dump(uint want) {
        var r = new List<string>();
        EnumWindows((h, p) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid != want) return true;
            r.Add("== [" + Class(h) + "] " + Text(h));
            EnumChildWindows(h, (c, q) => { string t = Text(c); if (t.Length > 0) r.Add("  [" + Class(c) + "] " + t); return true; }, IntPtr.Zero);
            return true;
        }, IntPtr.Zero);
        return r;
    }
}
"@
$procs = @(Get-Process -Name $Name -ErrorAction SilentlyContinue)
if (-not $procs) { "no $Name process"; exit 1 }
foreach ($p in $procs) { [Win]::Dump([uint32]$p.Id) }
