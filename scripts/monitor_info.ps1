Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class VofaMonitors {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO {
        public int cbSize; public RECT rcMonitor; public RECT rcWork; public uint dwFlags;
    }
    public delegate bool MonitorEnumProc(IntPtr hMonitor, IntPtr hdc, ref RECT rect, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumDisplayMonitors(IntPtr hdc, IntPtr rect, MonitorEnumProc cb, IntPtr data);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool GetMonitorInfo(IntPtr monitor, ref MONITORINFO info);
    [DllImport("shcore.dll")] public static extern int GetDpiForMonitor(IntPtr monitor, int type, out uint dpiX, out uint dpiY);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);

    public static List<string> List() {
        SetProcessDpiAwarenessContext(new IntPtr(-4));
        var result = new List<string>();
        EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (IntPtr h, IntPtr hdc, ref RECT r, IntPtr d) => {
            var info = new MONITORINFO();
            info.cbSize = Marshal.SizeOf(info);
            if (GetMonitorInfo(h, ref info)) {
                uint dx = 0, dy = 0;
                GetDpiForMonitor(h, 0, out dx, out dy);
                result.Add(string.Format(
                    "monitor={0} rect={1},{2},{3},{4} work={5},{6},{7},{8} dpi={9}x{10} scale={11}%",
                    h, info.rcMonitor.Left, info.rcMonitor.Top, info.rcMonitor.Right, info.rcMonitor.Bottom,
                    info.rcWork.Left, info.rcWork.Top, info.rcWork.Right, info.rcWork.Bottom,
                    dx, dy, (int)(dx / 96.0 * 100)));
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
"@
[VofaMonitors]::List()
