param(
    [double]$LogicalX = 830,
    [double]$LogicalY = 400,
    [int]$Delta = 120,
    [int]$Count = 5
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class VofaWheel {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, int d, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    public struct RECT { public int Left, Top, Right, Bottom; }
    public struct POINT { public int X, Y; }
}
"@

[VofaWheel]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$process = Get-Process vofa2_app -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) {
    Write-Output "vofa2_app is not running"
    exit 1
}

$hwnd = $process.MainWindowHandle
[VofaWheel]::ShowWindow($hwnd, 9) | Out-Null
[VofaWheel]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 300

$pt = New-Object VofaWheel+POINT
$pt.X = 0
$pt.Y = 0
[VofaWheel]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
$dpr = [VofaWheel]::GetDpiForWindow($hwnd) / 96.0
$x = $pt.X + [int]($LogicalX * $dpr)
$y = $pt.Y + [int]($LogicalY * $dpr)
[VofaWheel]::SetCursorPos($x, $y) | Out-Null
Start-Sleep -Milliseconds 200

for ($i = 0; $i -lt $Count; $i++) {
    [VofaWheel]::mouse_event(0x0800, 0, 0, $Delta, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 120
}
Write-Output ("wheel at {0},{1} delta {2} x{3}" -f $x, $y, $Delta, $Count)
