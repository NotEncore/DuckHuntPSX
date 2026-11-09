using System.Buffers.Binary;
using System.Globalization;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

namespace RomConv;

internal static class Program
{
    private const int InesHeaderSize = 16;
    private const int TrainerSize = 512;
    private const int TilePixels = 8;
    private const int BytesPerTile = 16;
    private const int AtlasTilesPerRow = 16;

    // Diagnostic grayscale only. NES in-game palettes are in PRG and are not applied here.
    private static readonly byte[] AtlasGray = { 0, 85, 170, 255 };

    /*
     * Verified PRG+CHR hashes (header and trainer excluded).
     * This list stays empty until a first-party dump is recorded in-tree.
     * Public DAT hashes are usually of a full file (with iNES header) and are
     * not copied here. Do not invent known-good hashes.
     */
    private static readonly VerifiedRom[] VerifiedRoms =
    {
        // First-party PRG+CHR hash from the local dump in roms/ (not a public DAT file hash).
        new VerifiedRom
        {
            Sha256 = "7026334A7E8742B61B450F4B3B182922C6A69FC723D7CD19C83DB365F15E45BA",
            Crc32 = "24598791",
            Label = "Duck Hunt, user cartridge dump, NES 2.0, mapper 0, 16KiB PRG + 8KiB CHR"
        }
    };

    public static int Main(string[] args)
    {
        try
        {
            return Run(args);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("error: " + ex.Message);
            return 1;
        }
    }

    private static int Run(string[] args)
    {
        string? romPath = null;
        string outDir = Path.GetFullPath("extracted");
        bool diag = false;
        bool reconstruct = false;
        bool ps1pack = false;
        bool packOnly = false;
        bool verifyOnly = false;
        string? compareDir = null;

        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            if (a is "-h" or "--help")
            {
                PrintUsage();
                return 0;
            }
            if (a is "--diag")
            {
                diag = true;
                continue;
            }
            if (a is "--reconstruct")
            {
                reconstruct = true;
                continue;
            }
            if (a is "--verify-only")
            {
                verifyOnly = true;
                continue;
            }
            if (a is "--ps1pack-only")
            {
                packOnly = true;
                continue;
            }
            if (a is "--ps1pack")
            {
                ps1pack = true;
                continue;
            }
            if (a is "--compare" && i + 1 < args.Length)
            {
                compareDir = Path.GetFullPath(args[++i]);
                continue;
            }
            if (a.StartsWith("--compare=", StringComparison.Ordinal))
            {
                compareDir = Path.GetFullPath(a[10..]);
                continue;
            }
            if (a is "--rom" && i + 1 < args.Length)
            {
                romPath = args[++i];
                continue;
            }
            if (a is "--out" && i + 1 < args.Length)
            {
                outDir = Path.GetFullPath(args[++i]);
                continue;
            }
            if (a.StartsWith("--rom=", StringComparison.Ordinal))
            {
                romPath = a[6..];
                continue;
            }
            if (a.StartsWith("--out=", StringComparison.Ordinal))
            {
                outDir = Path.GetFullPath(a[6..]);
                continue;
            }
            if (romPath == null && !a.StartsWith('-'))
            {
                romPath = a;
                continue;
            }
            Console.Error.WriteLine("error: unknown argument '" + a + "'");
            PrintUsage();
            return 2;
        }

        if (string.IsNullOrWhiteSpace(romPath))
        {
            Console.Error.WriteLine("error: provide --rom <path> to a local dump. This tool never downloads a ROM.");
            PrintUsage();
            return 2;
        }

        romPath = Path.GetFullPath(romPath);
        if (!File.Exists(romPath))
        {
            Console.Error.WriteLine("error: file not found: " + romPath);
            return 2;
        }

        byte[] file = File.ReadAllBytes(romPath);
        var identified = Identify(file, Path.GetExtension(romPath), diag);

        if (identified.Kind is FileKind.Unif or FileKind.Nsf or FileKind.Unknown)
        {
            WriteIdentifyError(identified);
            return 3;
        }

