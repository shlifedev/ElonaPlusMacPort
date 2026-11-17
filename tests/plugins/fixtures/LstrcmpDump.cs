// Dump Windows lstrcmpA / CompareStringA(0x0411) signs for a deterministic
// printable-ASCII corpus. Built as x86 like the original hspda.dll caller.
// Output: "<sign> <hex a|-> <hex b|->" lines; header lines start with '#'.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

internal static class LstrcmpDump
{
    [DllImport("kernel32.dll", CallingConvention = CallingConvention.Winapi)]
    private static extern int lstrcmpA(byte[] a, byte[] b);

    [DllImport("kernel32.dll", CallingConvention = CallingConvention.Winapi)]
    private static extern int CompareStringA(uint locale, uint flags, byte[] a, int alen, byte[] b, int blen);

    [DllImport("kernel32.dll")]
    private static extern uint GetUserDefaultLCID();

    [DllImport("kernel32.dll")]
    private static extern uint GetACP();

    private static byte[] Z(string s)
    {
        byte[] r = new byte[s.Length + 1];
        for (int i = 0; i < s.Length; i++) r[i] = (byte)s[i];
        return r;
    }

    private static string Hex(string s)
    {
        if (s.Length == 0) return "-";
        StringBuilder b = new StringBuilder();
        foreach (char c in s) b.Append(((int)c).ToString("x2"));
        return b.ToString();
    }

    private static int Main(string[] args)
    {
        if (args.Length != 2) { Console.Error.WriteLine("usage: LstrcmpDump OUT_USER OUT_0411"); return 2; }
        List<string[]> pairs = new List<string[]>();
        List<string> singles = new List<string>();
        singles.Add("");
        for (int c = 0x20; c <= 0x7e; c++) singles.Add(((char)c).ToString());
        foreach (string a in singles) foreach (string b in singles) pairs.Add(new string[] { a, b });
        string alphabet = "aAbB-'_ 0.,~+";
        List<string> twos = new List<string>();
        foreach (char x in alphabet) foreach (char y in alphabet) twos.Add(new string(new char[] { x, y }));
        foreach (string a in twos) foreach (string b in twos) pairs.Add(new string[] { a, b });
        uint state = 20261117;
        for (int i = 0; i < 12000; i++)
        {
            string[] p = new string[2];
            for (int k = 0; k < 2; k++)
            {
                state = state * 1103515245u + 12345u;
                int len = (int)((state >> 16) % 9);
                StringBuilder s = new StringBuilder();
                for (int j = 0; j < len; j++)
                {
                    state = state * 1103515245u + 12345u;
                    // Bias towards letters, '-', '\'' and a few symbols to stress ties.
                    int pick = (int)((state >> 16) % 100);
                    char ch;
                    if (pick < 50) ch = "aAbBcC"[pick % 6];
                    else if (pick < 65) ch = "-'"[pick % 2];
                    else ch = (char)(0x20 + (int)((state >> 8) % 95));
                    s.Append(ch);
                }
                p[k] = s.ToString();
            }
            pairs.Add(p);
        }
        using (StreamWriter user = new StreamWriter(args[0], false, new UTF8Encoding(false)))
        using (StreamWriter ja = new StreamWriter(args[1], false, new UTF8Encoding(false)))
        {
            user.NewLine = "\n";
            ja.NewLine = "\n";
            string header = "# lcid=0x" + GetUserDefaultLCID().ToString("x4") + " acp=" + GetACP() + " pairs=" + pairs.Count;
            user.WriteLine(header + " api=lstrcmpA");
            ja.WriteLine(header + " api=CompareStringA(0x0411)");
            foreach (string[] p in pairs)
            {
                byte[] a = Z(p[0]);
                byte[] b = Z(p[1]);
                int r = lstrcmpA(a, b);
                int c = CompareStringA(0x0411, 0, a, -1, b, -1);
                if (c == 0) throw new Exception("CompareStringA failed");
                user.WriteLine(Math.Sign(r) + " " + Hex(p[0]) + " " + Hex(p[1]));
                ja.WriteLine((c - 2) + " " + Hex(p[0]) + " " + Hex(p[1]));
            }
        }
        Console.WriteLine("pairs=" + pairs.Count);
        return 0;
    }
}
