param(
    [string]$Button = "middle",
    [double]$X1 = 600,
    [double]$Y1 = 300,
    [double]$X2 = 1000,
    [double]$Y2 = 500
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class VofaDrag {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    public struct POINT { public int X, Y; }
}
"@

[VofaDrag]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$process = Get-Process vofa2_app -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) {
    Write-Output "vofa2_app is not running"
    exit 1
}
$hwnd = $process.MainWindowHandle
[VofaDrag]::ShowWindow($hwnd, 9) | Out-Null
[VofaDrag]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 300

$pt = New-Object VofaDrag+POINT
$pt.X = 0
$pt.Y = 0
[VofaDrag]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
$dpr = [VofaDrag]::GetDpiForWindow($hwnd) / 96.0
$startX = $pt.X + [int]($X1 * $dpr)
$startY = $pt.Y + [int]($Y1 * $dpr)
$endX = $pt.X + [int]($X2 * $dpr)
$endY = $pt.Y + [int]($Y2 * $dpr)

$down = 0x0004
$up = 0x0008
if ($Button -eq "left") { $down = 0x0002; $up = 0x0004 }
elseif ($Button -eq "middle") { $down = 0x0020; $up = 0x0040 }
elseif ($Button -eq "right") { $down = 0x0008; $up = 0x0010 }

[VofaDrag]::SetCursorPos($startX, $startY) | Out-Null
Start-Sleep -Milliseconds 150
[VofaDrag]::mouse_event($down, 0, 0, 0, [UIntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 120

for ($i = 1; $i -le 12; $i++) {
    $x = [int]($startX + ($endX - $startX) * $i / 12)
    $y = [int]($startY + ($endY - $startY) * $i / 12)
    [VofaDrag]::SetCursorPos($x, $y) | Out-Null
    Start-Sleep -Milliseconds 30
}
[VofaDrag]::mouse_event($up, 0, 0, 0, [UIntPtr]::Zero) | Out-Null
Write-Output ("$Button drag {0},{1} -> {2},{3}" -f $startX, $startY, $endX, $endY)