        if (identified.Kind == FileKind.HeaderlessNrom)
        {
            if (!diag)
            {
                Console.Error.WriteLine(
                    "error: no iNES/NES 2.0 header (magic NES\\x1A). " +
                    "File size matches a headerless 16KiB PRG + 8KiB CHR image. " +
                    "Re-run with --diag to extract CHR for inspection, or dump again as a standard .nes file. " +
                    "Do not download a ROM to add a header.");
                return 3;
            }
        }

        if (identified.ChrSize == 0)
        {
            Console.Error.WriteLine("error: CHR-ROM size is 0 (CHR-RAM). v0 only extracts CHR-ROM tiles.");
            return 3;
        }

        long payloadStart = identified.PayloadOffset;
        long payloadLen = identified.PrgSize + identified.ChrSize;
        if (payloadStart + payloadLen > file.Length)
        {
            Console.Error.WriteLine(
                "error: file is smaller than header describes " +
                $"(need {payloadStart + payloadLen} bytes, have {file.Length}).");
            return 3;
        }

        byte[] payload = new byte[payloadLen];
        Buffer.BlockCopy(file, (int)payloadStart, payload, 0, (int)payloadLen);

        string sha256 = Convert.ToHexString(SHA256.HashData(payload));
        uint crc32 = Crc32.Compute(payload);
        string crc32Hex = crc32.ToString("X8", CultureInfo.InvariantCulture);

        VerifiedRom? match = null;
        foreach (var v in VerifiedRoms)
        {
            if (string.Equals(v.Sha256, sha256, StringComparison.OrdinalIgnoreCase))
            {
                match = v;
                break;
            }
        }

        bool verified = match != null;
        if ((packOnly || verifyOnly) && (!verified || identified.PrgSize != 16384 || identified.ChrSize != 8192))
        {
            Console.Error.WriteLine("error: PS1 asset generation requires a verified 16 KiB PRG / 8 KiB CHR Duck Hunt dump.");
            return 3;
        }
        if (verifyOnly)
        {
            Console.WriteLine("Verified local Duck Hunt cartridge payload.");
            return 0;
        }
        byte[] chr = new byte[identified.ChrSize];
        Buffer.BlockCopy(payload, (int)identified.PrgSize, chr, 0, chr.Length);

        if (packOnly)
        {
            if (Directory.Exists(outDir) && Directory.EnumerateFileSystemEntries(outDir).Any())
            {
                Console.Error.WriteLine("error: choose an empty output directory for --ps1pack-only.");
                return 2;
            }
            byte[] localPrg = payload.AsSpan(0, (int)identified.PrgSize).ToArray();
            Console.Write(Reconstruct.ExportHuntPs1(localPrg, chr, outDir, previews: false));
            return File.Exists(Path.Combine(outDir, "hunt_ps1_assets.c")) ? 0 : 4;
        }

        Directory.CreateDirectory(outDir);
        string chrPath = Path.Combine(outDir, "chr.bin");
        string pngPath = Path.Combine(outDir, "chr_atlas.png");
        string reportPath = Path.Combine(outDir, "report.txt");

        byte[] prg = new byte[identified.PrgSize];
        Buffer.BlockCopy(payload, 0, prg, 0, prg.Length);
        string prgPath = Path.Combine(outDir, "prg.bin");
        File.WriteAllBytes(prgPath, prg);
        File.WriteAllBytes(chrPath, chr);
        File.WriteAllBytes(pngPath, BuildChrAtlasPng(chr));

        string report = BuildReport(
            romPath,
            file.Length,
            identified,
            sha256,
            crc32Hex,
            verified,
            match,
            chrPath,
            pngPath,
            payloadStart + payloadLen,
            diag,
            file);

        if (reconstruct)
        {
            string reconDir = Path.Combine(outDir, "reconstruct");
            report += Reconstruct.Run(prg, chr, reconDir);
        }

        if (ps1pack)
        {
            string packDir = Path.Combine(outDir, "ps1");
            report += Reconstruct.ExportHuntPs1(prg, chr, packDir, verifiedAssets: verified);
            string repoGen = Path.GetFullPath(Path.Combine(outDir, "..", "huntps1", "generated"));
            Directory.CreateDirectory(repoGen);
            foreach (string name in new[] { "hunt_ps1_assets.h", "hunt_ps1_assets.c",
                "nes_fly_rom_data.h", "game_b_flight.h", "clay_tables.h", "rom_hit_score_tables.h",
                "hunt_score_sprites.h", "game_a_banners.h", "clay_background.h",
                "clay_sprites.h", "clay_rom_hud.h" })
            {
                string src = Path.Combine(packDir, name);
                if (File.Exists(src))
                    File.Copy(src, Path.Combine(repoGen, name), true);
            }
        }

