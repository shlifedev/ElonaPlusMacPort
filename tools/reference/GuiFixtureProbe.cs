// GUI fixture probe for the official HSP 3.4 x86 runtime (hsp3.exe).
// Separate from ProbeGameWindow.cs. Runs a plan of fixture cases, each in its own
// working directory. Only windows owned by the exact PID this helper started are
// inspected or captured (never the desktop or other processes). Every started
// process is closed, and killed by PID if it does not exit.
//
// usage: GuiFixtureProbe.exe PLAN_FILE RESULT_DIR
// plan line (tab separated): name  mode(exit|capture|input)  runtime  ax  workdir  timeout_ms
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

internal static class GuiFixtureProbe
{
    private delegate bool EnumProc(IntPtr window, IntPtr extra);
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct GuiThreadInfo
    {
        public int cbSize, flags; public IntPtr hwndActive, hwndFocus, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret; public Rect rcCaret;
    }
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc proc, IntPtr extra);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr extra);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassNameW(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] private static extern bool PostMessageW(IntPtr window, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] private static extern IntPtr SendMessageW(IntPtr window, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll", EntryPoint = "SendMessageW")] private static extern IntPtr SendMessageWOut(IntPtr window, uint msg, out int w, out int l);
    [DllImport("user32.dll", EntryPoint = "SendMessageW", CharSet = CharSet.Unicode)] private static extern IntPtr SendMessageWText(IntPtr window, uint msg, IntPtr w, StringBuilder l);
    [DllImport("user32.dll", EntryPoint = "SendMessageA")] private static extern IntPtr SendMessageABytes(IntPtr window, uint msg, IntPtr w, byte[] l);
    [DllImport("user32.dll")] private static extern int GetWindowLongW(IntPtr window, int index);
    [DllImport("user32.dll")] private static extern bool GetGUIThreadInfo(uint thread, ref GuiThreadInfo info);
    [DllImport("user32.dll")] private static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] private static extern bool IsWindowUnicode(IntPtr window);
    [DllImport("kernel32.dll")] private static extern uint GetACP();
    [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr window, IntPtr dc);
    [DllImport("gdi32.dll")] private static extern bool BitBlt(IntPtr dst, int x, int y, int w, int h, IntPtr src, int sx, int sy, uint rop);

    private const uint WM_CLOSE = 0x0010, WM_GETTEXT = 0x000D, WM_GETTEXTLENGTH = 0x000E, WM_SETTEXT = 0x000C;
    private const uint EM_GETSEL = 0x00B0, EM_SETSEL = 0x00B1, EM_GETLIMITTEXT = 0x00D5;
    private const uint PW_CLIENTONLY = 1, PW_RENDERFULLCONTENT = 2;

    private static StreamWriter log;
    private static void Log(string line) { log.WriteLine(line); log.Flush(); }
    private static string Hex(byte[] bytes, int count) { return BitConverter.ToString(bytes, 0, count).Replace("-", "").ToLowerInvariant(); }

    private static List<IntPtr> OwnedTopWindows(int pid)
    {
        var list = new List<IntPtr>();
        EnumWindows(delegate (IntPtr w, IntPtr x) { uint p; GetWindowThreadProcessId(w, out p); if (p == pid && IsWindowVisible(w)) list.Add(w); return true; }, IntPtr.Zero);
        return list;
    }

    private static string ClassOf(IntPtr w) { var sb = new StringBuilder(256); GetClassNameW(w, sb, sb.Capacity); return sb.ToString(); }
    private static string TitleOf(IntPtr w) { var sb = new StringBuilder(512); GetWindowTextW(w, sb, sb.Capacity); return sb.ToString(); }

    // Captures client area (PrintWindow) and writes PNG plus raw top-down BGRA.
    private static void Capture(IntPtr window, string prefix)
    {
        string key = Path.GetFileName(prefix);
        Rect client; GetClientRect(window, out client);
        int width = client.Right - client.Left, height = client.Bottom - client.Top;
        Rect outer; GetWindowRect(window, out outer);
        Log(key + ".window_rect=" + outer.Left + "," + outer.Top + "," + outer.Right + "," + outer.Bottom);
        Log(key + ".client_size=" + width + "x" + height);
        try { Log(key + ".dpi=" + GetDpiForWindow(window)); } catch (EntryPointNotFoundException) { Log(key + ".dpi=unavailable"); }
        if (width < 1 || height < 1 || width > 4096 || height > 4096) { Log(key + ".capture=skipped_bad_size"); return; }
        // Method variants: PrintWindow can return TRUE yet deliver a blank bitmap,
        // so each method is saved separately and verified by pixel content.
        // "pw" = PW_CLIENTONLY|PW_RENDERFULLCONTENT, "pwlegacy" = PW_CLIENTONLY,
        // "dc" = BitBlt from this window's own client DC (GetDC(hwnd)), which
        // reads only this window's client area on screen.
        foreach (string method in new[] { "pw", "pwlegacy", "dc" })
        {
            using (var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb))
            {
                bool ok;
                using (var g = Graphics.FromImage(bitmap))
                {
                    IntPtr dc = g.GetHdc();
                    try
                    {
                        if (method == "pw") ok = PrintWindow(window, dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
                        else if (method == "pwlegacy") ok = PrintWindow(window, dc, PW_CLIENTONLY);
                        else
                        {
                            IntPtr source = GetDC(window);
                            try { ok = BitBlt(dc, 0, 0, width, height, source, 0, 0, 0x00CC0020); }
                            finally { ReleaseDC(window, source); }
                        }
                    }
                    finally { g.ReleaseHdc(dc); }
                }
                string file = method == "pw" ? prefix : prefix + "-" + method;
                Log(Path.GetFileName(file) + ".capture_method=" + method + " api_success=" + ok);
                if (method == "pw") Log(key + ".printwindow=" + ok);
                bitmap.Save(file + ".png", ImageFormat.Png);
                var data = bitmap.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                try
                {
                    var raw = new byte[width * height * 4];
                    for (int y = 0; y < height; ++y) Marshal.Copy(data.Scan0 + y * data.Stride, raw, y * width * 4, width * 4);
                    File.WriteAllBytes(file + ".bgra", raw);
                }
                finally { bitmap.UnlockBits(data); }
            }
        }
    }

    private static void InspectInput(Process process, IntPtr top, string prefix)
    {
        var children = new List<IntPtr>();
        EnumChildWindows(top, delegate (IntPtr w, IntPtr x) { children.Add(w); return true; }, IntPtr.Zero);
        Log(prefix + ".children=" + children.Count);
        var info = new GuiThreadInfo(); info.cbSize = Marshal.SizeOf(typeof(GuiThreadInfo));
        uint dummy; uint thread = GetWindowThreadProcessId(top, out dummy);
        bool got = GetGUIThreadInfo(thread, ref info);
        Log(prefix + ".gui_thread_info=" + got + " focus=" + info.hwndFocus.ToInt64() + " caret=" + info.hwndCaret.ToInt64());
        for (int i = 0; i < children.Count; ++i)
        {
            IntPtr c = children[i];
            string p = prefix + ".child" + i;
            Log(p + ".hwnd=" + c.ToInt64() + " class=" + ClassOf(c) + " unicode_window=" + IsWindowUnicode(c) + " focused=" + (c == info.hwndFocus));
            Log(p + ".style=0x" + GetWindowLongW(c, -16).ToString("x8") + " exstyle=0x" + GetWindowLongW(c, -20).ToString("x8") + " id=" + GetWindowLongW(c, -12));
            var ansi = new byte[1024];
            int n = SendMessageABytes(c, WM_GETTEXT, (IntPtr)ansi.Length, ansi).ToInt32();
            Log(p + ".text_ansi_hex=" + Hex(ansi, Math.Max(0, n)) + " bytes=" + n);
            var wide = new StringBuilder(512);
            int wn = SendMessageWText(c, WM_GETTEXT, (IntPtr)wide.Capacity, wide).ToInt32();
            var units = new StringBuilder();
            for (int k = 0; k < wn; ++k) units.Append(((int)wide[k]).ToString("X4")).Append(k + 1 < wn ? "," : "");
            Log(p + ".text_utf16=" + units + " units=" + wn);
            if (!ClassOf(c).Equals("Edit", StringComparison.OrdinalIgnoreCase)) continue;
            Log(p + ".limit=" + SendMessageW(c, EM_GETLIMITTEXT, IntPtr.Zero, IntPtr.Zero).ToInt64());
            int s, e; SendMessageWOut(c, EM_GETSEL, out s, out e);
            Log(p + ".initial_sel=" + s + "," + e);
            // Programmatic set/query: CP932 bytes 82 A0 41 42 ("あAB" in CP932).
            byte[] payload = { 0x82, 0xA0, 0x41, 0x42, 0x00 };
            Log(p + ".set_ansi_return=" + SendMessageABytes(c, WM_SETTEXT, IntPtr.Zero, payload).ToInt64());
            n = SendMessageABytes(c, WM_GETTEXT, (IntPtr)ansi.Length, ansi).ToInt32();
            Log(p + ".after_set_ansi_hex=" + Hex(ansi, Math.Max(0, n)) + " bytes=" + n);
            wide.Length = 0; wn = SendMessageWText(c, WM_GETTEXT, (IntPtr)512, wide).ToInt32();
            units.Length = 0; for (int k = 0; k < wn; ++k) units.Append(((int)wide[k]).ToString("X4")).Append(k + 1 < wn ? "," : "");
            Log(p + ".after_set_utf16=" + units + " units=" + wn);
            SendMessageW(c, EM_SETSEL, (IntPtr)1, (IntPtr)2);
            SendMessageWOut(c, EM_GETSEL, out s, out e);
            Log(p + ".after_setsel_1_2=" + s + "," + e);
        }
    }

    private static void RunCase(string[] f, string resultDir)
    {
        string name = f[0], mode = f[1], runtime = f[2], ax = f[3], workdir = f[4];
        int timeout = int.Parse(f[5]);
        string prefix = Path.Combine(resultDir, name);
        Log("case=" + name + " mode=" + mode);
        var psi = new ProcessStartInfo(runtime, "\"" + ax + "\"") { WorkingDirectory = workdir, UseShellExecute = false };
        using (Process process = Process.Start(psi))
        {
            var seen = new List<IntPtr>();
            try
            {
                Log(name + ".pid=" + process.Id + " session=" + process.SessionId);
                if (mode == "exit")
                {
                    if (process.WaitForExit(timeout)) { Log(name + ".exit_code=" + process.ExitCode); return; }
                    Log(name + ".exit_code=timeout");
                    seen = OwnedTopWindows(process.Id);
                    for (int i = 0; i < seen.Count; ++i) { Log(name + ".timeout_window" + i + " class=" + ClassOf(seen[i]) + " title=" + TitleOf(seen[i])); Capture(seen[i], prefix + "-timeout" + i); }
                    return;
                }
                var deadline = DateTime.UtcNow.AddMilliseconds(timeout);
                while (DateTime.UtcNow < deadline && !process.HasExited && (seen = OwnedTopWindows(process.Id)).Count == 0) Thread.Sleep(100);
                if (process.HasExited) { Log(name + ".early_exit=" + process.ExitCode); return; }
                Thread.Sleep(3000);
                seen = OwnedTopWindows(process.Id);
                Log(name + ".visible_windows=" + seen.Count);
                for (int i = 0; i < seen.Count; ++i)
                {
                    Log(name + ".window" + i + " class=" + ClassOf(seen[i]) + " title=" + TitleOf(seen[i]));
                    if (mode == "input") InspectInput(process, seen[i], name + ".window" + i);
                    Capture(seen[i], prefix + "-window" + i);
                }
            }
            finally
            {
                if (!process.HasExited)
                {
                    foreach (var w in OwnedTopWindows(process.Id)) PostMessageW(w, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
                    if (!process.WaitForExit(3000)) { process.Kill(); process.WaitForExit(); Log(name + ".terminated_owned_process=true"); }
                    else Log(name + ".closed_exit_code=" + process.ExitCode);
                }
            }
        }
    }

    private static int Main(string[] args)
    {
        if (args.Length != 2) return 2;
        Directory.CreateDirectory(args[1]);
        bool aware = false;
        try { aware = SetProcessDPIAware(); } catch (EntryPointNotFoundException) { }
        using (log = new StreamWriter(Path.Combine(args[1], "probe.log"), false, new UTF8Encoding(false)))
        {
            Log("probe_pid=" + Process.GetCurrentProcess().Id + " session=" + Process.GetCurrentProcess().SessionId + " ptr_bits=" + IntPtr.Size * 8 + " dpi_aware=" + aware + " acp=" + GetACP());
            foreach (string line in File.ReadAllLines(args[0]))
            {
                if (line.Trim().Length == 0) continue;
                string[] f = line.Split('\t');
                if (f.Length != 6) { Log("bad_plan_line=" + line); continue; }
                try { RunCase(f, args[1]); }
                catch (Exception error) { Log(f[0] + ".probe_error=" + error.GetType().Name + ": " + error.Message.Replace('\n', ' ')); }
            }
            Log("done=true");
        }
        File.WriteAllText(Path.Combine(args[1], "done.marker"), "done");
        return 0;
    }
}
