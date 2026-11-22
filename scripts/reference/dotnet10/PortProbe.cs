#:property PublishAot=false
using System.Buffers.Binary;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

if (args.Length is < 1 or > 2)
{
    Console.Error.WriteLine("Usage: PortProbe <plain.bin> [test-inventory.s1]");
    return 2;
}

Console.WriteLine($"runtime={RuntimeInformation.FrameworkDescription} {RuntimeInformation.RuntimeIdentifier}");
Console.WriteLine($"os={RuntimeInformation.OSDescription}");

int integer = 1;
double fraction = 2.5;
Console.WriteLine($"int-first-x10={(int)((integer + fraction) * 10)}");
Console.WriteLine($"double-first-x10={(int)((fraction + integer) * 10)}");

var cells = new int[2, 3];
for (int y = 0; y < cells.GetLength(1); y++)
    for (int x = 0; x < cells.GetLength(0); x++)
        cells[x, y] = 10 * y + x;
var flat = new int[cells.Length];
Buffer.BlockCopy(cells, 0, flat, 0, Buffer.ByteLength(cells));
Console.WriteLine($"flat-array={string.Join(",", flat)}");

Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
var cp932 = Encoding.GetEncoding(932, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback);
const string mixed = "A表B";
Console.WriteLine($"text-length={mixed.Length} cp932-bytes={Convert.ToHexString(cp932.GetBytes(mixed))}");
foreach (string hex in new[] { "8790", "81E0" })
{
    try
    {
        string decoded = cp932.GetString(Convert.FromHexString(hex));
        Console.WriteLine($"cp932-{hex}=U+{(int)decoded[0]:X4} reencoded={Convert.ToHexString(cp932.GetBytes(decoded))}");
    }
    catch (DecoderFallbackException error)
    {
        Console.WriteLine($"cp932-{hex}={error.GetType().Name}");
    }
}

var random = new Random(12345);
var draws = new int[3];
for (int i = 0; i < draws.Length; i++)
    draws[i] = random.Next(32768);
Console.WriteLine($"rng={string.Join(",", draws)}");

using (var plain = File.OpenRead(args[0]))
using (var gzip = new GZipStream(plain, CompressionMode.Decompress))
{
    try
    {
        using var output = new MemoryStream();
        gzip.CopyTo(output);
        Console.WriteLine($"gzip-plain=decoded {output.Length}");
    }
    catch (InvalidDataException error)
    {
        Console.WriteLine($"gzip-plain={error.GetType().Name}");
    }
}

if (args.Length == 2)
{
    const int fields = 70;
    const int slots = 1320;
    const int playerSlots = 200;
    byte[] payload = new byte[fields * slots * sizeof(int)];
    using var input = File.OpenRead(args[1]);
    using var gzip = new GZipStream(input, CompressionMode.Decompress);
    gzip.ReadExactly(payload);
    if (gzip.ReadByte() != -1)
        throw new InvalidDataException("Unexpected inventory payload length for ElonaPlus 2.335.");
    if (!BitConverter.IsLittleEndian)
        throw new PlatformNotSupportedException("The direct memory copy comparison requires a little-endian CPU.");

    var rectangular = new int[fields, slots];
    Buffer.BlockCopy(payload, 0, rectangular, 0, payload.Length);
    int hspActive = 0;
    int rectangularActive = 0;
    for (int slot = 0; slot < playerSlots; slot++)
    {
        int offset = slot * fields * sizeof(int);
        if (BinaryPrimitives.ReadInt32LittleEndian(payload.AsSpan(offset)) > 0)
            hspActive++;
        if (rectangular[0, slot] > 0)
            rectangularActive++;
    }
    Console.WriteLine($"inventory-bytes={payload.Length} sha256={Convert.ToHexStringLower(SHA256.HashData(payload))}");
    Console.WriteLine($"active-player-slots hsp={hspActive} rectangular={rectangularActive}");
}

return 0;