        if (compareDir != null)
        {
            string cmpOut = Path.Combine(outDir, "compare");
            report += ComparePpu.Run(chr, compareDir, cmpOut);
        }

        File.WriteAllText(reportPath, report, new UTF8Encoding(false));
        Console.Write(report);

        if (!verified)
        {
            Console.WriteLine();
            Console.WriteLine("NOTE: hash is not on the verified-revision list. Extraction finished for local inspection only.");
        }

        return 0;
    }

    private static void PrintUsage()
    {
        Console.WriteLine(
            """
            romconv — inspect a local NES Duck Hunt dump and extract CHR tiles
            Usage:
              romconv --rom <path-to-dump> [--out <dir>] [--diag] [--reconstruct] [--ps1pack] [--compare <dir>]

            --rom           Path to a lawfully obtained dump on this computer (required).
            --out           Output directory (default: .\extracted).
            --diag          Extra header bytes; also allows headerless 24KiB NROM inspection.
            --reconstruct   Decode PRG screen/animation streams into local diagnostic PNGs.
            --ps1pack       Pack title+hunt BG, duck $00–$0B, dog $0C–$17 into gitignored PS1 4bpp C arrays (PPUCTRL $88).
            --ps1pack-only  Generate PS1 graphics and flight tables from a verified ROM into --out.
            --verify-only   Verify the supported cartridge payload without writing files.
            --compare       Diff emulator PPU dumps in <dir> against reconstruction (does not change reconstruct).

            Never downloads a ROM. Never modifies the input file.
            Hashes are SHA-256 and CRC32 of PRG+CHR only (header/trainer excluded).
            """);
    }

    private static void WriteIdentifyError(IdentifiedRom id)
    {
        switch (id.Kind)
        {
            case FileKind.Unif:
                Console.Error.WriteLine(
                    "error: UNIF container (magic UNIF). v0 supports iNES and NES 2.0 .nes files only. " +
                    "Redump or convert locally to iNES if your dumper supports it. Do not download a ROM.");
                break;
            case FileKind.Nsf:
                Console.Error.WriteLine(
                    "error: NSF sound dump (magic NESM), not a cartridge ROM. Provide a .nes iNES dump.");
                break;
            default:
                Console.Error.WriteLine(
                    "error: unrecognized dump. Expected iNES/NES 2.0 (.nes, starts with NES<EOF>). " +
                    "Zip archives, save states, and FDS disks are not supported. " +
                    (id.Note ?? ""));
                break;
        }
    }

    private static IdentifiedRom Identify(byte[] file, string ext, bool diag)
    {
        if (file.Length >= 4 && file[0] == (byte)'U' && file[1] == (byte)'N' && file[2] == (byte)'I' && file[3] == (byte)'F')
            return new IdentifiedRom { Kind = FileKind.Unif, Note = "UNIF" };

        if (file.Length >= 4 && file[0] == (byte)'N' && file[1] == (byte)'E' && file[2] == (byte)'S' && file[3] == (byte)'M')
            return new IdentifiedRom { Kind = FileKind.Nsf, Note = "NSF" };

        bool inesMagic = file.Length >= 4 &&
                         file[0] == (byte)'N' && file[1] == (byte)'E' && file[2] == (byte)'S' && file[3] == 0x1A;

        if (inesMagic)
            return ParseInes(file);

        if (file.Length == 16 * 1024 + 8 * 1024)
        {
            return new IdentifiedRom
            {
                Kind = FileKind.HeaderlessNrom,
                FormatName = "headerless (no iNES header)",
                Mapper = -1,
                PrgSize = 16 * 1024,
                ChrSize = 8 * 1024,
                PayloadOffset = 0,
                HasTrainer = false,
                Note = "Assumed 16KiB PRG then 8KiB CHR because file size is 24576 bytes."
            };
        }

        _ = diag;
        _ = ext;
        return new IdentifiedRom
        {
            Kind = FileKind.Unknown,
            Note = $"file size {file.Length} bytes; first bytes {Preview(file)}"
        };
    }

    private static IdentifiedRom ParseInes(byte[] file)
    {
        if (file.Length < InesHeaderSize)
        {
            return new IdentifiedRom { Kind = FileKind.Unknown, Note = "truncated iNES header" };
        }

        byte prgUnits = file[4];
        byte chrUnits = file[5];
        byte flags6 = file[6];
        byte flags7 = file[7];
        bool nes2 = (flags7 & 0x0C) == 0x08;
        bool trainer = (flags6 & 0x04) != 0;
        int mapper = (flags7 & 0xF0) | (flags6 >> 4);
        int submapper = 0;
        long prgSize;
        long chrSize;

        if (nes2)
        {
            mapper |= (file[8] & 0x0F) << 8;
            submapper = file[8] >> 4;
            int prgMsb = file[9] & 0x0F;
            int chrMsb = file[9] >> 4;
            if (prgMsb == 0x0F || chrMsb == 0x0F)
            {
                return new IdentifiedRom
                {
                    Kind = FileKind.Unknown,
                    Note = "NES 2.0 exponent-multiplier ROM size is not supported in v0."
                };
            }
            prgSize = ((prgMsb << 8) | prgUnits) * 16384L;
            chrSize = ((chrMsb << 8) | chrUnits) * 8192L;
        }
        else
        {
            prgSize = prgUnits * 16384L;
            chrSize = chrUnits * 8192L;
        }

        long offset = InesHeaderSize + (trainer ? TrainerSize : 0);

        return new IdentifiedRom
        {
            Kind = nes2 ? FileKind.Nes20 : FileKind.Ines,
            FormatName = nes2 ? "NES 2.0" : "iNES",
            Mapper = mapper,
            Submapper = submapper,
            PrgSize = prgSize,
            ChrSize = chrSize,
            PayloadOffset = offset,
            HasTrainer = trainer,
            Flags6 = flags6,
            Flags7 = flags7,
            Mirroring = (flags6 & 0x08) != 0 ? "four-screen" : ((flags6 & 0x01) != 0 ? "vertical" : "horizontal"),
            Battery = (flags6 & 0x02) != 0,
            Header = file.AsSpan(0, InesHeaderSize).ToArray()
        };
    }

    private static string BuildReport(
        string romPath,
        int fileLength,
        IdentifiedRom id,
        string sha256,
        string crc32Hex,
        bool verified,
        VerifiedRom? match,
        string chrPath,
        string pngPath,
        long payloadEnd,
        bool diag,
        byte[] file)
    {
        var sb = new StringBuilder();
        sb.AppendLine("Duck Hunt PS1 — ROM converter v0");
        sb.AppendLine("Input ROM was not modified. No network access.");
        sb.AppendLine();
        sb.AppendLine("Source");
        sb.AppendLine("  path:     " + romPath);
        sb.AppendLine("  size:     " + fileLength + " bytes");
        sb.AppendLine();
        sb.AppendLine("Header");
        sb.AppendLine("  format:   " + id.FormatName);
        sb.AppendLine("  mapper:   " + (id.Mapper < 0 ? "unknown (no header)" : id.Mapper.ToString(CultureInfo.InvariantCulture)));
        if (id.Kind == FileKind.Nes20)
            sb.AppendLine("  submapper: " + id.Submapper);
        sb.AppendLine("  PRG:      " + id.PrgSize + " bytes");
        sb.AppendLine("  CHR:      " + id.ChrSize + " bytes");
        sb.AppendLine("  trainer:  " + (id.HasTrainer ? "yes (512 bytes, excluded from hashes)" : "no"));
        if (id.Mirroring != null)
            sb.AppendLine("  nametable: " + id.Mirroring);
        if (id.Kind is FileKind.Ines or FileKind.Nes20)
            sb.AppendLine("  battery:  " + (id.Battery ? "yes" : "no"));
        sb.AppendLine("  payload:  offset " + id.PayloadOffset);
        if (payloadEnd < fileLength)
            sb.AppendLine("  trailing: " + (fileLength - payloadEnd) + " bytes after PRG+CHR (not hashed)");
        sb.AppendLine();
        sb.AppendLine("Hashes (PRG+CHR only; header and trainer excluded)");
        sb.AppendLine("  SHA-256:  " + sha256);
        sb.AppendLine("  CRC32:    " + crc32Hex);
        sb.AppendLine();
        sb.AppendLine("Verification");
        if (verified && match != null)
        {
            sb.AppendLine("  status:   VERIFIED");
            sb.AppendLine("  revision: " + match.Label);
        }
        else
        {
            sb.AppendLine("  status:   NOT VERIFIED");
            sb.AppendLine("  reason:   no first-party PRG+CHR hashes are recorded in this tool yet.");
            sb.AppendLine("            Unknown hashes may be inspected; they are not treated as compatible revisions.");
        }
        sb.AppendLine();
        int tiles = (int)(id.ChrSize / BytesPerTile);
        int rows = (tiles + AtlasTilesPerRow - 1) / AtlasTilesPerRow;
        sb.AppendLine("Extracted files");
        sb.AppendLine("  " + Path.Combine(Path.GetDirectoryName(chrPath) ?? "", "prg.bin"));
        sb.AppendLine("  " + chrPath);
        sb.AppendLine("  " + pngPath);
        sb.AppendLine("    tiles:  " + tiles + " (8x8, NES 2bpp: 8 bytes plane0 + 8 bytes plane1)");
        sb.AppendLine("    atlas:  " + AtlasTilesPerRow + " tiles/row, " + rows + " rows, grayscale 0/85/170/255");
        sb.AppendLine();
        sb.AppendLine("Notes");
        sb.AppendLine("  CHR atlas is a diagnostic view, not NES palette-accurate art.");
        sb.AppendLine("  Game A/B/C, palettes, audio, and PS1 disc build are not in v0.");
        if (id.Mapper > 0)
            sb.AppendLine("  Mapper is not 0 (NROM). Duck Hunt cartridges are mapper 0; this dump may be unsupported later.");
        if (id.Note != null)
            sb.AppendLine("  " + id.Note);

        if (diag && id.Header != null)
        {
            sb.AppendLine();
            sb.AppendLine("Diag header (16 bytes)");
            sb.AppendLine("  " + Convert.ToHexString(id.Header));
            sb.AppendLine("  flags6=0x" + id.Flags6.ToString("X2", CultureInfo.InvariantCulture) +
                          " flags7=0x" + id.Flags7.ToString("X2", CultureInfo.InvariantCulture));
        }

        _ = file;
        return sb.ToString();
    }

    private static byte[] BuildChrAtlasPng(byte[] chr)
    {
        int tiles = chr.Length / BytesPerTile;
        if (tiles <= 0)
            throw new InvalidOperationException("CHR contains no tiles.");

        int cols = AtlasTilesPerRow;
        int rows = (tiles + cols - 1) / cols;
        int width = cols * TilePixels;
        int height = rows * TilePixels;
        byte[] gray = new byte[width * height];

        for (int t = 0; t < tiles; t++)
        {
            int col = t % cols;
            int row = t / cols;
            int tileOff = t * BytesPerTile;
            for (int y = 0; y < TilePixels; y++)
            {
                byte p0 = chr[tileOff + y];
                byte p1 = chr[tileOff + 8 + y];
                int destRow = (row * TilePixels + y) * width + col * TilePixels;
                for (int x = 0; x < TilePixels; x++)
                {
                    int bit = 7 - x;
                    int idx = ((p1 >> bit) & 1) << 1 | ((p0 >> bit) & 1);
                    gray[destRow + x] = AtlasGray[idx];
                }
            }
        }

        return Png.WriteGrayscale8(width, height, gray);
    }

    private static string Preview(byte[] file)
    {
        int n = Math.Min(4, file.Length);
        return Convert.ToHexString(file.AsSpan(0, n));
    }
}

