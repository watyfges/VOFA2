param(
    [int]$X,
    [int]$Y,
    [int]$Width,
    [int]$Height,
    [switch]$Maximize
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class VofaMove {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT rect);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
}
"@

[VofaMove]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$process = Get-Process vofa2_app -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) { Write-Output "vofa2_app is not running"; exit 1 }
$hwnd = $process.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "no main window"; exit 1 }

[VofaMove]::ShowWindow($hwnd, 9) | Out-Null
[VofaMove]::SetForegroundWindow($hwnd) | Out-Null
if ($Maximize) {
    [VofaMove]::ShowWindow($hwnd, 3) | Out-Null
} else {
    # SWP_NOZORDER | SWP_NOACTIVATE, physical pixels.
    [VofaMove]::SetWindowPos($hwnd, [IntPtr]::Zero, $X, $Y, $Width, $Height, 0x0004 -bor 0x0010) | Out-Null
}
Start-Sleep -Milliseconds 600
$rect = New-Object VofaMove+RECT
[VofaMove]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
$dpi = [VofaMove]::GetDpiForWindow($hwnd)
Write-Output ("moved hwnd={0} to {1},{2} size {3}x{4} maximize={5}" -f $hwnd, $X, $Y, $Width, $Height, $Maximize)
Write-Output ("actual rect={0},{1},{2},{3} size={4}x{5} dpi={6} scale={7}%" -f $rect.Left, $rect.Top, $rect.Right, $rect.Bottom, ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top), $dpi, [int]($dpi / 96.0 * 100))
