// Dump changed mappings for the Japanese LCMapStringA flags used by 2.335.
// Arguments and return values use CP932 bytes, independently of the system ACP.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
public static class WidthMapReference {
    [DllImport("kernel32.dll", ExactSpelling=true, SetLastError=true)]
    static extern int LCMapStringA(int locale, int flags, byte[] input, int length, [Out] byte[] output, int capacity);
    [DllImport("kernel32.dll", ExactSpelling=true)]
    static extern int MultiByteToWideChar(int page, int flags, byte[] input, int length, [Out] ushort[] output, int capacity);
    static byte[] Map(int flag, byte[] s) {
        var dst = new byte[16];
        int n = LCMapStringA(0x411, flag, s, s.Length, dst, dst.Length);
        if (n <= 0) throw new Exception("LCMapStringA error " + Marshal.GetLastWin32Error());
        return dst.Take(n).ToArray();
    }
    static string Hex(byte[] s) { return BitConverter.ToString(s).Replace("-", ""); }
    public static void Run(string path) {
        var singles = new List<byte[]>();
        for (int i = 1; i < 65536; ++i) {
            var s = i <= 255 ? new byte[]{(byte)i} : new byte[]{(byte)(i >> 8), (byte)i};
            if (MultiByteToWideChar(932, 8, s, s.Length, new ushort[3], 3) == 1) singles.Add(s);
        }
        var marks = new byte[][] {new byte[]{0xde}, new byte[]{0xdf}, new byte[]{0x81,0x4a}, new byte[]{0x81,0x4b}};
        using (var w = new StreamWriter(path, false, new UTF8Encoding(false))) {
            foreach (int flag in new int[]{0x400000, 0x800000})
                foreach (var s in singles) {
                    var own = Map(flag,s);
                    if (!own.SequenceEqual(s)) w.WriteLine("{0:X} {1} {2}", flag, Hex(s), Hex(own));
                    foreach (var mark in marks) {
                        var pair = s.Concat(mark).ToArray();
                        var actual = Map(flag,pair);
                        if (!actual.SequenceEqual(own.Concat(Map(flag,mark))))
                            w.WriteLine("{0:X} {1}+{2} {3}", flag, Hex(s), Hex(mark), Hex(actual));
                    }
                }
        }
        Console.WriteLine("CP932 single-character inputs per flag: " + singles.Count);
    }
}