internal enum FileKind
{
    Ines,
    Nes20,
    HeaderlessNrom,
    Unif,
    Nsf,
    Unknown
}

internal sealed class IdentifiedRom
{
    public FileKind Kind;
    public string FormatName = "";
    public int Mapper;
    public int Submapper;
    public long PrgSize;
    public long ChrSize;
    public long PayloadOffset;
    public bool HasTrainer;
    public string? Mirroring;
    public bool Battery;
    public byte Flags6;
    public byte Flags7;
    public byte[]? Header;
    public string? Note;
}

internal sealed class VerifiedRom
{
    public required string Sha256 { get; init; }
    public string? Crc32 { get; init; }
    public required string Label { get; init; }
}

internal static class Crc32
{
    private static readonly uint[] Table = Build();

    private static uint[] Build()
    {
        var t = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            uint c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }

    public static uint Compute(ReadOnlySpan<byte> data)
    {
        uint crc = 0xFFFFFFFFu;
        foreach (byte b in data)
            crc = Table[(crc ^ b) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }
}

internal static class Png
{
    public static byte[] WriteGrayscale8(int width, int height, byte[] pixels)
    {
        if (pixels.Length != width * height)
            throw new ArgumentException("pixel count");

        using var ms = new MemoryStream();
        ReadOnlySpan<byte> sig = stackalloc byte[] { 137, 80, 78, 71, 13, 10, 26, 10 };
        ms.Write(sig);

        Span<byte> ihdr = stackalloc byte[13];
        BinaryPrimitives.WriteInt32BigEndian(ihdr[0..4], width);
        BinaryPrimitives.WriteInt32BigEndian(ihdr[4..8], height);
        ihdr[8] = 8;
        ihdr[9] = 0;
        ihdr[10] = 0;
        ihdr[11] = 0;
        ihdr[12] = 0;
        WriteChunk(ms, "IHDR"u8, ihdr);

        byte[] raw = new byte[height * (1 + width)];
        for (int y = 0; y < height; y++)
        {
            int src = y * width;
            int dst = y * (1 + width);
            raw[dst] = 0;
            Buffer.BlockCopy(pixels, src, raw, dst + 1, width);
        }

        using var compressed = new MemoryStream();
        using (var zs = new ZLibStream(compressed, CompressionLevel.SmallestSize, leaveOpen: true))
            zs.Write(raw, 0, raw.Length);
        WriteChunk(ms, "IDAT"u8, compressed.ToArray());
        WriteChunk(ms, "IEND"u8, ReadOnlySpan<byte>.Empty);
        return ms.ToArray();
    }

