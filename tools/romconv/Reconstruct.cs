using System.Globalization;
using System.Text;

namespace RomConv;

/// <summary>
/// Local diagnostic reconstruction. Reads the user ROM at known PRG offsets
/// verified against the CPU upload/animation routines. Does not embed CHR/PRG bytes.
/// </summary>
internal static partial class Reconstruct
{
    // CPU $C000 mirror of 16KiB PRG. Offsets below are file-relative inside PRG.
    private const int AnimTablePrg = 0x1AC5;
    private const int SkeletonPrg = 0x1CC0;
    private const int HuntScreenPrg = 0x2042;
    private const int TitleScreenPrg = 0x2404;
    private const int PalettePtrPrg = 0x2532;
    private const int AnimFrameCount = 0x25;
    // Game A hunt frame: Mesen 2.1.1 capture, PPUCTRL $88.
    private const int HuntGameACtrl = 0x88;
    // Reset path PRG $0049 LDA #$90 STA $2000 — not confirmed for the title screen.
    private const int ResetPpuCtrl = 0x90;

    public static string Run(byte[] prg, byte[] chr, string outDir)
    {
        var log = new StringBuilder();
        log.AppendLine();
        log.AppendLine("Reconstruction (local diagnostics)");
        Directory.CreateDirectory(outDir);

        byte[] palHunt = new byte[32];
        byte[] palTitle = new byte[32];
        // NMI: DEY then ASL, so request flag N loads pointer (N-1).
        bool palHuntOk = TryLoadIndexedPalette(prg, 2, palHunt, log, "hunt palette request 2 -> table slot 1");
        bool palTitleOk = TryLoadIndexedPalette(prg, 1, palTitle, log, "title palette request 1 -> table slot 0");
        byte[] palSlot2 = new byte[32];
        bool palSlot2Ok = TryLoadIndexedPalette(prg, 3, palSlot2, log, "UNVERIFIED alt palette request 3 -> table slot 2");

        var huntPpu = new byte[0x4000];
        var titlePpu = new byte[0x4000];
        Array.Fill(huntPpu, (byte)0x00);
        Array.Fill(titlePpu, (byte)0x00);
        // SCRIPT_HELP_CLEAR_BOTH_SCREENS: fill NT $2000/$2400 with tile $24, attributes 0.
        ClearNametable(huntPpu, 0x2000, 0x24);
        ClearNametable(titlePpu, 0x2000, 0x24);
        ClearNametable(titlePpu, 0x2400, 0x24);

        bool huntOk = TryUploadPpuFile(prg, HuntScreenPrg, huntPpu, log, "hunt screen PRG $2042 / CPU $E042");
        bool titleOk = TryUploadPpuFile(prg, TitleScreenPrg, titlePpu, log, "title screen PRG $2404 / CPU $E404");

        int huntBgPat = NesPpu.BgPattern(HuntGameACtrl);
        int huntSprPat = NesPpu.SprPattern(HuntGameACtrl);
        int titleBgPat = NesPpu.BgPattern(HuntGameACtrl);
        log.AppendLine("  PPUCTRL:      hunt Game A uses $" + HuntGameACtrl.ToString("X2", CultureInfo.InvariantCulture) +
                       " (Mesen-confirmed): BG $" + huntBgPat.ToString("X4", CultureInfo.InvariantCulture) +
                       " SPR $" + huntSprPat.ToString("X4", CultureInfo.InvariantCulture) + ", 8x8.");
        log.AppendLine("                reset LDA #$90 STA $2000 at PRG $0049 is not the live hunt value.");
        log.AppendLine("                title uses $" + HuntGameACtrl.ToString("X2", CultureInfo.InvariantCulture) +
                       " (Mesen 2.1.1 title frame 90 CONFIRMED).");

        if (huntOk)
        {
            byte[] img = RenderNametable(chr, huntPpu, 0x2000, palHuntOk ? palHunt : GrayscalePal(), huntBgPat);
            File.WriteAllBytes(Path.Combine(outDir, "bg_hunt_scene.png"), Png.WriteRgb8(256, 240, img));
            log.AppendLine("  wrote:        bg_hunt_scene.png (NT $2000 + FILE_01, BG pattern from PPUCTRL $88)");
            log.AppendLine("  sky:          rows 0–3 stay clear tile $24. Fetched from CHR $1000 this is blank sky.");
            log.AppendLine("                Diagonals were $24 from CHR $0000 under the old $90 assumption.");
            File.WriteAllText(
                Path.Combine(outDir, "bg_hunt_scene_tiles.txt"),
                DumpNametable(huntPpu, 0x2000),
                new UTF8Encoding(false));
            File.WriteAllText(
                Path.Combine(outDir, "bg_hunt_scene_attr.txt"),
                DumpAttr(huntPpu, 0x2000),
                new UTF8Encoding(false));
            File.WriteAllBytes(Path.Combine(outDir, "bg_hunt_attr_map.png"), RenderAttrMap(huntPpu, 0x2000));
            File.WriteAllBytes(Path.Combine(outDir, "bg_hunt_clear_leftover.png"), RenderLeftover(huntPpu, 0x2000, 0x24));
            log.AppendLine("  wrote:        bg_hunt_attr_map.png (palette index per 16×16) and bg_hunt_clear_leftover.png (cells still $24)");
            if (palSlot2Ok)
            {
                byte[] alt = RenderNametable(chr, huntPpu, 0x2000, palSlot2, huntBgPat);
                File.WriteAllBytes(Path.Combine(outDir, "bg_hunt_scene_palette_slot2_UNVERIFIED.png"), Png.WriteRgb8(256, 240, alt));
                log.AppendLine("  wrote:        bg_hunt_scene_palette_slot2_UNVERIFIED.png (not selected by NMI flag math)");
            }
        }
        else
            log.AppendLine("  hunt scene:   NOT reconstructed (PPU stream decode failed).");

        if (titleOk)
        {
            byte[] img = RenderNametable(chr, titlePpu, 0x2000, palTitleOk ? palTitle : GrayscalePal(), titleBgPat);
            File.WriteAllBytes(Path.Combine(outDir, "bg_title_nt2000_diag.png"), Png.WriteRgb8(256, 240, img));
            byte[] img24 = RenderNametable(chr, titlePpu, 0x2400, palTitleOk ? palTitle : GrayscalePal(), titleBgPat);
            File.WriteAllBytes(Path.Combine(outDir, "bg_title_nt2400_diag.png"), Png.WriteRgb8(256, 240, img24));
            log.AppendLine("  wrote:        bg_title_nt2000_diag.png and bg_title_nt2400_diag.png");
        }

        string framesDir = Path.Combine(outDir, "frames");
        Directory.CreateDirectory(framesDir);
        int decodedFrames = 0;
        var frameNotes = new StringBuilder();
        for (int f = 0; f < AnimFrameCount; f++)
        {
            if (!TryRenderAnimFrame(prg, chr, palHuntOk ? palHunt : GrayscalePal(), f, framesDir, huntSprPat, out string note))
            {
                frameNotes.AppendLine("  frame $" + f.ToString("X2", CultureInfo.InvariantCulture) + ": " + note);
                continue;
            }
            decodedFrames++;
            frameNotes.AppendLine("  frame $" + f.ToString("X2", CultureInfo.InvariantCulture) + ": " + note);
        }

        log.AppendLine("  anim table:   PRG $1AC5 / CPU $DAC5, " + AnimFrameCount + " slot(s), " + decodedFrames + " decoded.");
        log.AppendLine("  skeletons:    PRG $1CC0, first byte added to Y, second to X (STA $0200 then X).");
        log.AppendLine("  H-flip:       ROM $10D6 LDX direction / BEQ skip; facing right uses FD attr as stored.");
        log.AppendLine("  MAKE_*_PROPER: hide sprite if |sprite-origin| >= $30 (PRG $1117).");
        log.Append(frameNotes);

        byte[] duckPal = palHuntOk ? (byte[])palHunt.Clone() : GrayscalePal();
        ApplyLut(prg, 0x2724, duckPal, 16, 8);
        log.AppendLine("  duck pal:     LUT PRG $2724 color 0 written to $3F10 (same path as DUCK_COLOR_DATA_TO_RAM_PALETTE, Y=0).");

        string duckDir = Path.Combine(outDir, "gameA_duck");
        Directory.CreateDirectory(duckDir);
        var scripts = new (string Name, int Cpu, string Role)[]
        {
            ("E705", 0xE705, "OBJ_ANIM pointer 2: CMP dir < 3 or >= 6 (right/left set 2)"),
            ("E70C", 0xE70C, "OBJ_ANIM pointer 1: AND #7 == 0"),
            ("E71D", 0xE71D, "OBJ_ANIM pointer 3: dir values 3,4,5 / 11 / 15"),
            ("E713", 0xE713, "TARGET_STATE_4 hit/shocked"),
            ("E718", 0xE718, "TARGET_STATE_6 falling"),
        };
        var confirmed = new SortedSet<int>();
        foreach (var s in scripts)
        {
            int[] frames = ParseAnimScript(prg, s.Cpu, log, s.Name, s.Role);
            foreach (int f in frames)
                confirmed.Add(f);
        }
        foreach (int f in confirmed)
        {
            if (TryRenderAnimFrame(prg, chr, duckPal, f, duckDir, huntSprPat, out string note, originX: 128, originY: 96, applyProper: true, facingLeft: false))
            {
                log.AppendLine("  gameA duck:   frame $" + f.ToString("X2", CultureInfo.InvariantCulture) + " " + note);
                string src = Path.Combine(duckDir, "frame_" + f.ToString("X2", CultureInfo.InvariantCulture) + ".png");
                if (File.Exists(src) && (f == 0x03 || f == 0x06 || f == 0x0A))
                    File.Copy(src, Path.Combine(outDir, "gameA_duck_frame_" + f.ToString("X2", CultureInfo.InvariantCulture) + ".png"), true);
            }
        }

        byte[] atlasPal = palHuntOk ? palHunt : GrayscalePal();
        File.WriteAllBytes(Path.Combine(outDir, "chr_sprite_bank_atlas.png"), BuildPatternAtlas(chr, huntSprPat, atlasPal, 5));
        File.WriteAllBytes(Path.Combine(outDir, "chr_bg_bank_atlas.png"), BuildPatternAtlas(chr, huntBgPat, atlasPal, 0));
        log.AppendLine("  wrote:        chr_sprite_bank_atlas.png (sprite pattern $" +
                       huntSprPat.ToString("X4", CultureInfo.InvariantCulture) + ", 256 tiles, sprite pal 1)");
        log.AppendLine("  wrote:        chr_bg_bank_atlas.png (BG pattern $" +
                       huntBgPat.ToString("X4", CultureInfo.InvariantCulture) + ", 256 tiles, BG pal 0)");

        File.WriteAllText(Path.Combine(outDir, "reconstruct_report.txt"), log.ToString(), new UTF8Encoding(false));
        return log.ToString();
    }

