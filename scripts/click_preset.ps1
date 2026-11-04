param(
    [double]$LogicalX = 302,
    [double]$LogicalY = 28,
    [int]$Downs = 1,
    [switch]$NoKeys
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class VofaMouse {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
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

[VofaMouse]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$process = Get-Process vofa2_app -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) {
    Write-Output "vofa2_app is not running"
    exit 1
}

$hwnd = $process.MainWindowHandle
[VofaMouse]::ShowWindow($hwnd, 9) | Out-Null
[VofaMouse]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 400

$rect = New-Object VofaMouse+RECT
[VofaMouse]::GetClientRect($hwnd, [ref]$rect) | Out-Null
$pt = New-Object VofaMouse+POINT
$pt.X = 0
$pt.Y = 0
[VofaMouse]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
$dpr = [VofaMouse]::GetDpiForWindow($hwnd) / 96.0

$clientW = $rect.Right - $rect.Left
$clientH = $rect.Bottom - $rect.Top
$x = $pt.X + [int]($LogicalX * $dpr)
$y = $pt.Y + [int]($LogicalY * $dpr)

Write-Output ("client {0}x{1} dpr {2} click {3},{4}" -f $clientW, $clientH, $dpr, $x, $y)

[VofaMouse]::SetCursorPos($x, $y) | Out-Null
Start-Sleep -Milliseconds 150
[VofaMouse]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 80
[VofaMouse]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 600

if (-not $NoKeys) {
    Add-Type -AssemblyName System.Windows.Forms
    for ($i = 0; $i -lt $Downs; $i++) {
        [System.Windows.Forms.SendKeys]::SendWait("{DOWN}")
        Start-Sleep -Milliseconds 120
    }
    [System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
    Write-Output "preset selection sent"
} else {
    Write-Output "click sent (no keys)"
}
