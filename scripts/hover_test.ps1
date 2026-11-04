param(
    [double]$LogicalX = 800,
    [double]$LogicalY = 400
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class VofaHover {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    public struct POINT { public int X, Y; }
}
"@
[VofaHover]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$process = Get-Process vofa2_app -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) { Write-Output "vofa2_app is not running"; exit 1 }
$hwnd = $process.MainWindowHandle
[VofaHover]::ShowWindow($hwnd, 9) | Out-Null
[VofaHover]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 300
$pt = New-Object VofaHover+POINT
$pt.X = 0; $pt.Y = 0
[VofaHover]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
$dpr = [VofaHover]::GetDpiForWindow($hwnd) / 96.0
$x = $pt.X + [int]($LogicalX * $dpr)
$y = $pt.Y + [int]($LogicalY * $dpr)
[VofaHover]::SetCursorPos($x, $y) | Out-Null
Start-Sleep -Milliseconds 400
Write-Output ("hover at {0},{1}" -f $x, $y)