    private static void ClearNametable(byte[] ppu, int ntBase, byte tile)
    {
        for (int i = 0; i < 960; i++)
            ppu[ntBase + i] = tile;
        for (int i = 0; i < 64; i++)
            ppu[ntBase + 0x3C0 + i] = 0;
    }

    private static byte[] BuildPatternAtlas(byte[] chr, int patternBase, byte[] pal32, int palette)
    {
        const int cols = 16;
        const int rows = 16;
        var rgb = new byte[cols * 8 * rows * 8 * 3];
        for (int t = 0; t < 256; t++)
        {
            int tx = t % cols;
            int ty = t / cols;
            NesPpu.DrawTile(chr, patternBase, t, pal32, palette, false, false, tx * 8, ty * 8, cols * 8, rgb, null, null);
        }
        return Png.WriteRgb8(cols * 8, rows * 8, rgb);
    }

    private static byte[] GrayscalePal()
    {
        var p = new byte[32];
        for (int i = 0; i < 8; i++)
        {
            p[i * 4 + 0] = 0x0F;
            p[i * 4 + 1] = 0x00;
            p[i * 4 + 2] = 0x10;
            p[i * 4 + 3] = 0x30;
        }
        return p;
    }

    private static bool TryLoadIndexedPalette(byte[] prg, int requestFlag, byte[] dest32, StringBuilder log, string label)
    {
        if (requestFlag <= 0)
        {
            log.AppendLine("  " + label + ": request flag 0 means no upload.");
            return false;
        }
        int ptrOff = PalettePtrPrg + (requestFlag - 1) * 2;
        if (ptrOff + 1 >= prg.Length)
        {
            log.AppendLine("  " + label + ": pointer out of range.");
            return false;
        }
        int cpu = prg[ptrOff] | (prg[ptrOff + 1] << 8);
        if (!CpuToPrg(cpu, out int palOff))
        {
            log.AppendLine("  " + label + ": pointer $" + cpu.ToString("X4", CultureInfo.InvariantCulture) + " not in PRG.");
            return false;
        }
        var ppu = new byte[0x4000];
        if (!TryUploadPpuFile(prg, palOff, ppu, log, label + " stream PRG $" + palOff.ToString("X4", CultureInfo.InvariantCulture)))
            return false;
        Buffer.BlockCopy(ppu, 0x3F00, dest32, 0, 32);
        log.AppendLine("  " + label + ": loaded from CPU $" + cpu.ToString("X4", CultureInfo.InvariantCulture)
                       + " / PRG $" + palOff.ToString("X4", CultureInfo.InvariantCulture));
        return true;
    }

