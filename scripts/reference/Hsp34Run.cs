// Capture the exact stdout/stderr bytes of a reviewed HSP3 console fixture.
// No source compilation or game execution is performed by this helper.
using System;
using System.Diagnostics;
using System.IO;

internal static class Hsp34Run
{
    private static int Main(string[] args)
    {
        try
        {
            if (args.Length != 3) throw new ArgumentException("usage: Hsp34Run.exe HSP3CL FIXTURE_AX LOG_PREFIX");
            string ax = Path.GetFullPath(args[1]);
            if (ax.Contains("\"")) throw new ArgumentException("Invalid fixture path");
            ProcessStartInfo info = new ProcessStartInfo(Path.GetFullPath(args[0]), "\"" + ax + "\"");
            info.WorkingDirectory = Path.GetDirectoryName(ax);
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            info.RedirectStandardInput = true;
            info.RedirectStandardOutput = true;
            info.RedirectStandardError = true;
            using (Process process = Process.Start(info))
            using (FileStream stdout = File.Create(args[2] + ".stdout"))
            using (FileStream stderr = File.Create(args[2] + ".stderr"))
            {
                process.StandardInput.Close();
                var outputTask = process.StandardOutput.BaseStream.CopyToAsync(stdout);
                var errorTask = process.StandardError.BaseStream.CopyToAsync(stderr);
                bool completed = process.WaitForExit(15000);
                if (!completed) { process.Kill(); process.WaitForExit(); }
                outputTask.Wait();
                errorTask.Wait();
                Console.WriteLine("runtime_exit=" + process.ExitCode);
                Console.WriteLine("timed_out=" + (!completed).ToString().ToLowerInvariant());
                return completed ? process.ExitCode : 124;
            }
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.ToString());
            return 125;
        }
    }
}
