// Session-1 probe for the native elona_gui_host window (x64). Separate from
// ProbeGameWindow.cs / GuiFixtureProbe.cs. Starts each planned command itself
// and only inspects/captures top-level windows owned by that exact PID. Never
// captures the desktop. Every started process exits on its own (deterministic
// host bounds) or is killed by PID after its timeout.
//
// usage: WindowsGuiProbe.exe PLAN_FILE RESULT_DIR
// plan line: name workdir capture_delay_ms|@ready_file timeout_ms exe arg1 ...
// A private ready file avoids assuming cold startup reaches a screen in N ms.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

internal static class WindowsGuiProbe
{
    private delegate bool EnumProc(IntPtr window, IntPtr extra);
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc proc, IntPtr extra);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassNameW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr window, IntPtr dc);
    [DllImport("gdi32.dll")] private static extern bool BitBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, uint rop);
    [DllImport("user32.dll")] private static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr window);

    private static StreamWriter log;
    private static void Log(string s) { log.WriteLine(s); log.Flush(); }

    private static List<IntPtr> Owned(int pid)
    {
        var list = new List<IntPtr>();
        EnumWindows(delegate (IntPtr w, IntPtr x) { uint p; GetWindowThreadProcessId(w, out p); if (p == pid && IsWindowVisible(w)) list.Add(w); return true; }, IntPtr.Zero);
        return list;
    }

    private static string Text(IntPtr w, bool cls)
    {
        var sb = new StringBuilder(512);
        if (cls) GetClassNameW(w, sb, sb.Capacity); else GetWindowTextW(w, sb, sb.Capacity);
        return sb.ToString();
    }

    private static void Capture(IntPtr window, string prefix, string key)
    {
        Rect c; GetClientRect(window, out c);
        int width = c.Right - c.Left, height = c.Bottom - c.Top;
        Rect o; GetWindowRect(window, out o);
        Log(key + ".class=" + Text(window, true) + " title=" + Text(window, false));
        Log(key + ".window_rect=" + o.Left + "," + o.Top + "," + o.Right + "," + o.Bottom + " client=" + width + "x" + height);
        try { Log(key + ".dpi=" + GetDpiForWindow(window)); } catch (EntryPointNotFoundException) { }
        if (width < 1 || height < 1 || width > 8192 || height > 8192) { Log(key + ".capture=skipped"); return; }
        foreach (string method in new[] { "pw", "dc" })
        {
            using (var bmp = new Bitmap(width, height, PixelFormat.Format32bppArgb))
            {
                bool ok;
                using (var g = Graphics.FromImage(bmp))
                {
                    IntPtr dc = g.GetHdc();
                    try
                    {
                        if (method == "pw") ok = PrintWindow(window, dc, 1 | 2);   // PW_CLIENTONLY | PW_RENDERFULLCONTENT
                        else { IntPtr s = GetDC(window); try { ok = BitBlt(dc, 0, 0, width, height, s, 0, 0, 0x00CC0020); } finally { ReleaseDC(window, s); } }
                    }
                    finally { g.ReleaseHdc(dc); }
                }
                Log(key + "." + method + ".api_success=" + ok);
                bmp.Save(prefix + "-" + method + ".png", ImageFormat.Png);
            }
        }
    }

    private static string Quote(string a) { return "\"" + a.Replace("\"", "\\\"") + "\""; }

    private static void RunCase(string[] f, string results)
    {
        string name = f[0], workdir = f[1];
        string readyFile = f[2].StartsWith("@", StringComparison.Ordinal) ? f[2].Substring(1) : null;
        int delay = readyFile == null ? int.Parse(f[2]) : 0, timeout = int.Parse(f[3]);
        var args = new List<string>();
        for (int i = 5; i < f.Length; ++i) args.Add(Quote(f[i]));
        var psi = new ProcessStartInfo(f[4], string.Join(" ", args)) { WorkingDirectory = workdir, UseShellExecute = false, RedirectStandardError = true, RedirectStandardOutput = true };
        Log("case=" + name);
        var sw = Stopwatch.StartNew();
        using (var p = Process.Start(psi))
        {
            var err = p.StandardError.ReadToEndAsync();
            var outp = p.StandardOutput.ReadToEndAsync();
            Log(name + ".pid=" + p.Id + " session=" + p.SessionId);
            try
            {
                List<IntPtr> wins = new List<IntPtr>();
                while (!p.HasExited && sw.ElapsedMilliseconds < timeout - 1000 &&
                       (readyFile == null ? sw.ElapsedMilliseconds < delay : !File.Exists(readyFile))) Thread.Sleep(100);
                bool ready = readyFile == null || File.Exists(readyFile);
                if (readyFile != null) Log(name + ".ready_file_observed=" + ready);
                if (!p.HasExited && ready)
                {
                    wins = Owned(p.Id);
                    Log(name + ".visible_windows=" + wins.Count + " at_ms=" + sw.ElapsedMilliseconds);
                    for (int i = 0; i < wins.Count; ++i) Capture(wins[i], Path.Combine(results, name + "-window" + i), name + ".window" + i);
                }
                else if (p.HasExited) Log(name + ".exited_before_capture_ms=" + sw.ElapsedMilliseconds);
                else Log(name + ".capture_ready_timeout_ms=" + sw.ElapsedMilliseconds);
                if (!p.WaitForExit(Math.Max(1000, timeout - (int)sw.ElapsedMilliseconds))) { p.Kill(); p.WaitForExit(); Log(name + ".terminated_owned_process=true"); }
                Log(name + ".exit_code=" + p.ExitCode + " elapsed_ms=" + sw.ElapsedMilliseconds);
            }
            finally
            {
                if (!p.HasExited) { p.Kill(); p.WaitForExit(); Log(name + ".terminated_owned_process=true"); }
                File.WriteAllText(Path.Combine(results, name + ".stderr.log"), err.Result);
                File.WriteAllText(Path.Combine(results, name + ".stdout.log"), outp.Result);
            }
        }
    }

    private static int Main(string[] a)
    {
        if (a.Length != 2) return 2;
        Directory.CreateDirectory(a[1]);
        bool aware = false;
        try { aware = SetProcessDPIAware(); } catch (EntryPointNotFoundException) { }
        using (log = new StreamWriter(Path.Combine(a[1], "probe.log"), false, new UTF8Encoding(false)))
        {
            Log("probe_pid=" + Process.GetCurrentProcess().Id + " session=" + Process.GetCurrentProcess().SessionId + " ptr_bits=" + IntPtr.Size * 8 + " dpi_aware=" + aware);
            foreach (string line in File.ReadAllLines(a[0], Encoding.UTF8))
            {
                if (line.Trim().Length == 0) continue;
                string[] f = line.Split('\t');
                try { RunCase(f, a[1]); } catch (Exception e) { Log(f[0] + ".probe_error=" + e.GetType().Name + ": " + e.Message.Replace('\n', ' ')); }
            }
            Log("done=true");
        }
        File.WriteAllText(Path.Combine(a[1], "done.marker"), "done");
        return 0;
    }
}