    private static bool TryUploadPpuFile(byte[] prg, int start, byte[] ppu, StringBuilder log, string label)
    {
        if (start < 0 || start >= prg.Length)
        {
            log.AppendLine("  " + label + ": start out of range.");
            return false;
        }
        int p = start;
        int cmds = 0;
        try
        {
            while (p < prg.Length)
            {
                int y = 0;
                byte hi = prg[p + y];
                if (hi == 0)
                {
                    if (cmds == 0)
                    {
                        log.AppendLine("  " + label + ": empty stream.");
                        return false;
                    }
                    log.AppendLine("  " + label + ": decoded " + cmds + " PPU command(s), end at PRG $"
                                   + (p + 1).ToString("X4", CultureInfo.InvariantCulture));
                    return true;
                }
                y++;
                if (p + y >= prg.Length) break;
                byte lo = prg[p + y];
                y++;
                if (p + y >= prg.Length) break;
                byte setting = prg[p + y];
                bool inc32 = (setting & 0x80) != 0;
                bool repeat = (setting & 0x40) != 0;
                int count = setting & 0x3F;
                if (count == 0)
                {
                    log.AppendLine("  " + label + ": command count 0 (DEX would wrap); aborting.");
                    return false;
                }
                if (repeat)
                    y++;
                int addr = (hi << 8) | lo;
                for (int n = 0; n < count; n++)
                {
                    if (!repeat)
                        y++;
                    if (p + y >= prg.Length)
                    {
                        log.AppendLine("  " + label + ": truncated command.");
                        return false;
                    }
                    WritePpu(ppu, addr, prg[p + y]);
                    addr = (addr + (inc32 ? 32 : 1)) & 0x3FFF;
                }
                p = p + y + 1;
                cmds++;
                if (cmds > 512)
                {
                    log.AppendLine("  " + label + ": too many commands; aborting.");
                    return false;
                }
            }
        }
        catch (Exception ex)
        {
            log.AppendLine("  " + label + ": " + ex.Message);
            return false;
        }
        log.AppendLine("  " + label + ": unterminated stream.");
        return false;
    }

