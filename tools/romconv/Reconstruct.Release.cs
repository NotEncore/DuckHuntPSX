using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace RomConv;

internal static partial class Reconstruct
{
    private static void WriteGameTables(byte[] prg, string output)
    {
        const int bank = 0xc000;
        var header = new StringBuilder("/* Generated from the user's local ROM. */\n");
        foreach (var (name, address, length) in new[] {
            ("clay_seeds", 0xe968, 96), ("clay_cels", 0xe92a, 24),
            ("clay_hit_sizes", 0xe944, 36), ("clay_depth_speeds", 0xe9c8, 56),
            ("clay_update_counts", 0xea00, 48), ("clay_round_offsets", 0xec17, 9)
        })
            WriteByteTable(header, name, prg.AsSpan(address - bank, length).ToArray());
        File.WriteAllText(Path.Combine(output, "clay_tables.h"), header.ToString());
        // $EB30 contains pointers to signed coordinate pairs, terminated by $AA.
        // Validate the record boundaries before serializing C initializers.
        header.Clear().AppendLine("/* Generated from the user's verified local ROM. */");
        var hitRows = new List<string>();
        var hitCounts = new List<int>();
        for (int i = 0; i < 6; i++)
        {
            int pointer = 0xeb30 - bank + i * 2;
            int start = (prg[pointer] | prg[pointer + 1] << 8) - bank;
            int end = (prg[pointer + 2] | prg[pointer + 3] << 8) - bank;
            int bytes = end - start - 1;
            if (start < 0 || end > prg.Length || bytes < 2 || bytes > 18 ||
                (bytes & 1) != 0 || prg[end - 1] != 0xaa)
                throw new InvalidDataException("Invalid clay hitbox record in verified ROM.");
            hitRows.Add("{ " + string.Join(", ", prg.AsSpan(start, bytes).ToArray()
                .Select(value => unchecked((sbyte)value))) + " }");
            hitCounts.Add(bytes / 2);
        }
        header.AppendLine("#define ROM_CLAY_HIT_OFFSETS { " + string.Join(", ", hitRows) + " }");
        header.AppendLine("#define ROM_CLAY_HIT_COUNTS { " + string.Join(", ", hitCounts) + " }");
        var scoreRows = new List<string>();
        for (int i = 0; i < 3; i++)
            scoreRows.Add("{ " + string.Join(", ", prg.AsSpan(0xebf1 - bank + i * 3, 3).ToArray()) + " }");
        header.AppendLine("#define ROM_SCORE_TILES { " + string.Join(", ", scoreRows) + " }");
        var speedRows = new List<string>();
        for (int i = 0; i < 12; i++)
            speedRows.Add("{ " + string.Join(", ", prg.AsSpan(0xea00 - bank + i * 4, 4).ToArray()) + " }");
        header.AppendLine("#define ROM_LEGACY_SPEED_ROWS " + string.Join(", ", speedRows));
        var firstDuckColors = prg.AsSpan(0xe766 - bank, 8).ToArray().Select(value => value >> 4);
        header.AppendLine("#define ROM_FIRST_DUCK_COLORS { " + string.Join(", ", firstDuckColors) + " }");
        File.WriteAllText(Path.Combine(output, "rom_hit_score_tables.h"), header.ToString());

        header.Clear().AppendLine("/* Generated from the user's local ROM. */");
        WriteByteTable(header, "hunt_round_thresholds", prg.AsSpan(0xec02 - bank, 20).ToArray());
        File.WriteAllText(Path.Combine(output, "hunt_round_thresholds.h"), header.ToString());

        var offsets = new List<int>();
        var scripts = new List<byte>();
        for (int i = 0; i < 16; i++)
        {
            int pointer = 0xe60b - bank + i * 2;
            int cursor = (prg[pointer] | prg[pointer + 1] << 8) - bank;
            offsets.Add(scripts.Count);
            scripts.Add(prg[cursor++]);
            for (;;)
            {
                byte duration = prg[cursor++];
                scripts.Add(duration);
                scripts.Add(prg[cursor++]);
                if (duration == 255)
                {
                    scripts.Add(prg[cursor]);
                    break;
                }
            }
        }
        header.Clear().AppendLine("/* Generated from the user's local ROM. */");
        header.AppendLine("static const unsigned short game_b_script_offset[16] = { " +
            string.Join(", ", offsets) + " };");
        WriteByteTable(header, "game_b_scripts", scripts.ToArray());
        WriteByteTable(header, "game_b_speeds", prg.AsSpan(0xe73c - bank, 30).ToArray());
        WriteByteTable(header, "game_b_colors", prg.AsSpan(0xe766 - bank, 8).ToArray());
        File.WriteAllText(Path.Combine(output, "game_b_flight.h"), header.ToString());
    }

