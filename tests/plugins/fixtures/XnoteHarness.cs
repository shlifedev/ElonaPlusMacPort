// Direct x86 call harness for the bundled hspda.dll xnotesel/xnoteadd.
// The HSP 3.4 runtime crashes while marshalling the `$83` (PPVAL) argument,
// so this harness passes a hand-built HSP3 PVal (flag at +0, pt at +0x1c)
// and a C string, matching the DLL's observed parameter use.
// Output lines: "xnote <label> <stat> <hex buffer up to NUL|->" (stat = -EAX).
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

internal static class XnoteHarness
{
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate int Fn4(IntPtr a, IntPtr b, int c, int d);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr LoadLibraryW(string path);

    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);

    private static Fn4 sel, add;
    private static StringBuilder output = new StringBuilder();

    private static IntPtr MakePVal(short flag, IntPtr pt)
    {
        IntPtr p = Marshal.AllocHGlobal(0x40);
        for (int i = 0; i < 0x40; i++) Marshal.WriteByte(p, i, 0);
        Marshal.WriteInt16(p, 0, flag);
        Marshal.WriteIntPtr(p, 0x1c, pt);
        return p;
    }

    private static string Hex(IntPtr buf)
    {
        StringBuilder b = new StringBuilder();
        for (int i = 0; ; i++)
        {
            byte c = Marshal.ReadByte(buf, i);
            if (c == 0) break;
            b.Append(c.ToString("x2"));
        }
        return b.Length == 0 ? "-" : b.ToString();
    }

    private static IntPtr CStr(string s)
    {
        byte[] raw = Encoding.ASCII.GetBytes(s + "\0");
        IntPtr p = Marshal.AllocHGlobal(raw.Length);
        Marshal.Copy(raw, 0, p, raw.Length);
        return p;
    }

    private static IntPtr Buffer(int size, byte[] init)
    {
        IntPtr p = Marshal.AllocHGlobal(size);
        for (int i = 0; i < size; i++) Marshal.WriteByte(p, i, 0);
        if (init != null) Marshal.Copy(init, 0, p, init.Length);
        return p;
    }

    private static void Line(string label, int eax, IntPtr buf)
    {
        output.Append("xnote " + label + " " + (-eax) + " " + (buf == IntPtr.Zero ? "-" : Hex(buf)) + "\n");
    }

    private static int Main(string[] args)
    {
        if (args.Length != 2) { Console.Error.WriteLine("usage: XnoteHarness HSPDA_DLL OUT"); return 2; }
        IntPtr module = LoadLibraryW(Path.GetFullPath(args[0]));
        if (module == IntPtr.Zero) throw new Exception("LoadLibrary failed " + Marshal.GetLastWin32Error());
        sel = (Fn4)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module, "_xnotesel@16"), typeof(Fn4));
        add = (Fn4)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module, "_xnoteadd@16"), typeof(Fn4));

        IntPtr xb = Buffer(256, null);
        IntPtr pv = MakePVal(2, xb);
        Line("sel1", sel(pv, IntPtr.Zero, 0, 0), xb);
        Line("add1", add(IntPtr.Zero, CStr("sword"), 0, 0), xb);
        Line("add2", add(IntPtr.Zero, CStr("shield"), 0, 0), xb);
        Line("add3", add(IntPtr.Zero, CStr("sword"), 0, 0), xb);
        Line("add4", add(IntPtr.Zero, CStr(""), 0, 0), xb);
        Line("add5", add(IntPtr.Zero, CStr(""), 0, 0), xb);

        IntPtr xb2 = Buffer(64, new byte[] { 97, 10, 98 });
        Line("sel2", sel(MakePVal(2, xb2), (IntPtr)4, 0, 0), xb2);
        Line("add6", add(IntPtr.Zero, CStr("c"), 0, 0), xb2);

        IntPtr xb3 = Buffer(256, Encoding.ASCII.GetBytes("old"));
        Line("sel4", sel(MakePVal(2, xb3), IntPtr.Zero, 0, 0), xb3);
        Marshal.WriteByte(xb3, 0, 0);  // game: `buff = ""` keeps the same buffer
        Line("add7", add(IntPtr.Zero, CStr("game"), 0, 0), xb3);
        Line("add8", add(IntPtr.Zero, CStr("game"), 0, 0), xb3);
        Line("add9", add(IntPtr.Zero, CStr("item b"), 0, 0), xb3);
        Line("add10", add(IntPtr.Zero, CStr("game"), 0, 0), xb3);

        IntPtr iv = Buffer(16, null);
        Line("sel3", sel(MakePVal(4, iv), IntPtr.Zero, 0, 0), IntPtr.Zero);
        output.Append("end\n");
        File.WriteAllText(args[1], output.ToString(), new UTF8Encoding(false));
        Console.Write(output.ToString());
        return 0;
    }
}