    private static void WritePpu(byte[] ppu, int addr, byte val)
    {
        addr &= 0x3FFF;
        ppu[addr] = val;
        if (addr >= 0x3F00 && addr < 0x3F20)
        {
            // $3F10/$3F14/$3F18/$3F1C mirrors of $3F00/$3F04/$3F08/$3F0C
            if ((addr & 3) == 0)
            {
                int baseAddr = 0x3F00 | (addr & 0x0C);
                ppu[baseAddr] = val;
                ppu[baseAddr | 0x10] = val;
            }
        }
    }

    private static byte[] RenderNametable(byte[] chr, byte[] ppu, int ntBase, byte[] pal32, int bgPattern)
    {
        var nt = new byte[1024];
        Buffer.BlockCopy(ppu, ntBase, nt, 0, 1024);
        return NesPpu.RenderNametable(chr, nt, pal32, bgPattern, null);
    }

    private static string DumpNametable(byte[] ppu, int ntBase)
    {
        var sb = new StringBuilder();
        sb.AppendLine("# Nametable tile indices (hex), 32 columns. Local diagnostic only.");
        for (int ty = 0; ty < 30; ty++)
        {
            for (int tx = 0; tx < 32; tx++)
            {
                if (tx > 0) sb.Append(' ');
                sb.Append(ppu[ntBase + ty * 32 + tx].ToString("X2", CultureInfo.InvariantCulture));
            }
            sb.AppendLine();
        }
        return sb.ToString();
    }

