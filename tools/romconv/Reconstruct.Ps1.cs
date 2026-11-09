using System.Globalization;
using System.Text;

namespace RomConv;

internal static partial class Reconstruct
{
    public const int HuntPs1Ctrl = 0x88;
    public const int DuckFrameLo = 0x00;
    public const int DuckFrameHi = 0x0B;
    public const int DogFrameLo = 0x0C;
    public const int DogFrameHi = 0x17;
    public const int DuckCellW = 40;
    public const int DuckCellH = 40;
    public const int DuckAtlasCols = 4;
    public const int DogCellW = 64;
    public const int DogCellH = 48;
    public const int DogAtlasCols = 4;
    public const int CursorTile = 0x26;

    /// <summary>
    /// Pack title + hunt BG, duck frames $00–$0B, dog frames $0C–$17, menu cursor.
    /// ROM streams only. PPUCTRL $88 (Mesen title + Game A). No capture files.
    /// </summary>
    public static string ExportHuntPs1(byte[] prg, byte[] chr, string outDir,
        bool previews = true, bool verifiedAssets = true)
    {
        var log = new StringBuilder();
        log.AppendLine();
        log.AppendLine("PS1 asset pack v3 (local ROM only; PPUCTRL $88)");
        Directory.CreateDirectory(outDir);

        byte[] palHunt = new byte[32];
        byte[] palTitle = new byte[32];
        bool palHuntOk = TryLoadIndexedPalette(prg, 2, palHunt, log, "hunt palette request 2 -> table slot 1");
        bool palTitleOk = TryLoadIndexedPalette(prg, 1, palTitle, log, "title palette request 1 -> table slot 0");
        if (!palHuntOk || !palTitleOk)
        {
            log.AppendLine("  FAIL: hunt or title palette stream missing.");
            File.WriteAllText(Path.Combine(outDir, "ps1pack_report.txt"), log.ToString(), new UTF8Encoding(false));
            return log.ToString();
        }

        var huntPpu = new byte[0x4000];
        var titlePpu = new byte[0x4000];
        ClearNametable(huntPpu, 0x2000, 0x24);
        ClearNametable(titlePpu, 0x2000, 0x24);
        if (!TryUploadPpuFile(prg, HuntScreenPrg, huntPpu, log, "hunt screen PRG $2042 / CPU $E042") ||
            !TryUploadPpuFile(prg, TitleScreenPrg, titlePpu, log, "title screen PRG $2404 / CPU $E404"))
        {
            log.AppendLine("  FAIL: nametable stream missing.");
            File.WriteAllText(Path.Combine(outDir, "ps1pack_report.txt"), log.ToString(), new UTF8Encoding(false));
            return log.ToString();
        }

        /* Hide the ROM-baked GAME A pointer; runtime draws tile $26 for all rows. */
        titlePpu[0x2000 + 17 * 32 + 6] = 0x24;
        StampCalibrationRow(titlePpu, log);
        /* v8: NES nametable stays authentic. CRT spacing is a PS1 display layer. */
        for (int r = 17; r <= 27; r++)
        {
            var rowHex = new StringBuilder();
            rowHex.Append("  title NT r" + r.ToString(CultureInfo.InvariantCulture) + ":");
            for (int c = 0; c < 32; c++)
                rowHex.Append(" " + titlePpu[0x2000 + r * 32 + c].ToString("X2", CultureInfo.InvariantCulture));
            log.AppendLine(rowHex.ToString());
        }

        byte[] sprPal = (byte[])palHunt.Clone();
        prg.AsSpan(0xe72c - 0xc000, 8).CopyTo(sprPal.AsSpan(16));

        int bgPat = NesPpu.BgPattern(HuntPs1Ctrl);
        int sprPat = NesPpu.SprPattern(HuntPs1Ctrl);
        log.AppendLine("  banks:       BG $" + bgPat.ToString("X4", CultureInfo.InvariantCulture) +
                       " SPR $" + sprPat.ToString("X4", CultureInfo.InvariantCulture) +
                       " from PPUCTRL $" + HuntPs1Ctrl.ToString("X2", CultureInfo.InvariantCulture) +
                       " (Mesen title frame 90 + Game A)");

        var huntClutNes = new List<byte>();
        var titleClutNes = new List<byte>();
        byte[] huntIdx = new byte[256 * 240];
        byte[] titleIdx = new byte[256 * 240];
        RasterNametable(chr, huntPpu, 0x2000, palHunt, bgPat, huntIdx, huntClutNes);
        RasterNametable(chr, titlePpu, 0x2000, palTitle, bgPat, titleIdx, titleClutNes);
        /* Mesen Game A HUD: ammo pal 1; HIT/score pal 3. HIT meter is 8px $D6/$D4 at $234C. */
        int[] hudTiles = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
            0xD9, 0xB7, 0xD6, 0xD7, 0xD5, 0xD4,
            0x1B, 0x18, 0x1E, 0x17, 0x0D,
            0x0A, 0x0E, 0x12, 0x16, 0x19, 0x1C, 0x1D };
        int[] hudPals  = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            3, 3, 3, 3, 3, 3, 3 };
        /* 256px wide so LoadImage RECT.w=64 (16-bit units). v6 used 168px → w=42,
         * which is not a multiple of 16 and hung GPU DMA on SCPH-5501. */
        const int hudW = 256, hudH = 8;
        byte[] hudIdx = new byte[hudW * hudH];
        for (int t = 0; t < hudTiles.Length; t++)
            RasterHudTile(chr, palHunt, bgPat, hudTiles[t], hudPals[t], huntClutNes, hudIdx, t * 8, 0, hudW);
        /* Runtime overlays must use the same CLUT indices as the baked hunt BG.
         * CHR+$0F atlas cells for $D9/$B7/$D6/$D7 read as white bars / "1"s on GPU. */
        /* Ammo $D9 at col 3. Flash/blank $B7 from the HIT spacer (col 11),
         * not from a pal-1 solid which rastered as a red rectangle.
         * White duck $D6 from col 13 (col 12 is the flashing slot in live NT). */
        CopyHuntCell(huntIdx, hudIdx, hudW, 3 * 8, 26 * 8, 10);
        CopyHuntCell(huntIdx, hudIdx, hudW, 11 * 8, 26 * 8, 11);
        CopyHuntCell(huntIdx, hudIdx, hudW, 13 * 8, 26 * 8, 12);
        CopyHuntCell(huntIdx, hudIdx, hudW, 12 * 8, 27 * 8, 13);
        {
            byte sky = (byte)IndexOf(huntClutNes, 0x0F);
            byte box = huntIdx[26 * 8 * 256 + 11 * 8];
            StampHudClear(hudIdx, hudW, 14, sky, box);
            StampHudClear(hudIdx, hudW, 15, sky, box);
        }
        log.AppendLine("  HUD overlay: $D9/$B7/$D6/$D7 from hunt NT; $D5/$D4 zeros from HIT spacer.");
        if (huntClutNes.Count > 16 || titleClutNes.Count > 16)
        {
            log.AppendLine("  FAIL: BG unique colors hunt=" + huntClutNes.Count + " title=" + titleClutNes.Count);
            File.WriteAllText(Path.Combine(outDir, "ps1pack_report.txt"), log.ToString(), new UTF8Encoding(false));
            return log.ToString();
        }

        ushort[] huntClut = ToClut16(huntClutNes, opaque: true);
        ushort[] titleClut = ToClut16(titleClutNes, opaque: true);

        var sprColors = new List<byte>();
        int duckAtlasW = DuckAtlasCols * DuckCellW;
        int duckAtlasH = 3 * DuckCellH;
        byte[] duckAtlas = new byte[duckAtlasW * duckAtlasH];
        int dogAtlasW = DogAtlasCols * DogCellW;
        int dogAtlasH = 3 * DogCellH;
        byte[] dogAtlas = new byte[dogAtlasW * dogAtlasH];
        int[] dogHotX = new int[DogFrameHi - DogFrameLo + 1];
        int[] dogHotY = new int[DogFrameHi - DogFrameLo + 1];

        for (int f = DuckFrameLo; f <= DuckFrameHi; f++)
        {
            int i = f - DuckFrameLo;
            int col = i % DuckAtlasCols;
            int row = i / DuckAtlasCols;
            if (!TryBuildDuckIndices(prg, chr, sprPal, sprPat, f,
                    out byte[] idx, out int w, out int h, out int bboxOx, out int bboxOy,
                    out List<byte> colors, out string note))
            {
                log.AppendLine("  duck $" + f.ToString("X2", CultureInfo.InvariantCulture) + " FAIL " + note);
                continue;
            }
            RemapIntoShared(idx, colors, sprColors);
            BlitCentered(idx, w, h, duckAtlas, duckAtlasW, DuckCellW, DuckCellH, col, row);
            log.AppendLine("  duck $" + f.ToString("X2", CultureInfo.InvariantCulture) + ": " + note);
            _ = bboxOx;
            _ = bboxOy;
        }

        for (int f = DogFrameLo; f <= DogFrameHi; f++)
        {
            int i = f - DogFrameLo;
            int col = i % DogAtlasCols;
            int row = i / DogAtlasCols;
            if (!TryBuildDuckIndices(prg, chr, sprPal, sprPat, f,
                    out byte[] idx, out int w, out int h, out int bboxOx, out int bboxOy,
                    out List<byte> colors, out string note))
            {
                log.AppendLine("  dog $" + f.ToString("X2", CultureInfo.InvariantCulture) + " FAIL " + note);
                continue;
            }
            RemapIntoShared(idx, colors, sprColors);
            BlitCentered(idx, w, h, dogAtlas, dogAtlasW, DogCellW, DogCellH, col, row);
            dogHotX[i] = (DogCellW - w) / 2 + bboxOx;
            dogHotY[i] = (DogCellH - h) / 2 + bboxOy;
            log.AppendLine("  dog $" + f.ToString("X2", CultureInfo.InvariantCulture) + ": " + note
                + " origin-in-cell " + dogHotX[i].ToString(CultureInfo.InvariantCulture) + ","
                + dogHotY[i].ToString(CultureInfo.InvariantCulture));
        }

        if (sprColors.Count > 15)
        {
            log.AppendLine("  FAIL: sprite unique colors " + sprColors.Count + " > 15");
            File.WriteAllText(Path.Combine(outDir, "ps1pack_report.txt"), log.ToString(), new UTF8Encoding(false));
            return log.ToString();
        }

        ushort[] sprClut = new ushort[16];
        sprClut[0] = 0x0000;
        for (int i = 0; i < sprColors.Count; i++)
            sprClut[i + 1] = ToRgb555(sprColors[i], opaque: true);

        byte[] cursorIdx = RasterCursor(chr, palTitle, bgPat, titleClutNes, log);
        log.AppendLine("  HUD tiles:   digits $00-$09, ammo $D9, blank $B7, white $D6/$D7, red $D4/$D5, ROUND $1B$18$1E$17$0D.");

        ushort[] sprBlue = (ushort[])sprClut.Clone();
        ushort[] sprRed = (ushort[])sprClut.Clone();
        RecolorClut(sprBlue, sprColors, 0x14, 0x26);
        RecolorClut(sprRed, sprColors, 0x14, 0x16);

        ushort[] huntTex = Pack4bpp(huntIdx, 256, 240);
        ushort[] titleTex = Pack4bpp(titleIdx, 256, 240);
        ushort[] duckTex = Pack4bpp(duckAtlas, duckAtlasW, duckAtlasH);
        ushort[] dogTex = Pack4bpp(dogAtlas, dogAtlasW, dogAtlasH);
        ushort[] cursorTex = Pack4bpp(cursorIdx, 8, 8);
        ushort[] hudTex = Pack4bpp(hudIdx, hudW, hudH);

        WritePs1C(outDir, huntClut, titleClut, sprClut, sprBlue, sprRed,
            huntTex, titleTex, duckTex, dogTex, cursorTex, hudTex,
            duckAtlasW, duckAtlasH, dogAtlasW, dogAtlasH, hudW, dogHotX, dogHotY);

        if (verifiedAssets)
        {
            WriteFlightTables(prg, outDir);
            WriteGameTables(prg, outDir);
            WriteScoreSprites(chr, outDir);
            WriteGameBanners(prg, chr, outDir);
            WriteClayBackground(prg, chr, outDir);
            WriteClaySprites(prg, chr, outDir);
            WriteClayHud(prg, chr, outDir);
        }
        if (previews)
        {
            File.WriteAllBytes(Path.Combine(outDir, "hunt_bg.png"), Png.WriteRgb8(256, 240, IndicesToRgb(huntIdx, 256, 240, huntClutNes, false)));
            File.WriteAllBytes(Path.Combine(outDir, "title_bg.png"), Png.WriteRgb8(256, 240, IndicesToRgb(titleIdx, 256, 240, titleClutNes, false)));
            File.WriteAllBytes(Path.Combine(outDir, "duck_atlas.png"), Png.WriteRgba8(duckAtlasW, duckAtlasH, DuckToRgba(duckAtlas, duckAtlasW, duckAtlasH, sprColors)));
            File.WriteAllBytes(Path.Combine(outDir, "dog_atlas.png"), Png.WriteRgba8(dogAtlasW, dogAtlasH, DuckToRgba(dogAtlas, dogAtlasW, dogAtlasH, sprColors)));
            File.WriteAllBytes(Path.Combine(outDir, "hud_tiles.png"), Png.WriteRgb8(hudW, 8, IndicesToRgb(hudIdx, hudW, 8, huntClutNes, false)));
        }
        log.AppendLine("  wrote:       hunt_ps1_assets.h / hunt_ps1_assets.c");
        log.AppendLine("  sprite NES colors: " + sprColors.Count);
        log.AppendLine("  source:      ROM title+hunt streams; FILE_01 hunt pal; title pal slot 0;");
        log.AppendLine("               Game A sprite pal $3F10 constants; anim table $DAC5; no capture files.");
        log.AppendLine("  CONFIRMED:   PPUCTRL $88 title (Mesen frame 90) and Game A hunt.");
        log.AppendLine("  CONFIRMED:   duck scripts $E705/$E70C/$E71D/$E713/$E718 (t=2,2,4 fly; t=20 hit; t=4,4 fall).");
        log.AppendLine("  CONFIRMED:   dog walk $278E frames $0C-$0F t=6 then $0C/$10 t=8; laugh $16/$17; hold $14/$15.");

        File.WriteAllText(Path.Combine(outDir, "ps1pack_report.txt"), log.ToString(), new UTF8Encoding(false));
        return log.ToString();
    }

    private static void CopyNtRow(byte[] ppu, int src, int dst)
    {
        for (int i = 0; i < 32; i++)
            ppu[0x2000 + dst * 32 + i] = ppu[0x2000 + src * 32 + i];
        for (int tx = 0; tx < 32; tx++)
        {
            int srcAttr = 0x2000 + 0x3C0 + (src / 4) * 8 + (tx / 4);
            int dstAttr = 0x2000 + 0x3C0 + (dst / 4) * 8 + (tx / 4);
            int srcShift = (tx / 2 % 2) * 2 + (src / 2 % 2) * 4;
            int dstShift = (tx / 2 % 2) * 2 + (dst / 2 % 2) * 4;
            int pal = (ppu[srcAttr] >> srcShift) & 3;
            ppu[dstAttr] = (byte)((ppu[dstAttr] & ~(3 << dstShift)) | (pal << dstShift));
        }
    }

    private static void ClearNtRow(byte[] ppu, int row, byte tile)
    {
        for (int i = 0; i < 32; i++)
            ppu[0x2000 + row * 32 + i] = tile;
    }

    private static void StampCalibrationRow(byte[] ppu, StringBuilder log)
    {
        /* Original: GAME C row 21, blank 22-23, TOP SCORE 24, blank 25, © 26.
         * Insert CALIBRATION on row 23 (same 8px gap as A/B/C). Shift TOP SCORE
         * to 25 and copyright to 27 so nothing overlaps. */
        CopyNtRow(ppu, 26, 27);
        ClearNtRow(ppu, 26, 0x24);
        CopyNtRow(ppu, 24, 25);
        ClearNtRow(ppu, 24, 0x24);

        byte[] tiles = { 0x0C, 0x0A, 0x15, 0x12, 0x0B, 0x1B, 0x0A, 0x1D, 0x12, 0x18, 0x17 };
        const int row = 23;
        const int col = 8;
        for (int i = 0; i < tiles.Length; i++)
            ppu[0x2000 + row * 32 + col + i] = tiles[i];
        for (int tx = col; tx < col + tiles.Length; tx++)
        {
            int dstAttr = 0x2000 + 0x3C0 + (row / 4) * 8 + (tx / 4);
            int dstShift = (tx / 2 % 2) * 2 + (row / 2 % 2) * 4;
            ppu[dstAttr] = (byte)((ppu[dstAttr] & ~(3 << dstShift)) | (3 << dstShift));
        }
        log.AppendLine("  title extra: CALIBRATION NT row 23; TOP SCORE -> 25; copyright -> 27.");
    }

    /* Move SCORE box 16 NES pixels left so the CRT overscan does not clip it. */
    private static void ShiftScorePanel(byte[] ppu, StringBuilder log)
    {
        const int src0 = 23;
        const int dst0 = 22;
        const int width = 8;
        for (int row = 25; row <= 29; row++)
        {
            byte[] tiles = new byte[width];
            int[] pal = new int[width];
            for (int i = 0; i < width; i++)
            {
                int sc = src0 + i;
                tiles[i] = ppu[0x2000 + row * 32 + sc];
                int attr = 0x2000 + 0x3C0 + (row / 4) * 8 + (sc / 4);
                int shift = (sc / 2 % 2) * 2 + (row / 2 % 2) * 4;
                pal[i] = (ppu[attr] >> shift) & 3;
            }
            byte fill = ppu[0x2000 + row * 32 + 31];
            for (int c = src0; c < 32; c++)
                ppu[0x2000 + row * 32 + c] = fill;
            for (int i = 0; i < width; i++)
            {
                int dc = dst0 + i;
                ppu[0x2000 + row * 32 + dc] = tiles[i];
                int attr = 0x2000 + 0x3C0 + (row / 4) * 8 + (dc / 4);
                int shift = (dc / 2 % 2) * 2 + (row / 2 % 2) * 4;
                ppu[attr] = (byte)((ppu[attr] & ~(3 << shift)) | (pal[i] << shift));
            }
        }
        log.AppendLine("  hunt extra:  SCORE CRT inset 8px (display cols 22-29; NES source 23-30).");
        log.AppendLine("  hunt extra:  16px overlapped HIT col 21; 8px is max full-panel inset.");
    }

    private static void CopyHuntCell(byte[] huntIdx, byte[] hudIdx, int hudW,
        int srcX, int srcY, int tile)
    {
        int dx = tile * 8;
        for (int row = 0; row < 8; row++)
        {
            int sy = (srcY + row) * 256 + srcX;
            int dy = row * hudW + dx;
            for (int col = 0; col < 8; col++)
                hudIdx[dy + col] = huntIdx[sy + col];
        }
    }

    /* RasterHudTile uses $0F for color 0. Replace those texels with the HIT
     * box black so $D5/$D4 do not flash sky-blue zeros on the CRT. */
    private static void StampHudClear(byte[] hudIdx, int hudW, int tile, byte sky, byte box)
    {
        int dx = tile * 8;
        for (int row = 0; row < 8; row++)
        {
            int dy = row * hudW + dx;
            for (int col = 0; col < 8; col++)
            {
                if (hudIdx[dy + col] == sky)
                    hudIdx[dy + col] = box;
            }
        }
    }

    /* Overlay tiles: NES color 0 is backdrop (sky). Painting sky into the black
     * HIT/SHOT boxes is what made ammo/HIT look wrong on the CRT. Use $0F. */
    private static void RasterHudTile(byte[] chr, byte[] pal32, int bgPat, int tile, int pal,
        List<byte> clut, byte[] dst, int dx, int dy, int dstW)
    {
        int tileOff = bgPat + (tile & 0xFF) * 16;
        for (int row = 0; row < 8; row++)
        {
            byte p0 = tileOff + 16 <= chr.Length ? chr[tileOff + row] : (byte)0;
            byte p1 = tileOff + 16 <= chr.Length ? chr[tileOff + 8 + row] : (byte)0;
            for (int col = 0; col < 8; col++)
            {
                int bit = 7 - col;
                int pix = ((p1 >> bit) & 1) << 1 | ((p0 >> bit) & 1);
                byte nes = pix == 0 ? (byte)0x0F : pal32[pal * 4 + pix];
                dst[(dy + row) * dstW + dx + col] = (byte)IndexOf(clut, nes);
            }
        }
    }

    private static void RasterChrTile(byte[] chr, byte[] pal32, int bgPat, int tile, int pal,
        List<byte> clut, byte[] dst, int dx, int dy, int dstW)
    {
        int tileOff = bgPat + (tile & 0xFF) * 16;
        for (int row = 0; row < 8; row++)
        {
            byte p0 = tileOff + 16 <= chr.Length ? chr[tileOff + row] : (byte)0;
            byte p1 = tileOff + 16 <= chr.Length ? chr[tileOff + 8 + row] : (byte)0;
            for (int col = 0; col < 8; col++)
            {
                int bit = 7 - col;
                int pix = ((p1 >> bit) & 1) << 1 | ((p0 >> bit) & 1);
                byte nes = pix == 0 ? pal32[0] : pal32[pal * 4 + pix];
                dst[(dy + row) * dstW + dx + col] = (byte)IndexOf(clut, nes);
            }
        }
    }

    private static void RecolorClut(ushort[] clut, List<byte> sprColors, byte fromNes, byte toNes)
    {
        for (int i = 0; i < sprColors.Count && i < 15; i++)
        {
            if (sprColors[i] == fromNes)
                clut[i + 1] = ToRgb555(toNes, opaque: true);
        }
    }

    private static byte[] RasterCursor(byte[] chr, byte[] pal32, int bgPat, List<byte> titleClut, StringBuilder log)
    {
        var idx = new byte[8 * 8];
        int tileOff = bgPat + CursorTile * 16;
        int pal = 2;
        for (int row = 0; row < 8; row++)
        {
            byte p0 = tileOff + 16 <= chr.Length ? chr[tileOff + row] : (byte)0;
            byte p1 = tileOff + 16 <= chr.Length ? chr[tileOff + 8 + row] : (byte)0;
            for (int col = 0; col < 8; col++)
            {
                int bit = 7 - col;
                int pix = ((p1 >> bit) & 1) << 1 | ((p0 >> bit) & 1);
                if (pix == 0)
                    continue;
                byte nes = pal32[pal * 4 + pix];
                int ci = titleClut.IndexOf(nes);
                idx[row * 8 + col] = (byte)(ci >= 0 ? ci : 1);
            }
        }
        log.AppendLine("  cursor:      BG tile $" + CursorTile.ToString("X2", CultureInfo.InvariantCulture) + " (title pointer)");
        return idx;
    }

    private static void RemapIntoShared(byte[] idx, List<byte> local, List<byte> shared)
    {
        for (int i = 0; i < idx.Length; i++)
        {
            if (idx[i] == 0)
                continue;
            byte nes = local[idx[i] - 1];
            idx[i] = (byte)(IndexOf(shared, nes) + 1);
        }
    }

    private static void BlitCentered(byte[] src, int sw, int sh, byte[] atlas, int aw, int cellW, int cellH, int col, int row)
    {
        int ox = col * cellW + (cellW - sw) / 2;
        int oy = row * cellH + (cellH - sh) / 2;
        for (int y = 0; y < sh; y++)
        {
            for (int x = 0; x < sw; x++)
            {
                byte p = src[y * sw + x];
                if (p == 0)
                    continue;
                int dx = ox + x;
                int dy = oy + y;
                if ((uint)dx >= (uint)aw || dy < 0 || dy >= atlas.Length / aw)
                    continue;
                atlas[dy * aw + dx] = p;
            }
        }
    }

    private static ushort[] ToClut16(List<byte> nes, bool opaque)
    {
        var clut = new ushort[16];
        for (int i = 0; i < 16; i++)
            clut[i] = i < nes.Count ? ToRgb555(nes[i], opaque) : (ushort)0x8000;
        return clut;
    }

    private static void RasterNametable(byte[] chr, byte[] ppu, int ntBase, byte[] pal32, int bgPat,
        byte[] dst, List<byte> clut)
    {
        for (int ty = 0; ty < 30; ty++)
        {
            for (int tx = 0; tx < 32; tx++)
            {
                int tile = ppu[ntBase + ty * 32 + tx];
                int attrAddr = ntBase + 0x3C0 + (ty / 4) * 8 + (tx / 4);
                byte attr = ppu[attrAddr];
                int shift = (tx / 2 % 2) * 2 + (ty / 2 % 2) * 4;
                int pal = (attr >> shift) & 3;
                int tileOff = bgPat + (tile & 0xFF) * 16;
                for (int row = 0; row < 8; row++)
                {
                    byte p0 = tileOff + 16 <= chr.Length ? chr[tileOff + row] : (byte)0;
                    byte p1 = tileOff + 16 <= chr.Length ? chr[tileOff + 8 + row] : (byte)0;
                    for (int col = 0; col < 8; col++)
                    {
                        int bit = 7 - col;
                        int pix = ((p1 >> bit) & 1) << 1 | ((p0 >> bit) & 1);
                        byte nes = pix == 0 ? pal32[0] : pal32[pal * 4 + pix];
                        dst[(ty * 8 + row) * 256 + (tx * 8 + col)] = (byte)IndexOf(clut, nes);
                    }
                }
            }
        }
    }

    private static bool TryBuildDuckIndices(byte[] prg, byte[] chr, byte[] pal32, int sprPat, int frame,
        out byte[] idx, out int w, out int h, out int bboxOx, out int bboxOy,
        out List<byte> colors, out string note, bool firstSpriteWins = false)
    {
        idx = Array.Empty<byte>();
        w = h = 32;
        bboxOx = bboxOy = 0;
        colors = new List<byte>();
        note = "";
        int table = AnimTablePrg + frame * 2;
        if (table + 1 >= prg.Length)
        {
            note = "table pointer out of range";
            return false;
        }
        int listCpu = prg[table] | (prg[table + 1] << 8);
        if (!CpuToPrg(listCpu, out int listOff))
        {
            note = "list not in PRG";
            return false;
        }
        var sprites = new List<Spr>();
        int cursor = listOff;
        int parts = 0;
        const int originX = 128, originY = 96;
        while (cursor < prg.Length)
        {
            byte hi = prg[cursor];
            if (hi == 0)
                break;
            if (cursor + 3 >= prg.Length)
            {
                note = "truncated part list";
                return false;
            }
            byte lo = prg[cursor + 1];
            sbyte yOff = (sbyte)prg[cursor + 2];
            sbyte xOff = (sbyte)prg[cursor + 3];
            int dataCpu = lo | (hi << 8);
            if (!CpuToPrg(dataCpu, out int dataOff))
            {
                note = "part data not in PRG";
                return false;
            }
            if (!DecodeFrameData(prg, dataOff, originX + xOff, originY + yOff, sprites, false, out string err))
            {
                note = err;
                return false;
            }
            parts++;
            cursor += 4;
            if (parts > 16)
                break;
        }
        for (int i = 0; i < sprites.Count; i++)
        {
            var s = sprites[i];
            if (MakeProper(s.Y, originY) == 0xFF || MakeProper(s.X, originX) == 0xFF)
            {
                s.Hidden = true;
                sprites[i] = s;
            }
        }
        var vis = sprites.Where(s => !s.Hidden).ToList();
        if (vis.Count == 0)
        {
            note = "no visible sprites";
            return false;
        }
        int minX = vis.Min(s => s.X);
        int minY = vis.Min(s => s.Y);
        int maxX = vis.Max(s => s.X + 8);
        int maxY = vis.Max(s => s.Y + 8);
        w = maxX - minX;
        h = maxY - minY;
        bboxOx = originX - minX;
        bboxOy = originY - minY;
        if (w <= 0 || h <= 0 || w > 64 || h > 64)
        {
            note = "bbox " + w + "x" + h + " out of range";
            return false;
        }
        if ((w & 3) != 0)
            w = (w + 3) & ~3;
        idx = new byte[w * h];
        if (firstSpriteWins)
            vis.Reverse();
        foreach (var s in vis)
        {
            bool flipH = (s.Attr & 0x40) != 0;
            bool flipV = (s.Attr & 0x80) != 0;
            int pal = 4 + (s.Attr & 3);
            int tileOff = sprPat + (s.Tile & 0xFF) * 16;
            for (int row = 0; row < 8; row++)
            {
                int srcRow = flipV ? 7 - row : row;
                byte p0 = tileOff + 16 <= chr.Length ? chr[tileOff + srcRow] : (byte)0;
                byte p1 = tileOff + 16 <= chr.Length ? chr[tileOff + 8 + srcRow] : (byte)0;
                for (int col = 0; col < 8; col++)
                {
                    int bit = flipH ? col : 7 - col;
                    int pix = ((p1 >> bit) & 1) << 1 | ((p0 >> bit) & 1);
                    if (pix == 0)
                        continue;
                    int px = s.X - minX + col;
                    int py = s.Y - minY + row;
                    if ((uint)px >= (uint)w || (uint)py >= (uint)h)
                        continue;
                    byte nes = pal32[pal * 4 + pix];
                    idx[py * w + px] = (byte)(IndexOf(colors, nes) + 1);
                }
            }
        }
        note = parts + " part(s), " + vis.Count + " sprite(s), " + w + "x" + h;
        return true;
    }

    private static int IndexOf(List<byte> clut, byte nes)
    {
        int i = clut.IndexOf(nes);
        if (i >= 0)
            return i;
        clut.Add(nes);
        return clut.Count - 1;
    }

    private static ushort ToRgb555(byte nes, bool opaque)
    {
        NesPpu.GetRgb(nes, out byte r, out byte g, out byte b);
        ushort c = (ushort)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
        if (opaque)
            c |= 0x8000;
        return c;
    }

    private static ushort[] Pack4bpp(byte[] pix, int w, int h)
    {
        var dst = new ushort[(w * h) / 4];
        for (int i = 0; i < w * h; i += 4)
            dst[i / 4] = (ushort)(pix[i] | (pix[i + 1] << 4) | (pix[i + 2] << 8) | (pix[i + 3] << 12));
        return dst;
    }

    private static byte[] IndicesToRgb(byte[] idx, int w, int h, List<byte> clut, bool skip0)
    {
        var rgb = new byte[w * h * 3];
        for (int i = 0; i < w * h; i++)
        {
            int ci = idx[i];
            if (skip0 && ci == 0)
                continue;
            if (ci >= clut.Count)
                ci = 0;
            NesPpu.GetRgb(clut[ci], out byte r, out byte g, out byte b);
            rgb[i * 3] = r;
            rgb[i * 3 + 1] = g;
            rgb[i * 3 + 2] = b;
        }
        return rgb;
    }

    private static byte[] DuckToRgba(byte[] idx, int w, int h, List<byte> colors)
    {
        var rgba = new byte[w * h * 4];
        for (int i = 0; i < idx.Length; i++)
        {
            int ci = idx[i];
            if (ci == 0)
                continue;
            byte nes = colors[ci - 1];
            NesPpu.GetRgb(nes, out byte r, out byte g, out byte b);
            rgba[i * 4] = r;
            rgba[i * 4 + 1] = g;
            rgba[i * 4 + 2] = b;
            rgba[i * 4 + 3] = 255;
        }
        return rgba;
    }

    private static void WriteFlightTables(byte[] prg, string dir)
    {
        var source = new StringBuilder("/* Generated from the user's local ROM. */\n");
        foreach (var (address, length) in new[] { (0xe64b, 22), (0xe661, 162), (0xea00, 32) })
        {
            source.AppendLine($"static const unsigned char k_{address:x4}[{length}] = {{");
            var table = prg.AsSpan(address - 0xc000, length);
            for (int i = 0; i < table.Length; i++)
            {
                if (i % 16 == 0)
                    source.Append("    ");
                source.Append($"0x{table[i]:X2},");
                if (i % 16 == 15 || i == table.Length - 1)
                    source.AppendLine();
            }
            source.AppendLine("};");
        }
        File.WriteAllText(Path.Combine(dir, "nes_fly_rom_data.h"), source.ToString(), new UTF8Encoding(false));
    }

    private static void WritePs1C(string dir,
        ushort[] huntClut, ushort[] titleClut, ushort[] sprClut, ushort[] sprBlue, ushort[] sprRed,
        ushort[] huntTex, ushort[] titleTex, ushort[] duckTex, ushort[] dogTex, ushort[] cursorTex, ushort[] hudTex,
        int duckW, int duckH, int dogW, int dogH, int hudW, int[] dogHotX, int[] dogHotY)
    {
        var h = new StringBuilder();
        h.AppendLine("/* Generated by romconv --ps1pack from a local Duck Hunt dump.");
        h.AppendLine(" * Do not commit this file. Nintendo graphics stay on the builder's machine. */");
        h.AppendLine("#ifndef HUNT_PS1_ASSETS_H");
        h.AppendLine("#define HUNT_PS1_ASSETS_H");
        h.AppendLine();
        h.AppendLine("#include <stdint.h>");
        h.AppendLine();
        h.AppendLine("#define HUNT_BG_W      256");
        h.AppendLine("#define HUNT_BG_H      240");
        h.AppendLine("#define HUNT_NES_X     32");
        h.AppendLine("#define HUNT_DUCK_CELL_W " + DuckCellW.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_DUCK_CELL_H " + DuckCellH.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_DUCK_COLS " + DuckAtlasCols.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_DUCK_ATLAS_W " + duckW.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_DUCK_ATLAS_H " + duckH.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_DUCK_HW   16");
        h.AppendLine("#define HUNT_DUCK_HH   16");
        h.AppendLine("#define HUNT_DUCK_W    HUNT_DUCK_CELL_W");
        h.AppendLine("#define HUNT_DUCK_H    HUNT_DUCK_CELL_H");
        h.AppendLine("#define HUNT_DOG_CELL_W " + DogCellW.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_DOG_CELL_H " + DogCellH.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_DOG_COLS " + DogAtlasCols.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_DOG_ATLAS_W " + dogW.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_DOG_ATLAS_H " + dogH.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_BG_TEX_N    ((HUNT_BG_W * HUNT_BG_H) / 4)");
        h.AppendLine("#define HUNT_DUCK_TEX_N  ((HUNT_DUCK_ATLAS_W * HUNT_DUCK_ATLAS_H) / 4)");
        h.AppendLine("#define HUNT_DOG_TEX_N   ((HUNT_DOG_ATLAS_W * HUNT_DOG_ATLAS_H) / 4)");
        h.AppendLine("#define HUNT_HUD_W       " + hudW.ToString(CultureInfo.InvariantCulture));
        h.AppendLine("#define HUNT_HUD_H       8");
        h.AppendLine("#define HUNT_HUD_TEX_N   ((HUNT_HUD_W * HUNT_HUD_H) / 4)");
        h.AppendLine("#define HUNT_HUD_AMMO    10");
        h.AppendLine("#define HUNT_HUD_BLANK   11");
        h.AppendLine("#define HUNT_HUD_WDUCK_T 12");
        h.AppendLine("#define HUNT_HUD_WDUCK_B 13");
        h.AppendLine("#define HUNT_HUD_RDUCK_T 14");
        h.AppendLine("#define HUNT_HUD_RDUCK_B 15");
        h.AppendLine("#define HUNT_HUD_R       16");
        h.AppendLine("#define HUNT_HUD_O       17");
        h.AppendLine("#define HUNT_HUD_U       18");
        h.AppendLine("#define HUNT_HUD_N       19");
        h.AppendLine("#define HUNT_HUD_D       20");
        h.AppendLine("#define HUNT_HUD_A       21");
        h.AppendLine("#define HUNT_HUD_E       22");
        h.AppendLine("#define HUNT_HUD_I       23");
        h.AppendLine("#define HUNT_HUD_M       24");
        h.AppendLine("#define HUNT_HUD_P       25");
        h.AppendLine("#define HUNT_HUD_S       26");
        h.AppendLine("#define HUNT_HUD_T       27");
        h.AppendLine("#define HUNT_DOG_FRAMES  12");
        h.AppendLine();
        h.AppendLine("extern const uint16_t hunt_bg_clut[16];");
        h.AppendLine("extern const uint16_t hunt_title_clut[16];");
        h.AppendLine("extern const uint16_t hunt_spr_clut[16];");
        h.AppendLine("extern const uint16_t hunt_spr_clut_blue[16];");
        h.AppendLine("extern const uint16_t hunt_spr_clut_red[16];");
        h.AppendLine("extern const uint16_t hunt_bg_tex[HUNT_BG_TEX_N];");
        h.AppendLine("extern const uint16_t hunt_title_tex[HUNT_BG_TEX_N];");
        h.AppendLine("extern const uint16_t hunt_duck_tex[HUNT_DUCK_TEX_N];");
        h.AppendLine("extern const uint16_t hunt_dog_tex[HUNT_DOG_TEX_N];");
        h.AppendLine("extern const uint16_t hunt_cursor_tex[16];");
        h.AppendLine("extern const uint16_t hunt_hud_tex[HUNT_HUD_TEX_N];");
        h.AppendLine("extern const int hunt_dog_hot_x[HUNT_DOG_FRAMES];");
        h.AppendLine("extern const int hunt_dog_hot_y[HUNT_DOG_FRAMES];");
        h.AppendLine();
        h.AppendLine("#endif");
        File.WriteAllText(Path.Combine(dir, "hunt_ps1_assets.h"), h.ToString(), new UTF8Encoding(false));

        var c = new StringBuilder();
        c.AppendLine("/* Generated by romconv --ps1pack. Gitignored. */");
        c.AppendLine("#include \"hunt_ps1_assets.h\"");
        c.AppendLine();
        WriteU16Array(c, "hunt_bg_clut", huntClut);
        WriteU16Array(c, "hunt_title_clut", titleClut);
        WriteU16Array(c, "hunt_spr_clut", sprClut);
        WriteU16Array(c, "hunt_spr_clut_blue", sprBlue);
        WriteU16Array(c, "hunt_spr_clut_red", sprRed);
        WriteU16Array(c, "hunt_bg_tex", huntTex);
        WriteU16Array(c, "hunt_title_tex", titleTex);
        WriteU16Array(c, "hunt_duck_tex", duckTex);
        WriteU16Array(c, "hunt_dog_tex", dogTex);
        WriteU16Array(c, "hunt_cursor_tex", cursorTex);
        WriteU16Array(c, "hunt_hud_tex", hudTex);
        WriteI32Array(c, "hunt_dog_hot_x", dogHotX);
        WriteI32Array(c, "hunt_dog_hot_y", dogHotY);
        File.WriteAllText(Path.Combine(dir, "hunt_ps1_assets.c"), c.ToString(), new UTF8Encoding(false));
    }

    private static void WriteI32Array(StringBuilder c, string name, int[] data)
    {
        c.Append("const int ");
        c.Append(name);
        c.Append('[');
        c.Append(data.Length.ToString(CultureInfo.InvariantCulture));
        c.AppendLine("] = {");
        c.Append("\t");
        for (int i = 0; i < data.Length; i++)
        {
            if (i > 0)
                c.Append(", ");
            c.Append(data[i].ToString(CultureInfo.InvariantCulture));
        }
        c.AppendLine();
        c.AppendLine("};");
        c.AppendLine();
    }

    private static void WriteU16Array(StringBuilder c, string name, ushort[] data)
    {
        c.Append("const uint16_t ");
        c.Append(name);
        c.Append('[');
        c.Append(data.Length.ToString(CultureInfo.InvariantCulture));
        c.AppendLine("] = {");
        for (int i = 0; i < data.Length; i++)
        {
            if (i % 12 == 0)
                c.Append("\t");
            c.Append("0x");
            c.Append(data[i].ToString("X4", CultureInfo.InvariantCulture));
            if (i + 1 < data.Length)
                c.Append(',');
            if (i % 12 == 11 || i + 1 == data.Length)
                c.AppendLine();
            else
                c.Append(' ');
        }
        c.AppendLine("};");
        c.AppendLine();
    }
}
