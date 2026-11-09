using System.Globalization;
using System.Text;

namespace RomConv;

/// <summary>
/// Compare emulator PPU dumps to converter reconstruction. Does not change
/// reconstruction logic or the ROM file.
/// </summary>
internal static class ComparePpu
{

    public static string Run(byte[] chrFromRom, string captureDir, string outDir)
    {
        var log = new StringBuilder();
        log.AppendLine();
        log.AppendLine("PPU capture compare");
        log.AppendLine("  capture:    " + captureDir);
        Directory.CreateDirectory(outDir);
        Directory.CreateDirectory(captureDir);

        string? ntPath = FirstExisting(captureDir, "nametable.bin", "Nametable.bin", "vram.bin");
        string? palPath = FirstExisting(captureDir, "palette.bin", "Palette.bin");
        string? oamPath = FirstExisting(captureDir, "oam.bin", "OAM.bin", "sprite.bin");
        string? chrPath = FirstExisting(captureDir, "chr.bin", "CHR.bin");
        string? ctrlPath = FirstExisting(captureDir, "ppuctrl.txt", "PPUCTRL.txt");

        bool haveNt = ntPath != null;
        bool havePal = palPath != null;
        bool haveOam = oamPath != null;
        if (!haveNt || !havePal || !haveOam)
        {
            log.AppendLine("  status:     WAITING FOR EMULATOR DUMPS (not an emulator-confirmed discrepancy yet).");
            log.AppendLine("  missing:    " +
                           (haveNt ? "" : "nametable.bin ") +
                           (havePal ? "" : "palette.bin ") +
                           (haveOam ? "" : "oam.bin"));
            log.AppendLine();
            log.Append(CaptureHowto());
            File.WriteAllText(Path.Combine(outDir, "compare_report.txt"), log.ToString(), new UTF8Encoding(false));
            File.WriteAllText(Path.Combine(captureDir, "compare_report.txt"), log.ToString(), new UTF8Encoding(false));
            return log.ToString();
        }

        byte[] ntFile = File.ReadAllBytes(ntPath!);
        byte[] palFile = File.ReadAllBytes(palPath!);
        byte[] oamFile = File.ReadAllBytes(oamPath!);
        byte[] chr = chrPath != null ? File.ReadAllBytes(chrPath) : chrFromRom;

        byte[] nt = TakeNametable0(ntFile);
        byte[] pal = new byte[32];
        Array.Copy(palFile, pal, Math.Min(32, palFile.Length));

        int ctrl = 0x90;
        bool ctrlKnown = false;
        if (ctrlPath != null)
        {
            string t = File.ReadAllText(ctrlPath).Trim();
            if (t.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                t = t[2..];
            if (int.TryParse(t, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out int v))
            {
                ctrl = v & 0xFF;
                ctrlKnown = true;
            }
        }

        // PPUCTRL bit3 = BG pattern table, bit4 = sprite pattern table.
        int bgPat = (ctrl & 0x08) != 0 ? 0x1000 : 0x0000;
        int sprPat = (ctrl & 0x10) != 0 ? 0x1000 : 0x0000;
        bool spr16 = (ctrl & 0x20) != 0;

        log.AppendLine("  files:      nametable " + ntFile.Length + "B, palette " + palFile.Length +
                       "B, oam " + oamFile.Length + "B, chr " + chr.Length + "B");
        log.AppendLine(ctrlKnown
            ? "  PPUCTRL:    $" + ctrl.ToString("X2", CultureInfo.InvariantCulture) +
              "  BG pattern $" + bgPat.ToString("X4", CultureInfo.InvariantCulture) +
              "  SPR pattern $" + sprPat.ToString("X4", CultureInfo.InvariantCulture) +
              "  sprites " + (spr16 ? "8x16" : "8x8")
            : "  PPUCTRL:    not captured; assumed $90 (BG $0000, SPR $1000, 8x8) — UNCONFIRMED");

        log.AppendLine("  palette:    " + Convert.ToHexString(pal));

        int sky24 = CountTile(nt, 0, 4, 0x24);
        int sky00 = CountTile(nt, 0, 4, 0x00);
        var skySet = UniqueTiles(nt, 0, 4);
        log.AppendLine("  sky rows0-3 tile $24 count=" + sky24 + "/128  tile $00=" + sky00 +
                       "  unique=[" + string.Join(",", skySet.Select(t => t.ToString("X2", CultureInfo.InvariantCulture))) + "]");

        string reconTiles = Path.GetFullPath(Path.Combine(captureDir, "..", "..", "extracted", "reconstruct", "bg_hunt_scene_tiles.txt"));
        if (!File.Exists(reconTiles))
            reconTiles = Path.GetFullPath("extracted/reconstruct/bg_hunt_scene_tiles.txt");
        if (File.Exists(reconTiles))
        {
            byte[] recon = LoadTileMap(reconTiles);
            int match = 0, skyMismatch = 0;
            for (int i = 0; i < 960; i++)
            {
                if (recon[i] == nt[i]) match++;
                if (i < 128 && recon[i] != nt[i]) skyMismatch++;
            }
            log.AppendLine("  vs converter NT: " + match + "/960 tiles identical; sky rows 0-3 mismatches=" + skyMismatch);
            int recon24 = 0;
            for (int i = 0; i < 128; i++)
                if (recon[i] == 0x24) recon24++;
            log.AppendLine("  converter sky $24 count=" + recon24 + "/128 (static reconstruct). Emulator $24 count=" + sky24 + ".");
            int listed = 0;
            for (int i = 0; i < 960 && listed < 16; i++)
            {
                if (recon[i] == nt[i]) continue;
                log.AppendLine("  NT mismatch tile (" + (i % 32).ToString(CultureInfo.InvariantCulture) + "," +
                               (i / 32).ToString(CultureInfo.InvariantCulture) + ") converter=$" +
                               recon[i].ToString("X2", CultureInfo.InvariantCulture) + " live=$" +
                               nt[i].ToString("X2", CultureInfo.InvariantCulture));
                listed++;
            }
            if (sky24 == 0 && recon24 > 0)
                log.AppendLine("  EMULATOR-CONFIRMED: live Game A sky is NOT tile $24. Converter leftover-$24 theory is disproved for this frame.");
            else if (sky24 > 64 && bgPat == 0x1000)
                log.AppendLine("  EMULATOR-CONFIRMED: live sky nametable is tile $24 fetched from BG pattern $1000. Mesen screenshot is solid blue, so CHR $1000+$24 is a blank/sky tile. Converter diagonals are from drawing the same tile index out of CHR $0000.");
            else if (sky24 > 64)
                log.AppendLine("  EMULATOR-CONFIRMED: live sky is mostly tile $24. Inspect capture_bg.png versus the Mesen screenshot for whether that tile is blank or a triangle in the active BG bank.");
            else
                log.AppendLine("  MIXED: both dumps have some $24; inspect capture_bg.png vs bg_hunt_scene.png.");
        }
        else
            log.AppendLine("  vs converter: extracted/reconstruct/bg_hunt_scene_tiles.txt not found (run --reconstruct once).");

        var oamTiles = new Dictionary<int, int>();
        int visible = 0, fontish = 0, duckish = 0;
        var oamLog = new StringBuilder();
        oamLog.AppendLine("# OAM Y tile attr X  (Y>=$EF hidden)");
        for (int i = 0; i < 64; i++)
        {
            int o = i * 4;
            if (o + 3 >= oamFile.Length) break;
            int y = oamFile[o];
            int tile = oamFile[o + 1];
            int attr = oamFile[o + 2];
            int x = oamFile[o + 3];
            bool hid = y >= 0xEF;
            oamLog.AppendLine(i.ToString("00", CultureInfo.InvariantCulture) + "  " +
                              y.ToString("X2", CultureInfo.InvariantCulture) + " " +
                              tile.ToString("X2", CultureInfo.InvariantCulture) + " " +
                              attr.ToString("X2", CultureInfo.InvariantCulture) + " " +
                              x.ToString("X2", CultureInfo.InvariantCulture) + (hid ? "  hidden" : ""));
            if (hid) continue;
            visible++;
            oamTiles[tile] = oamTiles.GetValueOrDefault(tile) + 1;
            if (tile <= 0x1F) fontish++;
            if (tile >= 0x40 && tile <= 0xA8) duckish++;
        }
        log.AppendLine("  OAM visible: " + visible + "  tiles<=$1F=" + fontish +
                       "  tiles $40-$A8=" + duckish);
        if (fontish > 0 && sprPat == 0)
            log.AppendLine("  EMULATOR-CONFIRMED: flying duck OAM uses tile IDs $00-$0C with sprite pattern $0000. Those indices are duck CHR, not the font at $1000. Converter ducks look like font if they blit the same IDs from $1000.");
        else if (fontish > 0 && duckish == 0)
            log.AppendLine("  EMULATOR-CONFIRMED: on-screen sprites use tile IDs <=$1F. Combined with SPR pattern $1000 this would be font CHR.");
        else if (duckish > 0 && fontish == 0)
            log.AppendLine("  EMULATOR-CONFIRMED: flying sprites use tile IDs $40+; converter frames that blit $00-$0C may be the wrong frame or bank.");
        else
            log.AppendLine("  OAM mixes low and high tile IDs; see capture_oam.txt.");

        log.AppendLine("  CHR mapping: NROM identity (pattern $0000 = CHR $0000, pattern $1000 = CHR $1000) on this dump.");

        var bgIndex = new byte[NesPpu.Width * NesPpu.Height];
        byte[] bg = NesPpu.RenderNametable(chr, nt, pal, bgPat, bgIndex);

        int yMode = 1;
        byte[]? shot = null;
        int shotW = 0, shotH = 0;
        string? shotPath = FirstExisting(captureDir, "mesen_screenshot.png");
        if (shotPath != null && Png.TryReadRgb8(File.ReadAllBytes(shotPath), out shotW, out shotH, out shot))
            log.AppendLine("  mesen png:  " + shotW + "x" + shotH + " " + shotPath);
        else
        {
            shot = null;
            log.AppendLine("  mesen png:  not decoded (skip pixel match / Y-delay pick).");
        }

        NesPpu.SpriteLayer sprLayer = NesPpu.RenderOam(chr, oamFile, pal, sprPat, spr16, yMode);
        byte[] framed = NesPpu.Composite(bg, bgIndex, sprLayer);
        if (shot != null && shotW == NesPpu.Width && shotH == NesPpu.Height)
        {
            int match1 = CountNearest(framed, shot);
            var spr0 = NesPpu.RenderOam(chr, oamFile, pal, sprPat, spr16, 0);
            byte[] framed0 = NesPpu.Composite(bg, bgIndex, spr0);
            int match0 = CountNearest(framed0, shot);
            log.AppendLine("  Y placement: nearest NES color Y+1 " + match1 + "/61440 vs Y+0 " + match0 + "/61440 (exact RGB ignored; palettes differ).");
            if (match0 > match1)
            {
                yMode = 0;
                sprLayer = spr0;
                framed = framed0;
                log.AppendLine("  using OAM Y as top row for this capture (better index match).");
            }
            else
                log.AppendLine("  using NES scanline delay (display at OAM Y+1).");
        }
        else
            log.AppendLine("  using NES scanline delay (display at OAM Y+1); screenshot not available to confirm.");

        byte[] duckRgba = NesPpu.IsolatedSpritesPngRgba(
            chr, oamFile, pal, sprPat, spr16, yMode,
            (y, tile, attr, x) => y < 0x90,
            out int duckW, out int duckH, out int duckX, out int duckY);

        WriteBoth(outDir, captureDir, "gameA_bg.png", Png.WriteRgb8(NesPpu.Width, NesPpu.Height, bg));
        WriteBoth(outDir, captureDir, "gameA_sprites.png", Png.WriteRgba8(NesPpu.Width, NesPpu.Height, sprLayer.Rgba));
        WriteBoth(outDir, captureDir, "gameA_frame.png", Png.WriteRgb8(NesPpu.Width, NesPpu.Height, framed));
        WriteBoth(outDir, captureDir, "gameA_duck.png", Png.WriteRgba8(duckW, duckH, duckRgba));
        WriteBoth(outDir, captureDir, "capture_bg.png", Png.WriteRgb8(NesPpu.Width, NesPpu.Height, bg));
        WriteBoth(outDir, captureDir, "capture_sprites.png", Png.WriteRgba8(NesPpu.Width, NesPpu.Height, sprLayer.Rgba));
        WriteBoth(outDir, captureDir, "capture_frame.png", Png.WriteRgb8(NesPpu.Width, NesPpu.Height, framed));
        File.WriteAllText(Path.Combine(outDir, "capture_oam.txt"), oamLog.ToString(), new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(captureDir, "capture_oam.txt"), oamLog.ToString(), new UTF8Encoding(false));
        log.AppendLine("  duck sprite: isolated " + duckW + "x" + duckH + " at (" + duckX + "," + duckY + ") from OAM Y<" + 0x90.ToString("X2", CultureInfo.InvariantCulture) + ", SPR $" + sprPat.ToString("X4", CultureInfo.InvariantCulture));
        log.AppendLine("  wrote:      gameA_frame.png / gameA_bg.png / gameA_sprites.png / gameA_duck.png");

        if (shot != null && shotW == NesPpu.Width && shotH == NesPpu.Height)
            log.Append(PixelReport(framed, shot, bg, sprLayer, outDir, captureDir));
        else if (shot != null)
            log.AppendLine("  pixel match: screenshot size " + shotW + "x" + shotH + " != 256x240 — not compared.");

        string report = log.ToString();
        File.WriteAllText(Path.Combine(outDir, "compare_report.txt"), report, new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(captureDir, "compare_report.txt"), report, new UTF8Encoding(false));
        return report;
    }

    public static string CaptureHowto()
    {
        return
            """
              Shortest Mesen 2 capture (GUI; no CLI dump API on this build):
              1. Open your verified ROM in Mesen.
              2. Title: GAME A, play until a duck is flying. Emulation → Pause.
              3. Debug → Script Window → Open captures\mesen_dump_gameA.lua → Run
                 (or Memory Tools export Nametable RAM, Palette RAM, Sprite RAM into captures\gameA\).
              4. Save PPUCTRL from PPU Viewer as captures\gameA\ppuctrl.txt (one hex byte).
              5. romconv --rom roms\DuckHunt.nes --compare captures\gameA
            """;
    }

    private static void WriteBoth(string a, string b, string name, byte[] bytes)
    {
        File.WriteAllBytes(Path.Combine(a, name), bytes);
        File.WriteAllBytes(Path.Combine(b, name), bytes);
    }

    private static int CountExact(byte[] a, byte[] b)
    {
        int n = Math.Min(a.Length, b.Length) / 3;
        int m = 0;
        for (int i = 0; i < n; i++)
        {
            int o = i * 3;
            if (a[o] == b[o] && a[o + 1] == b[o + 1] && a[o + 2] == b[o + 2]) m++;
        }
        return m;
    }

    private static int CountNearest(byte[] a, byte[] b)
    {
        int n = Math.Min(a.Length, b.Length) / 3;
        int m = 0;
        for (int i = 0; i < n; i++)
        {
            int o = i * 3;
            if (NesPpu.NearestNesIndex(a[o], a[o + 1], a[o + 2]) ==
                NesPpu.NearestNesIndex(b[o], b[o + 1], b[o + 2]))
                m++;
        }
        return m;
    }

    private static string PixelReport(byte[] framed, byte[] shot, byte[] bg, NesPpu.SpriteLayer spr, string outDir, string captureDir)
    {
        var sb = new StringBuilder();
        const int n = NesPpu.Width * NesPpu.Height;
        int exact = 0, near = 0, skyExact = 0, skyNear = 0, skyN = 0, duckExact = 0, duckNear = 0, duckN = 0;
        var diff = new byte[n * 3];
        for (int i = 0; i < n; i++)
        {
            int o = i * 3;
            bool same = framed[o] == shot[o] && framed[o + 1] == shot[o + 1] && framed[o + 2] == shot[o + 2];
            if (same) exact++;
            int fi = NesPpu.NearestNesIndex(framed[o], framed[o + 1], framed[o + 2]);
            int si = NesPpu.NearestNesIndex(shot[o], shot[o + 1], shot[o + 2]);
            if (fi == si) near++;
            int y = i / NesPpu.Width;
            bool sky = y < 32;
            bool duckPix = spr.Rgba[i * 4 + 3] != 0;
            if (sky) { skyN++; if (same) skyExact++; if (fi == si) skyNear++; }
            if (duckPix) { duckN++; if (same) duckExact++; if (fi == si) duckNear++; }
            if (same)
            {
                diff[o] = framed[o];
                diff[o + 1] = framed[o + 1];
                diff[o + 2] = framed[o + 2];
            }
            else
            {
                diff[o] = 236;
                diff[o + 1] = 0;
                diff[o + 2] = 140;
            }
        }
        WriteBoth(outDir, captureDir, "gameA_vs_mesen_diff.png", Png.WriteRgb8(NesPpu.Width, NesPpu.Height, diff));
        sb.AppendLine("  pixel vs Mesen screenshot (256x240):");
        sb.AppendLine("    exact RGB:           " + exact + "/61440 (" + Pct(exact, n) + ")");
        sb.AppendLine("    nearest NES color #: " + near + "/61440 (" + Pct(near, n) + ") — palette-table vs CHR");
        sb.AppendLine("    sky rows 0-3 exact / nearest:  " + skyExact + "/" + skyN + "  /  " + skyNear + "/" + skyN);
        sb.AppendLine("    sprite-covered exact / nearest:" + duckExact + "/" + duckN + "  /  " + duckNear + "/" + duckN);
        if (exact < n)
        {
            sb.AppendLine("    unmatched pixels are magenta in gameA_vs_mesen_diff.png.");
            sb.AppendLine("    HYPOTHESIS if nearest-index match >> exact RGB: Mesen's 2C02 table differs from romconv's.");
            sb.AppendLine("    HYPOTHESIS if sprite pixels miss but BG matches: priority/Y/clip, not CHR tiles.");
        }
        int bgExact = CountExact(bg, shot);
        sb.AppendLine("    BG-only vs screenshot exact RGB: " + bgExact + "/61440 (sprites explain some of the rest).");
        return sb.ToString();
    }

    private static string Pct(int a, int b)
    {
        if (b <= 0) return "0%";
        return (a * 100.0 / b).ToString("0.00", CultureInfo.InvariantCulture) + "%";
    }

    private static string? FirstExisting(string dir, params string[] names)
    {
        foreach (string n in names)
        {
            string p = Path.Combine(dir, n);
            if (File.Exists(p)) return p;
        }
        return null;
    }

    private static byte[] TakeNametable0(byte[] raw)
    {
        var nt = new byte[1024];
        int n = Math.Min(1024, raw.Length);
        Buffer.BlockCopy(raw, 0, nt, 0, n);
        return nt;
    }

    private static int CountTile(byte[] nt, int row0, int rows, byte tile)
    {
        int c = 0;
        for (int r = row0; r < row0 + rows; r++)
            for (int x = 0; x < 32; x++)
                if (nt[r * 32 + x] == tile) c++;
        return c;
    }

    private static List<byte> UniqueTiles(byte[] nt, int row0, int rows)
    {
        var s = new SortedSet<byte>();
        for (int r = row0; r < row0 + rows; r++)
            for (int x = 0; x < 32; x++)
                s.Add(nt[r * 32 + x]);
        return s.ToList();
    }

    private static byte[] LoadTileMap(string path)
    {
        var nt = new byte[960];
        int i = 0;
        foreach (string line in File.ReadAllLines(path))
        {
            if (line.StartsWith('#') || string.IsNullOrWhiteSpace(line)) continue;
            foreach (string part in line.Split(' ', StringSplitOptions.RemoveEmptyEntries))
            {
                if (i >= 960) break;
                if (byte.TryParse(part, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out byte t))
                    nt[i++] = t;
            }
        }
        return nt;
    }

}