    public static byte[] WriteRgb8(int width, int height, byte[] rgb)
    {
        if (rgb.Length != width * height * 3)
            throw new ArgumentException("pixel count");
        return WriteRaw(width, height, 2, 3, rgb);
    }

    public static byte[] WriteRgba8(int width, int height, byte[] rgba)
    {
        if (rgba.Length != width * height * 4)
            throw new ArgumentException("pixel count");
        return WriteRaw(width, height, 6, 4, rgba);
    }

    private static byte[] WriteRaw(int width, int height, byte colorType, int bpp, byte[] pixels)
    {
        using var ms = new MemoryStream();
        ReadOnlySpan<byte> sig = stackalloc byte[] { 137, 80, 78, 71, 13, 10, 26, 10 };
        ms.Write(sig);

        Span<byte> ihdr = stackalloc byte[13];
        BinaryPrimitives.WriteInt32BigEndian(ihdr[0..4], width);
        BinaryPrimitives.WriteInt32BigEndian(ihdr[4..8], height);
        ihdr[8] = 8;
        ihdr[9] = colorType;
        WriteChunk(ms, "IHDR"u8, ihdr);

        int stride = width * bpp;
        byte[] raw = new byte[height * (1 + stride)];
        for (int y = 0; y < height; y++)
        {
            int src = y * stride;
            int dst = y * (1 + stride);
            raw[dst] = 0;
            Buffer.BlockCopy(pixels, src, raw, dst + 1, stride);
        }

        using var compressed = new MemoryStream();
        using (var zs = new ZLibStream(compressed, CompressionLevel.SmallestSize, leaveOpen: true))
            zs.Write(raw, 0, raw.Length);
        WriteChunk(ms, "IDAT"u8, compressed.ToArray());
        WriteChunk(ms, "IEND"u8, ReadOnlySpan<byte>.Empty);
        return ms.ToArray();
    }

