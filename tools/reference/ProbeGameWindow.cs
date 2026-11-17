// Bounded reference observation of one copied game process in the SSH session.
// Captures only windows belonging to that exact PID; never the user's desktop.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

internal static class ProbeGameWindow
{
    private delegate bool EnumProc(IntPtr window, IntPtr extra);
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc proc, IntPtr extra);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr window, uint msg, IntPtr w, IntPtr l);

    private static int Main(string[] args)
    {
        if (args.Length != 2) return 2;
        string exe = Path.GetFullPath(args[0]);
        string output = Path.GetFullPath(args[1]);
        string root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "ElonaPlusNative-2.335", "reference-game") + Path.DirectorySeparatorChar;
        if (!exe.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new ArgumentException("Only isolated reference copies can be launched");
        Directory.CreateDirectory(output);
        using (Process process = Process.Start(new ProcessStartInfo(exe) { WorkingDirectory=Path.GetDirectoryName(exe), UseShellExecute=false }))
        {
            var windows = new List<IntPtr>();
            try
            {
                Console.WriteLine("pid=" + process.Id + " session=" + process.SessionId);
                Thread.Sleep(10000);
                if (process.HasExited) { Console.WriteLine("early_exit=" + process.ExitCode); return 1; }
                EnumWindows(delegate(IntPtr window, IntPtr unused) {
                    uint pid; GetWindowThreadProcessId(window, out pid);
                    if (pid == process.Id && IsWindowVisible(window)) windows.Add(window);
                    return true;
                }, IntPtr.Zero);
                Console.WriteLine("visible_windows=" + windows.Count);
                for (int index=0; index<windows.Count; ++index)
                {
                    IntPtr window=windows[index]; Rect rect;
                    var title=new StringBuilder(512); GetWindowText(window,title,title.Capacity);
                    Console.WriteLine("window="+index+" title="+title);
                    if (!GetWindowRect(window,out rect)) continue;
                    int width=rect.Right-rect.Left, height=rect.Bottom-rect.Top;
                    if (width<1 || height<1 || width>4096 || height>4096) continue;
                    using (var bitmap=new Bitmap(width,height))
                    using (var graphics=Graphics.FromImage(bitmap))
                    {
                        IntPtr dc=graphics.GetHdc(); bool captured;
                        try { captured=PrintWindow(window,dc,2); }
                        finally { graphics.ReleaseHdc(dc); }
                        Console.WriteLine("capture="+index+" success="+captured+" size="+width+"x"+height);
                        bitmap.Save(Path.Combine(output,"window-"+index+".png"),ImageFormat.Png);
                    }
                }
                return windows.Count > 0 ? 0 : 1;
            }
            finally
            {
                foreach (var window in windows) PostMessage(window,0x0010,IntPtr.Zero,IntPtr.Zero);
                if (!process.HasExited && !process.WaitForExit(2000)) { process.Kill(); process.WaitForExit(); Console.WriteLine("terminated_owned_process=true"); }
            }
        }
    }
}
