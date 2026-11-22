// x86-only adapter for the documented HSPCMP.DLL 3.4 stdcall API.
// Compiles HSP source to AX; it never loads or executes the resulting game.
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

internal static class Hsp34Compile
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryW(string path);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true, SetLastError = true)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool FreeLibrary(IntPtr module);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate int HspFunction(IntPtr p1, IntPtr p2, IntPtr p3, IntPtr p4);

    private static HspFunction Bind(IntPtr module, string name)
    {
        string export = "_" + name + "@16";
        IntPtr address = GetProcAddress(module, export);
        if (address == IntPtr.Zero) throw new InvalidOperationException("Missing export: " + export);
        return (HspFunction)Marshal.GetDelegateForFunctionPointer(address, typeof(HspFunction));
    }

    private static IntPtr EncodePath(string path)
    {
        Encoding encoding = Encoding.GetEncoding(932, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback);
        byte[] bytes = encoding.GetBytes(Path.GetFullPath(path) + "\0");
        IntPtr memory = Marshal.AllocHGlobal(bytes.Length);
        Marshal.Copy(bytes, 0, memory, bytes.Length);
        return memory;
    }

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length != 5) throw new ArgumentException("usage: Hsp34Compile.exe DLL SOURCE OUTPUT COMMON LOG_PREFIX");
            if (IntPtr.Size != 4) throw new InvalidOperationException("This helper must run as an x86 process");
            if (File.Exists(args[2])) throw new IOException("Refusing to overwrite existing AX output");
            Directory.SetCurrentDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1])));
            IntPtr library = LoadLibraryW(Path.GetFullPath(args[0]));
            if (library == IntPtr.Zero) throw new InvalidOperationException("LoadLibrary failed: " + Marshal.GetLastWin32Error());
            IntPtr source = IntPtr.Zero, output = IntPtr.Zero, common = IntPtr.Zero, sizePointer = IntPtr.Zero, messages = IntPtr.Zero;
            HspFunction bye = null;
            bool initialized = false;
            try
            {
                HspFunction ini = Bind(library, "hsc_ini");
                HspFunction objectName = Bind(library, "hsc_objname");
                HspFunction commonPath = Bind(library, "hsc_compath");
                HspFunction compile = Bind(library, "hsc_comp");
                HspFunction messageSize = Bind(library, "hsc3_messize");
                HspFunction getMessages = Bind(library, "hsc_getmes");
                bye = Bind(library, "hsc_bye");
                source = EncodePath(args[1]);
                output = EncodePath(args[2]);
                common = EncodePath(args[3].TrimEnd('\\', '/') + "\\");
                // The SDK documents string parameters in argument slot 2.
                int initResult = ini(IntPtr.Zero, source, IntPtr.Zero, IntPtr.Zero);
                if (initResult != 0) throw new InvalidOperationException("hsc_ini failed: " + initResult);
                initialized = true;
                objectName(IntPtr.Zero, output, IntPtr.Zero, IntPtr.Zero);
                commonPath(IntPtr.Zero, common, IntPtr.Zero, IntPtr.Zero);
                int result = compile(IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
                sizePointer = Marshal.AllocHGlobal(4);
                Marshal.WriteInt32(sizePointer, 0);
                messageSize(sizePointer, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
                int size = Marshal.ReadInt32(sizePointer);
                if (size < 0 || size > 16 * 1024 * 1024) throw new InvalidOperationException("Invalid compiler message size: " + size);
                messages = Marshal.AllocHGlobal(size + 1);
                Marshal.WriteByte(messages, size, 0);
                getMessages(messages, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
                byte[] raw = new byte[size];
                Marshal.Copy(messages, raw, 0, size);
                // hsc3_messize reports buffer capacity, not the text length.
                // hsc_getmes writes a NUL-terminated string; never persist the
                // uninitialized bytes after its first terminator.
                int textLength = Array.IndexOf(raw, (byte)0);
                if (textLength >= 0) Array.Resize(ref raw, textLength);
                File.WriteAllBytes(args[4] + ".cp932.log", raw);
                string text = Encoding.GetEncoding(932).GetString(raw);
                File.WriteAllText(args[4] + ".utf8.log", text, new UTF8Encoding(false));
                Console.WriteLine("pointer_bits=32");
                Console.WriteLine("hsc_comp_return=" + result);
                Console.WriteLine("message_capacity=" + size);
                Console.WriteLine("message_bytes=" + raw.Length);
                Console.WriteLine("game_executed=false");
                if (result == 0 && !File.Exists(args[2])) throw new IOException("Compiler reported success without creating output");
                return result == 0 ? 0 : 1;
            }
            finally
            {
                if (initialized && bye != null) bye(IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
                foreach (IntPtr ptr in new IntPtr[] { source, output, common, sizePointer, messages })
                    if (ptr != IntPtr.Zero) Marshal.FreeHGlobal(ptr);
                FreeLibrary(library);
            }
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.ToString());
            return 2;
        }
    }
}
