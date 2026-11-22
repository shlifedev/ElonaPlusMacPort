#:property PublishAot=false
using System.Globalization;
using System.Text;

if (args.Length != 1)
{
    Console.Error.WriteLine("Usage: CompareCp932 <Win32 reference table directory>");
    return 2;
}

Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
var cp932 = Encoding.GetEncoding(932, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback);

Compare("decode", key =>
{
    string value = cp932.GetString(Convert.FromHexString(key));
    return string.Join(",", value.Select(c => ((int)c).ToString("X4", CultureInfo.InvariantCulture)));
});
Compare("encode", key =>
{
    char value = (char)int.Parse(key, NumberStyles.HexNumber, CultureInfo.InvariantCulture);
    return Convert.ToHexString(cp932.GetBytes(value.ToString()));
});
return 0;

void Compare(string direction, Func<string, string> convert)
{
    int count = 0;
    int different = 0;
    string path = Path.Combine(args[0], $"cp932-{direction}.tsv");
    foreach (string line in File.ReadLines(path))
    {
        string[] columns = line.Split('\t');
        string expected = columns[1];
        if (expected.StartsWith("ERR", StringComparison.Ordinal) || expected == "DEFAULT")
            expected = "rejected";
        string actual;
        try
        {
            actual = convert(columns[0]);
        }
        catch (DecoderFallbackException)
        {
            actual = "rejected";
        }
        catch (EncoderFallbackException)
        {
            actual = "rejected";
        }
        count++;
        if (expected == actual)
            continue;
        if (different < 3)
            Console.WriteLine($"{direction} {columns[0]} win32={expected} dotnet={actual}");
        different++;
    }
    Console.WriteLine($"{direction} rows={count} different={different}");
}
