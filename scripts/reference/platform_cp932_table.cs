// NOTE: platform-cp932-01 used char[] P/Invoke (ANSI-marshalled) and is INVALID.
// x86/x64-neutral dumper of the Windows code page 932 conversion tables via the
// real Win32 API (MultiByteToWideChar / WideCharToMultiByte). Writes TSV files only
// into the output directory given on the command line. No game data involved.
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

internal static class PlatformCp932Table
{
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern int MultiByteToWideChar(uint cp, uint flags, byte[] src, int srcLen, [Out] ushort[] dst, int dstLen);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern int WideCharToMultiByte(uint cp, uint flags, ushort[] src, int srcLen, [Out] byte[] dst, int dstLen, IntPtr defaultChar, out int usedDefault);
    private const uint MB_ERR_INVALID_CHARS = 8;
    private const uint WC_NO_BEST_FIT_CHARS = 0x400;

    private static string Decode(byte[] bytes)
    {
        var buffer = new ushort[8]; // ushort: blittable UTF-16, never ANSI-marshalled
        int n = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes, bytes.Length, buffer, buffer.Length);
        if (n <= 0) return "ERR" + Marshal.GetLastWin32Error();
        var sb = new StringBuilder();
        for (int i = 0; i < n; ++i) { if (i > 0) sb.Append(','); sb.Append(((int)buffer[i]).ToString("X4")); }
        return sb.ToString();
    }

    private static int Main(string[] args)
    {
        if (args.Length != 1) { Console.Error.WriteLine("usage: platform_cp932_table OUTPUT_DIR"); return 2; }
        Directory.CreateDirectory(args[0]);
        var utf8 = new UTF8Encoding(false);
        using (var decode = new StreamWriter(Path.Combine(args[0], "cp932-decode.tsv"), false, utf8))
        {
            decode.NewLine = "\n";
            for (int b = 0; b < 256; ++b)
                decode.WriteLine(b.ToString("X2") + "\t" + Decode(new byte[] { (byte)b }));
            for (int lead = 0x80; lead < 0x100; ++lead)
                for (int trail = 0x00; trail < 0x100; ++trail)
                {
                    string r = Decode(new byte[] { (byte)lead, (byte)trail });
                    decode.WriteLine(lead.ToString("X2") + trail.ToString("X2") + "\t" + r);
                }
        }
        using (var encode = new StreamWriter(Path.Combine(args[0], "cp932-encode.tsv"), false, utf8))
        {
            encode.NewLine = "\n";
            var dst = new byte[8];
            for (int cp = 0; cp < 0x10000; ++cp)
            {
                if (cp >= 0xD800 && cp <= 0xDFFF) continue;
                int usedDefault;
                int n = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, new ushort[] { (ushort)cp }, 1, dst, dst.Length, IntPtr.Zero, out usedDefault);
                string r;
                if (n <= 0) r = "ERR" + Marshal.GetLastWin32Error();
                else if (usedDefault != 0) r = "DEFAULT";
                else { var sb = new StringBuilder(); for (int i = 0; i < n; ++i) sb.Append(dst[i].ToString("X2")); r = sb.ToString(); }
                encode.WriteLine(cp.ToString("X4") + "\t" + r);
            }
        }
        Console.WriteLine("acp=" + GetACP() + " oemcp=" + GetOEMCP() + " ptr_bits=" + (IntPtr.Size * 8));
        return 0;
    }
    [DllImport("kernel32.dll")] private static extern uint GetACP();
    [DllImport("kernel32.dll")] private static extern uint GetOEMCP();
}