    public static bool TryReadRgb8(byte[] png, out int width, out int height, out byte[] rgb)
    {
        width = 0;
        height = 0;
        rgb = Array.Empty<byte>();
        if (png.Length < 33) return false;
        ReadOnlySpan<byte> sig = stackalloc byte[] { 137, 80, 78, 71, 13, 10, 26, 10 };
        if (!png.AsSpan(0, 8).SequenceEqual(sig)) return false;
        int off = 8;
        byte bitDepth = 0, colorType = 0, interlace = 0;
        var idat = new MemoryStream();
        while (off + 12 <= png.Length)
        {
            int len = BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(off, 4));
            if (len < 0 || off + 12 + len > png.Length) return false;
            string type = System.Text.Encoding.ASCII.GetString(png, off + 4, 4);
            var data = png.AsSpan(off + 8, len);
            if (type == "IHDR")
            {
                if (len < 13) return false;
                width = BinaryPrimitives.ReadInt32BigEndian(data);
                height = BinaryPrimitives.ReadInt32BigEndian(data[4..]);
                bitDepth = data[8];
                colorType = data[9];
                interlace = data[12];
            }
            else if (type == "IDAT")
                idat.Write(data);
            else if (type == "IEND")
                break;
            off += 12 + len;
        }
        if (width <= 0 || height <= 0 || width > 1024 || height > 1024) return false;
        if (bitDepth != 8 || interlace != 0) return false;
        if (colorType != 2 && colorType != 6) return false;
        int bpp = colorType == 6 ? 4 : 3;
        byte[] raw;
        try
        {
            idat.Position = 0;
            using var zs = new ZLibStream(idat, CompressionMode.Decompress);
            using var decoded = new MemoryStream();
            zs.CopyTo(decoded);
            raw = decoded.ToArray();
        }
        catch
        {
            return false;
        }
        int stride = width * bpp;
        int expected = height * (1 + stride);
        if (raw.Length < expected) return false;
        rgb = new byte[width * height * 3];
        var prev = new byte[stride];
        var cur = new byte[stride];
        for (int y = 0; y < height; y++)
        {
            int src = y * (1 + stride);
            byte filter = raw[src];
            ReadOnlySpan<byte> sl = raw.AsSpan(src + 1, stride);
            PaethUnfilter(filter, sl, prev, cur, bpp);
            for (int x = 0; x < width; x++)
            {
                int d = (y * width + x) * 3;
                int s = x * bpp;
                rgb[d] = cur[s];
                rgb[d + 1] = cur[s + 1];
                rgb[d + 2] = cur[s + 2];
            }
            var tmp = prev; prev = cur; cur = tmp;
        }
        return true;
    }