    private static void WriteByteTable(StringBuilder header, string name, byte[] bytes)
    {
        header.AppendLine($"static const unsigned char {name}[{bytes.Length}] = {{");
        foreach (byte[] line in bytes.Chunk(16))
            header.AppendLine("    " + string.Join(", ", line) + ",");
        header.AppendLine("};");
    }

    private static void WriteScoreSprites(byte[] chr, string output)
    {
        const int width = 256;
        byte[] pixels = new byte[width * 16];
        for (int wide = 0; wide < 2; wide++)
        {
            int cellWidth = wide == 0 ? 16 : 20;
            for (int index = 0; index < 8; index++)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < cellWidth; x++)
                    {
                        int column = wide == 0 ? x : x * 4 / 5;
                        int tile = column < 8 ? 0xf7 + index : 0xff;
                        int offset = tile * 16 + y;
                        int bit = 7 - (column & 7);
                        int color = ((chr[offset] >> bit) & 1) | (((chr[offset + 8] >> bit) & 1) << 1);
                        pixels[(wide * 8 + y) * width + index * cellWidth + x] = (byte)color;
                    }
        }
        var header = new StringBuilder("/* Generated from the user's local ROM. */\n");
        header.AppendLine("static const uint16_t hunt_score_tex[64 * 16] __attribute__((aligned(4))) = {");
        foreach (ushort[] line in Pack4bpp(pixels, width, 16).Chunk(16))
            header.AppendLine("    " + string.Join(", ", line.Select(word => $"0x{word:X4}")) + ",");
        header.AppendLine("};");
        File.WriteAllText(Path.Combine(output, "hunt_score_sprites.h"), header.ToString());
    }

    private static void WriteGameBanners(byte[] prg, byte[] chr, string output)
    {
        // Compose the panels from CHR tile references. The round number is drawn by the game.
        const int blank = -1;
        int[][] round = Enumerable.Range(0, 5)
            .Select(row => ReadTileIds(prg, 0xec4d + row * 7, 7))
            .ToArray();
        int[][] over = {
            Array.ConvertAll(prg.AsSpan(0xed3f - 0xc000, 11).ToArray(), value => (int)value),
            new[] { 0xd8, blank, blank, blank, blank, blank, blank, blank, blank, blank, 0x21 },
            ReadTileIds(prg, 0xed55, 5).Append(blank).Concat(ReadTileIds(prg, 0xed5b, 5)).ToArray(),
            new[] { 0xd8, blank, blank, blank, blank, blank, blank, blank, blank, blank, 0x21 },
            Array.ConvertAll(prg.AsSpan(0xed6b - 0xc000, 11).ToArray(), value => (int)value),
        };
        const int width = 128, height = 33;
        byte[] pixels = new byte[width * height * 4];
        for (int wide = 0; wide < 2; wide++)
        for (int panel = 0; panel < 2; panel++)
        {
            int x0 = panel == 0 ? 107 : 91;
            int nativeWidth = panel == 0 ? 49 : 81;
            int origin = wide != 0 ? x0 * 5 / 4 : x0;
            int end = wide != 0 ? (x0 + nativeWidth) * 5 / 4 : x0 + nativeWidth;
            int[][] tiles = panel == 0 ? round : over;
            for (int y = 0; y < height; y++)
            for (int x = origin; x < end; x++)
            {
                int sx = wide != 0 ? Math.Max(x0, ((x + 1) * 4 + 4) / 5 - 1) : x;
                int localX = sx - (x0 & ~7), localY = y + 3;
                int tile = tiles[localY / 8][localX / 8];
                int color = 1;
                if (tile != blank)
                {
                    int offset = (256 + tile) * 16 + (localY & 7);
                    int bit = 7 - (localX & 7);
                    color = ((chr[offset] >> bit) & 1) | (((chr[offset + 8] >> bit) & 1) << 1);
                }
                pixels[((wide * 2 + panel) * height + y) * width + x - origin] =
                    color == 1 ? (byte)5 : color == 2 ? (byte)4 : (byte)15;
            }
        }
        var header = new StringBuilder("/* Generated from the user's local ROM. */\n");
        header.AppendLine("static const uint16_t game_banner_tex[] __attribute__((aligned(4))) = {");
        foreach (ushort[] line in Pack4bpp(pixels, width, height * 4).Chunk(16))
            header.AppendLine("    " + string.Join(", ", line.Select(word => $"0x{word:X4}")) + ",");
        header.AppendLine("};");
        File.WriteAllText(Path.Combine(output, "game_a_banners.h"), header.ToString());
    }

    private static void WriteClayBackground(byte[] prg, byte[] chr, string output)
    {
        var ppu = new byte[0x4000];
        ClearNametable(ppu, 0x2000, 0x24);
        var trace = new StringBuilder();
        if (!TryUploadPpuFile(prg, 0x226c, ppu, trace, "clay field") ||
            !TryUploadPpuFile(prg, 0x257c, ppu, trace, "clay palette"))
            throw new InvalidDataException(trace.ToString());

        // Draw the initial round digit and the first six minimum-hit bars.
        ppu[0x2000 + 24 * 32 + 5] = 1;
        for (int x = 12; x < 18; x++)
            ppu[0x2000 + 27 * 32 + x] = 0xd7;

        // Reference RGB colors, indexed by NES palette entry and packed below.
        var colors = new Dictionary<int, int> {
            [0x2c] = 0x48cdde, [0x19] = 0x388700, [0x09] = 0x0b4800,
            [0x29] = 0x88d800, [0x1a] = 0x0c9300, [0x07] = 0x561d00,
            [0x0f] = 0, [0x10] = 0xadadad, [0x30] = 0xfffeff
        };
        var header = new StringBuilder("/* Generated from the user's local ROM. */\n");
        header.AppendLine("static const unsigned short clay_rom_background[256*240] __attribute__((aligned(4))) = {");
        for (int y = 0; y < 240; y++)
        for (int x = 0; x < 256; x++)
        {
            int tx = x / 8, ty = y / 8;
            int tile = ppu[0x2000 + ty * 32 + tx];
            int attribute = ppu[0x23c0 + ty / 4 * 8 + tx / 4];
            int palette = (attribute >> ((ty & 2) * 2 + (tx & 2))) & 3;
            int offset = 0x1000 + tile * 16 + (y & 7);
            int bit = 7 - (x & 7);
            int pixel = ((chr[offset] >> bit) & 1) | (((chr[offset + 8] >> bit) & 1) << 1);
            int rgb = colors[ppu[0x3f00 + (pixel == 0 ? 0 : palette * 4 + pixel)]];
            int rgb555 = ((rgb >> 19) & 31) | (((rgb >> 11) & 31) << 5) | (((rgb >> 3) & 31) << 10);
            header.Append($"0x{rgb555:X4},");
            if ((x & 31) == 31)
                header.AppendLine();
        }
        header.AppendLine("};");
        File.WriteAllText(Path.Combine(output, "clay_background.h"), header.ToString());
    }

    private static void WriteClaySprites(byte[] prg, byte[] chr, string output)
    {
        var ppu = new byte[0x4000];
        var trace = new StringBuilder();
        if (!TryUploadPpuFile(prg, 0x257c, ppu, trace, "clay sprite palette"))
            throw new InvalidDataException(trace.ToString());
        byte[] palette = ppu.AsSpan(0x3f00, 32).ToArray();
        var header = new StringBuilder("/* Generated from the user's local ROM. */\n");
        header.AppendLine("typedef struct { signed char x,y; unsigned char r,g,b; } ClayPixel;");
        var names = new List<string>();
        var counts = new List<int>();
        // Align each ROM metasprite to the projectile anchor used by the port.
        int[] anchorAdjustY = { 8, 8, 7, 4, 4, 3, 2, 2, 0, 1, 0, 0 };
        for (int cel = 24; cel <= 35; cel++)
        {
            int animation = Math.Max(24, cel - 1);
            if (!TryBuildDuckIndices(prg, chr, palette, 0, animation,
                out byte[] pixels, out int width, out int height, out int anchorX,
                out int anchorY, out List<byte> colors, out string note, firstSpriteWins: true))
                throw new InvalidDataException($"Clay cel {cel}: {note}");

            string name = $"clay_cel_{cel}";
            int count = 0;
            header.AppendLine($"static const ClayPixel {name}[] = {{");
            for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                int index = pixels[y * width + x];
                int dx = x - anchorX + (cel == 31 ? 2 : 0);
                int dy = y - anchorY + anchorAdjustY[cel - 24];
                if (index == 0 || dx < -16 || dx > 16 || dy < -10 || dy > 10)
                    continue;
                string rgb = colors[index - 1] == 0x30 ? "255,254,255" : "0,0,0";
                header.AppendLine($"{{{dx},{dy},{rgb}}},");
                count++;
            }
            header.AppendLine("};");
            names.Add(name);
            counts.Add(count);
        }
        header.AppendLine("static const ClayPixel *const clay_pixels[] = {" + string.Join(",", names) + "};");
        header.AppendLine("static const unsigned short clay_pixel_counts[] = {" + string.Join(",", counts) + "};");
        File.WriteAllText(Path.Combine(output, "clay_sprites.h"), header.ToString());
    }

    private static void WriteClayHud(byte[] prg, byte[] chr, string output)
    {
        var header = new StringBuilder("/* Generated from the user's local ROM. */\n");
        header.AppendLine("typedef struct { signed char x,y; unsigned char w,r,g,b; } ClayRun;");
        header.AppendLine("static const unsigned char rom_clay_digits[10][8] = {");
        for (int digit = 0; digit < 10; digit++)
        {
            int offset = (256 + digit) * 16;
            header.AppendLine("{" + string.Join(",", Enumerable.Range(0, 8)
                .Select(row => chr[offset + row + 8] & ~chr[offset + row])) + "},");
        }
        header.AppendLine("};");

        int Pixel(int tile, int x, int y)
        {
            int offset = (256 + tile) * 16 + (y & 7);
            int bit = 7 - (x & 7);
            return ((chr[offset] >> bit) & 1) | (((chr[offset + 8] >> bit) & 1) << 1);
        }
        void Patch(string name, int width, int height, Func<int, int, int> color)
        {
            header.AppendLine($"static const ClayRun rom_clay_{name}[] = {{");
            for (int y = 0; y < height; y++)
            for (int x = 0; x < width;)
            {
                int start = x;
                int rgb = color(x++, y);
                while (x < width && color(x, y) == rgb)
                    x++;
                header.AppendLine($"{{{start},{y},{x - start},{rgb >> 16},{(rgb >> 8) & 255},{rgb & 255}}},");
            }
            header.AppendLine("};");
        }
        void Panel(string name, int columns, int width, int xOffset,
            int textRow, int[] text, int[]? score = null)
        {
            Patch(name, width, 33, (x, y) =>
            {
                int column = (x + xOffset) / 8;
                int row = (y + 3) / 8;
                int tile;
                if (column == 0)
                    tile = row == 0 ? 0xff : row == 4 ? 0x23 : 0xd8;
                else if (column == columns - 1)
                    tile = row == 0 ? 0x1a : row == 4 ? 0xfd : 0x21;
                else if (row == 0 || row == 4)
                    tile = row == 0 ? 0x13 : 0xfc;
                else if (row == textRow)
                    tile = text[column - 1];
                else if (row == 3 && score != null)
                    tile = score[column - 1];
                else
                    tile = 0xb7;
                int pixel = Pixel(tile, x + xOffset, y + 3);
                return pixel == 0 ? 0x48cdde : pixel == 2 ? 0xfffeff : 0;
            });
        }
        Patch("red_mark", 8, 8, (x, y) => Pixel(0xd3, x, y) switch {
            2 => 0xfffeff, 3 => 0xb53120, _ => 0
        });
        Panel("go", 7, 49, 3, 2, ReadTileIds(prg, 0xec78, 5));
        Panel("over", 11, 82, 2, 2, Array.ConvertAll(prg.AsSpan(0xed56 - 0xc000, 9).ToArray(), value => (int)value));
        Panel("perfect", 10, 73, 3, 1, ReadTileIds(prg, 0xecbf, 8),
            new[] { 0xb7, 1, 0, 0, 0, 0, 0xb7, 0xb7 });
        Patch("minimum", 8, 8, (x, y) => Pixel(0xd7, x, y) == 0 ? 0x48cdde : 0);
        WriteClayBreak(chr, header);
        File.WriteAllText(Path.Combine(output, "clay_rom_hud.h"), header.ToString());
    }

    private static int[] ReadTileIds(byte[] prg, int address, int count)
    {
        return Array.ConvertAll(prg.AsSpan(address - 0xc000, count).ToArray(), value => (int)value);
    }

    private static void WriteClayBreak(byte[] chr, StringBuilder header)
    {
        // Measured fragment anchors, relative to the shot clay. Zero radius
        // marks a blink frame. The fragment artwork comes from CHR tile $EE.
        (int InnerX, int OuterY, int OuterX, int InnerY)[] motion = {
            (1,2,2,0), (2,4,4,1), (3,6,6,2), (4,7,7,3), (4,8,8,3),
            (5,9,9,4), (5,10,10,4), (6,11,11,5), (6,11,11,5), (6,12,12,5),
            (7,12,12,6), (7,13,13,6), (7,13,13,6), (7,14,14,6), (7,14,14,6),
            (8,15,15,7), (0,0,0,0), (8,15,15,7), (8,15,15,7), (8,16,16,7),
            (0,0,0,0), (9,16,16,8), (9,16,16,8), (9,17,17,8), (0,0,0,0),
            (9,17,17,8), (9,17,17,8), (0,0,0,0), (10,18,18,9), (10,18,18,9),
            (10,18,18,9), (0,0,0,0), (10,19,19,9), (10,19,19,9), (0,0,0,0),
            (10,19,19,9), (10,19,19,9)
        };
        var names = new List<string>();
        var counts = new List<int>();
        for (int frame = 0; frame < motion.Length; frame++)
        {
            var (innerX, outerY, outerX, innerY) = motion[frame];
            var pixels = new SortedDictionary<(int Y, int X), int>();
            void Pair(int left, int top)
            {
                foreach (int anchor in new[] { -left, left - 1 })
                    for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                    {
                        int offset = 0xee * 16 + y;
                        int bit = 7 - x;
                        int color = ((chr[offset] >> bit) & 1) |
                            (((chr[offset + 8] >> bit) & 1) << 1);
                        if (color != 0)
                            pixels[(top + y - 4, anchor + x - 4)] = color == 1 ? 0xfffeff : 0;
                    }
            }
            if (outerY != 0)
            {
                Pair(innerX, -outerY);
                Pair(outerX, -innerY);
                Pair(outerX, innerY);
                Pair(innerX, outerY);
            }

            string name = $"rom_clay_break_{frame}";
            names.Add(name);
            header.AppendLine($"static const ClayRun {name}[] = {{");
            var ordered = pixels.ToArray();
            int count = 0;
            for (int i = 0; i < ordered.Length;)
            {
                var ((y, x), rgb) = ordered[i++];
                int width = 1;
                while (i < ordered.Length && ordered[i].Key == (y, x + width) && ordered[i].Value == rgb)
                {
                    i++;
                    width++;
                }
                header.AppendLine($"{{{x},{y},{width},{rgb >> 16},{(rgb >> 8) & 255},{rgb & 255}}},");
                count++;
            }
            if (count == 0)
                header.AppendLine("{0,0,0,0,0,0},");
            counts.Add(count);
            header.AppendLine("};");
        }
        header.AppendLine("static const ClayRun *const rom_clay_break[] = {" + string.Join(",", names) + "};");
        header.AppendLine("static const unsigned char rom_clay_break_counts[] = {" + string.Join(",", counts) + "};");
    }
}
