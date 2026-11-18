// Read-only SDL_ttf coverage probe for an installed font; no windows/settings.
using System;
using System.IO;
using System.Globalization;
using System.Runtime.InteropServices;
class WindowsFontGlyphProbe {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
    static extern bool SetDllDirectory(string path);
    [DllImport("SDL3_ttf.dll", CallingConvention=CallingConvention.Cdecl)]
    [return:MarshalAs(UnmanagedType.I1)] static extern bool TTF_Init();
    [DllImport("SDL3_ttf.dll", CallingConvention=CallingConvention.Cdecl)]
    static extern IntPtr TTF_OpenFont(string path,float size);
    [DllImport("SDL3_ttf.dll", CallingConvention=CallingConvention.Cdecl)]
    [return:MarshalAs(UnmanagedType.I1)] static extern bool TTF_FontHasGlyph(IntPtr font,uint codepoint);
    [DllImport("SDL3_ttf.dll", CallingConvention=CallingConvention.Cdecl)]
    static extern void TTF_CloseFont(IntPtr font);
    [DllImport("SDL3_ttf.dll", CallingConvention=CallingConvention.Cdecl)]
    static extern void TTF_Quit();
    static int Main(string[] args) {
        if(args.Length!=3) return 2;
        SetDllDirectory(args[0]);
        if(!TTF_Init()) return 3;
        IntPtr font=TTF_OpenFont(args[1],14.0f);
        if(font==IntPtr.Zero) return 4;
        int count=0,missing=0;
        foreach(string s in File.ReadAllLines(args[2])) {
            if(s.Length==0) continue;
            uint cp=uint.Parse(s,NumberStyles.HexNumber,CultureInfo.InvariantCulture);
            ++count;
            if(!TTF_FontHasGlyph(font,cp)){Console.WriteLine("missing=U+"+cp.ToString("X4"));++missing;}
        }
        TTF_CloseFont(font);TTF_Quit();
        Console.WriteLine("checked="+count+" missing="+missing);
        return 0;
    }
}