    private static void PaethUnfilter(byte filter, ReadOnlySpan<byte> sl, byte[] prev, byte[] cur, int bpp)
    {
        for (int i = 0; i < sl.Length; i++)
        {
            byte a = i >= bpp ? cur[i - bpp] : (byte)0;
            byte b = prev[i];
            byte c = i >= bpp ? prev[i - bpp] : (byte)0;
            byte x = sl[i];
            cur[i] = filter switch
            {
                1 => (byte)(x + a),
                2 => (byte)(x + b),
                3 => (byte)(x + ((a + b) / 2)),
                4 => (byte)(x + Paeth(a, b, c)),
                _ => x
            };
        }
    }

    private static byte Paeth(byte a, byte b, byte c)
    {
        int p = a + b - c;
        int pa = Math.Abs(p - a), pb = Math.Abs(p - b), pc = Math.Abs(p - c);
        if (pa <= pb && pa <= pc) return a;
        if (pb <= pc) return b;
        return c;
    }

    private static void WriteChunk(Stream s, ReadOnlySpan<byte> type, ReadOnlySpan<byte> data)
    {
        Span<byte> len = stackalloc byte[4];
        BinaryPrimitives.WriteInt32BigEndian(len, data.Length);
        s.Write(len);
        s.Write(type);
        s.Write(data);

        byte[] crcBuf = new byte[type.Length + data.Length];
        type.CopyTo(crcBuf);
        data.CopyTo(crcBuf.AsSpan(type.Length));
        uint crc = Crc32.Compute(crcBuf);

        Span<byte> crcBytes = stackalloc byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(crcBytes, crc);
        s.Write(crcBytes);
    }
}
