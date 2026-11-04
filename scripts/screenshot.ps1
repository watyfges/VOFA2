param(
    [int]$AppPid = 0,
    [string]$Out = "D:\coding\vofa2\screenshot.png"
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class VofaWin32 {
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")]
    public static extern bool BringWindowToTop(IntPtr hWnd);
}
"@

if ($AppPid -eq 0) {
    $process = Get-Process vofa2_app -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($process) {
        $AppPid = $process.Id
    }
}

if ($AppPid -gt 0) {
    $process = Get-Process -Id $AppPid -ErrorAction SilentlyContinue
    if ($process) {
        $hwnd = $process.MainWindowHandle
        Write-Output ("PID {0} title '{1}' hwnd {2}" -f $AppPid, $process.MainWindowTitle, $hwnd)
        [VofaWin32]::ShowWindow($hwnd, 9) | Out-Null
        [VofaWin32]::BringWindowToTop($hwnd) | Out-Null
        [VofaWin32]::SetForegroundWindow($hwnd) | Out-Null
        Start-Sleep -Milliseconds 1000
    }
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$screen = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bitmap = New-Object System.Drawing.Bitmap($screen.Width, $screen.Height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($screen.Location, [System.Drawing.Point]::Empty, $bitmap.Size)
$bitmap.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
Write-Output $Out