    private static bool TryRenderAnimFrame(
        byte[] prg, byte[] chr, byte[] pal32, int frame, string framesDir, int sprPattern, out string note,
        int originX = 0, int originY = 0, bool applyProper = false, bool facingLeft = false)
    {
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
            note = "list CPU $" + listCpu.ToString("X4", CultureInfo.InvariantCulture) + " not in PRG";
            return false;
        }

        var sprites = new List<Spr>();
        int cursor = listOff;
        int parts = 0;
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
                note = "part data CPU $" + dataCpu.ToString("X4", CultureInfo.InvariantCulture) + " not in PRG";
                return false;
            }
            int partY = originY + yOff;
            int partX = originX + xOff;
            if (facingLeft)
            {
                partX = originX - xOff - 8;
            }
            if (!DecodeFrameData(prg, dataOff, partX, partY, sprites, facingLeft, out string err))
            {
                note = err;
                return false;
            }
            parts++;
            cursor += 4;
            if (parts > 16)
            {
                note = "too many parts";
                return false;
            }
        }

        if (sprites.Count == 0)
        {
            note = "list at PRG $" + listOff.ToString("X4", CultureInfo.InvariantCulture) + " produced no sprites";
            return false;
        }

        if (applyProper)
        {
            for (int i = 0; i < sprites.Count; i++)
            {
                var s = sprites[i];
                if (MakeProper(s.Y, originY) == 0xFF || MakeProper(s.X, originX) == 0xFF)
                {
                    s.Hidden = true;
                    sprites[i] = s;
                }
            }
        }

        var vis = sprites.Where(s => !s.Hidden).ToList();
        if (vis.Count == 0)
        {
            note = "all sprites clipped by MAKE_A_PROPER";
            return false;
        }

        int minX = vis.Min(s => s.X);
        int minY = vis.Min(s => s.Y);
        int maxX = vis.Max(s => s.X + 8);
        int maxY = vis.Max(s => s.Y + 8);
        int w = Math.Max(8, maxX - minX);
        int h = Math.Max(8, maxY - minY);
        var rgba = new byte[w * h * 4];

        foreach (var s in vis)
            DrawSprite(chr, pal32, sprPattern, s, minX, minY, w, rgba);

        string name = "frame_" + frame.ToString("X2", CultureInfo.InvariantCulture) + ".png";
        File.WriteAllBytes(Path.Combine(framesDir, name), Png.WriteRgba8(w, h, rgba));
        note = parts + " part(s), " + vis.Count + " sprite(s), "
               + w + "x" + h + ", list PRG $" + listOff.ToString("X4", CultureInfo.InvariantCulture)
               + " -> " + name;
        return true;
    }

    private static int MakeProper(int spritePos, int origin)
    {
        int a = spritePos;
        int d = a - origin;
        if (d < 0)
            d = -d;
        if (d >= 0x30)
            return 0xFF;
        return a & 0xFF;
    }

    private static bool DecodeFrameData(
        byte[] prg, int dataOff, int originX, int originY, List<Spr> sprites, bool facingLeft, out string err)
    {
        err = "";
        if (dataOff >= prg.Length)
        {
            err = "frame data out of range";
            return false;
        }
        int y = 0;
        byte group = prg[dataOff + y];
        int skel = group switch
        {
            0 => 0x00,
            1 => 0x10,
            2 => 0x38,
            _ => 0x44
        };
        byte attr = 0;
        y++;
        int tiles = 0;
        while (dataOff + y < prg.Length)
        {
            byte b = prg[dataOff + y];
            if (b == 0xFF)
                return true;
            if (b == 0xFE)
            {
                skel += 2;
                y++;
                continue;
            }
            if (b == 0xFD)
            {
                y++;
                if (dataOff + y >= prg.Length)
                {
                    err = "truncated FD attribute";
                    return false;
                }
                attr = prg[dataOff + y];
                y++;
                continue;
            }
            if (SkeletonPrg + skel + 1 >= prg.Length)
            {
                err = "skeleton index out of range";
                return false;
            }
            int sy = (sbyte)prg[SkeletonPrg + skel];
            skel++;
            int sx = (sbyte)prg[SkeletonPrg + skel];
            skel++;
            if (facingLeft)
                sx = -sx;
            int sprAttr = attr;
            if (facingLeft)
                sprAttr = (sprAttr & 0xBF) | ((sprAttr ^ 0x40) & 0x40);
            sprites.Add(new Spr
            {
                X = originX + sx,
                Y = originY + sy,
                Tile = b,
                Attr = sprAttr
            });
            tiles++;
            y++;
            if (tiles > 64)
            {
                err = "too many tiles in frame data";
                return false;
            }
        }
        err = "unterminated frame data";
        return false;
    }

    private static void DrawSprite(byte[] chr, byte[] pal32, int sprPattern, Spr s, int ox, int oy, int w, byte[] rgba)
    {
        int pal = s.Attr & 3;
        bool flipH = (s.Attr & 0x40) != 0;
        bool flipV = (s.Attr & 0x80) != 0;
        NesPpu.DrawTile(chr, sprPattern, s.Tile, pal32, pal + 4, flipH, flipV, s.X - ox, s.Y - oy, w, null, rgba, null);
    }

    private static bool CpuToPrg(int cpu, out int off)
    {
        off = 0;
        if (cpu >= 0xC000)
        {
            off = cpu - 0xC000;
            return off >= 0 && off < 0x4000;
        }
        if (cpu >= 0x8000)
        {
            off = cpu - 0x8000;
            return off >= 0 && off < 0x4000;
        }
        return false;
    }

    private struct Spr
    {
        public int X, Y, Tile, Attr;
        public bool Hidden;
    }

    private static void ApplyLut(byte[] prg, int lutOff, byte[] pal32, int dest, int len)
    {
        if (lutOff + len > prg.Length || dest + len > pal32.Length)
            return;
        Buffer.BlockCopy(prg, lutOff, pal32, dest, len);
    }

    private static int[] ParseAnimScript(byte[] prg, int cpu, StringBuilder log, string name, string role)
    {
        var frames = new List<int>();
        if (!CpuToPrg(cpu, out int off))
        {
            log.AppendLine("  script " + name + ": CPU $" + cpu.ToString("X4", CultureInfo.InvariantCulture) + " not in PRG");
            return Array.Empty<int>();
        }
        int i = off;
        var desc = new StringBuilder();
        for (int n = 0; n < 32 && i < prg.Length; n++)
        {
            byte b = prg[i];
            if (b >= 0xFA)
            {
                desc.Append(" cmd=$" + b.ToString("X2", CultureInfo.InvariantCulture));
                break;
            }
            if (i + 1 >= prg.Length)
                break;
            int frame = prg[i + 1];
            frames.Add(frame);
            if (desc.Length > 0)
                desc.Append(", ");
            desc.Append("t=" + b + "/f=$" + frame.ToString("X2", CultureInfo.InvariantCulture));
            i += 2;
        }
        log.AppendLine("  script " + name + " PRG $" + off.ToString("X4", CultureInfo.InvariantCulture)
                       + " (" + role + "): " + desc);
        return frames.Distinct().ToArray();
    }

    private static string DumpAttr(byte[] ppu, int ntBase)
    {
        var sb = new StringBuilder();
        sb.AppendLine("# Attribute bytes 8x8 at NT+0x3C0. Each nybble pair is pal 0-3 for a 16×16.");
        for (int row = 0; row < 8; row++)
        {
            for (int col = 0; col < 8; col++)
            {
                if (col > 0) sb.Append(' ');
                sb.Append(ppu[ntBase + 0x3C0 + row * 8 + col].ToString("X2", CultureInfo.InvariantCulture));
            }
            sb.AppendLine();
        }
        return sb.ToString();
    }

    private static byte[] RenderAttrMap(byte[] ppu, int ntBase)
    {
        var rgb = new byte[256 * 240 * 3];
        byte[][] cols =
        {
            new byte[] { 40, 80, 200 },
            new byte[] { 40, 180, 40 },
            new byte[] { 200, 180, 40 },
            new byte[] { 200, 40, 40 }
        };
        for (int ty = 0; ty < 30; ty++)
        {
            for (int tx = 0; tx < 32; tx++)
            {
                int attrAddr = ntBase + 0x3C0 + (ty / 4) * 8 + (tx / 4);
                byte attr = ppu[attrAddr];
                int shift = (tx / 2 % 2) * 2 + (ty / 2 % 2) * 4;
                int pal = (attr >> shift) & 3;
                byte[] c = cols[pal];
                for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                {
                    int o = ((ty * 8 + y) * 256 + tx * 8 + x) * 3;
                    rgb[o] = c[0];
                    rgb[o + 1] = c[1];
                    rgb[o + 2] = c[2];
                }
            }
        }
        return Png.WriteRgb8(256, 240, rgb);
    }

    private static byte[] RenderLeftover(byte[] ppu, int ntBase, byte clearTile)
    {
        var rgb = new byte[256 * 240 * 3];
        for (int ty = 0; ty < 30; ty++)
        {
            for (int tx = 0; tx < 32; tx++)
            {
                bool left = ppu[ntBase + ty * 32 + tx] == clearTile;
                byte r = left ? (byte)220 : (byte)30;
                byte g = left ? (byte)40 : (byte)30;
                byte b = left ? (byte)40 : (byte)30;
                if (!left) g = 160;
                for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                {
                    int o = ((ty * 8 + y) * 256 + tx * 8 + x) * 3;
                    rgb[o] = r;
                    rgb[o + 1] = g;
                    rgb[o + 2] = b;
                }
            }
        }
        return Png.WriteRgb8(256, 240, rgb);
    }
}
