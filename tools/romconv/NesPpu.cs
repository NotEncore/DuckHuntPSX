/*
The NES RGB approximation table is attributed to OneLoneCoder's olcNES.
Comparison reference: Part #4 - PPU Backgrounds/olc2C02.cpp
Verified revision: ac5ce64cdb3a390a89d550c5f130682b37eeb080
https://github.com/OneLoneCoder/olcNES/blob/ac5ce64cdb3a390a89d550c5f130682b37eeb080/Part%20%234%20-%20PPU%20Backgrounds/olc2C02.cpp
License (OLC-3)

Copyright 2018-2019 OneLoneCoder.com

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions or derivations of source code must retain the above
copyright notice, this list of conditions and the following disclaimer.

2. Redistributions or derivative works in binary form must reproduce
the above copyright notice. This list of conditions and the following
disclaimer must be reproduced in the documentation and/or other
materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived
from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
namespace RomConv;

/// <summary>
/// NES PPU render helpers. Pattern tables come from PPUCTRL; CHR is NROM identity.
/// Palette RGB is a 2C02 approximation (not Mesen's internal table).
/// </summary>
internal static class NesPpu
{
    public const int Width = 256;
    public const int Height = 240;

    // Same 2C02 approximation used by Reconstruct (hardware colors, not artwork).
    public static readonly byte[] NesRgb =
    {
        84,84,84, 0,30,116, 8,16,144, 48,0,136, 68,0,100, 92,0,48, 84,4,0, 60,24,0,
        32,42,0, 8,58,0, 0,64,0, 0,60,0, 0,50,60, 0,0,0, 0,0,0, 0,0,0,
        152,150,152, 8,76,196, 48,50,236, 92,30,228, 136,20,176, 160,20,100, 152,34,32, 120,60,0,
        84,90,0, 40,114,0, 8,124,0, 0,118,40, 0,102,120, 0,0,0, 0,0,0, 0,0,0,
        236,238,236, 76,154,236, 120,124,236, 176,98,236, 228,84,236, 236,88,180, 236,106,100, 212,136,32,
        160,170,0, 116,196,0, 76,208,32, 56,204,108, 56,180,204, 60,60,60, 0,0,0, 0,0,0,
        236,238,236, 168,204,236, 188,188,236, 212,178,236, 236,174,236, 236,174,212, 236,180,176, 228,196,144,
        204,210,120, 180,222,120, 168,226,144, 152,226,180, 160,214,228, 160,162,160, 0,0,0, 0,0,0
    };

    public static int BgPattern(int ctrl) => (ctrl & 0x08) != 0 ? 0x1000 : 0x0000;
    public static int SprPattern(int ctrl) => (ctrl & 0x10) != 0 ? 0x1000 : 0x0000;
    public static bool LargeSprites(int ctrl) => (ctrl & 0x20) != 0;

    public static void GetRgb(byte nes, out byte r, out byte g, out byte b)
    {
        int i = (nes & 0x3F) * 3;
        r = NesRgb[i];
        g = NesRgb[i + 1];
        b = NesRgb[i + 2];
    }

    public sealed class SpriteLayer
    {
        public readonly byte[] Rgba = new byte[Width * Height * 4];
        public readonly byte[] ColorIndex = new byte[Width * Height];
        public readonly byte[] Behind = new byte[Width * Height];
    }

    /// <summary>
    /// Render nametable 0 (1024 bytes: 960 tiles + 64 attributes).
    /// colorIndex is 0–3 within the tile palette (0 = backdrop / transparent for priority).
    /// </summary>
    public static byte[] RenderNametable(byte[] chr, byte[] nt, byte[] pal32, int bgPattern, byte[]? colorIndex)
    {
        var rgb = new byte[Width * Height * 3];
        for (int ty = 0; ty < 30; ty++)
        {
            for (int tx = 0; tx < 32; tx++)
            {
                int tile = nt[ty * 32 + tx];
                int attrAddr = 0x3C0 + (ty / 4) * 8 + (tx / 4);
                byte attr = nt[attrAddr];
                int shift = (tx / 2 % 2) * 2 + (ty / 2 % 2) * 4;
                int palIdx = (attr >> shift) & 3;
                DrawTile(chr, bgPattern, tile, pal32, palIdx, false, false, tx * 8, ty * 8, Width, rgb, null, colorIndex);
            }
        }
        return rgb;
    }

    /// <param name="yMode">0 = OAM Y as top row (some dumps); 1 = NES scanline delay (display at Y+1).</param>
    public static SpriteLayer RenderOam(byte[] chr, byte[] oam, byte[] pal32, int sprPattern, bool spr16, int yMode)
    {
        var layer = new SpriteLayer();
        for (int i = 63; i >= 0; i--)
        {
            int o = i * 4;
            if (o + 3 >= oam.Length) continue;
            int y = oam[o];
            if (y >= 0xEF) continue;
            int tile = oam[o + 1];
            int attr = oam[o + 2];
            int x = oam[o + 3];
            bool flipH = (attr & 0x40) != 0;
            bool flipV = (attr & 0x80) != 0;
            bool behind = (attr & 0x20) != 0;
            int palIdx = 4 + (attr & 3);
            int dy = y + yMode;
            if (spr16)
            {
                int top = tile & 0xFE;
                int bot = top | 1;
                if ((tile & 1) != 0) { top = tile | 1; bot = tile & 0xFE; }
                DrawSpriteTile(chr, sprPattern, flipV ? bot : top, pal32, palIdx, flipH, flipV, x, dy, layer, behind);
                DrawSpriteTile(chr, sprPattern, flipV ? top : bot, pal32, palIdx, flipH, flipV, x, dy + 8, layer, behind);
            }
            else
                DrawSpriteTile(chr, sprPattern, tile, pal32, palIdx, flipH, flipV, x, dy, layer, behind);
        }
        return layer;
    }

    public static byte[] Composite(byte[] bgRgb, byte[] bgIndex, SpriteLayer spr)
    {
        var outp = (byte[])bgRgb.Clone();
        for (int i = 0; i < Width * Height; i++)
        {
            if (spr.Rgba[i * 4 + 3] == 0) continue;
            if (spr.Behind[i] != 0 && bgIndex[i] != 0) continue;
            outp[i * 3] = spr.Rgba[i * 4];
            outp[i * 3 + 1] = spr.Rgba[i * 4 + 1];
            outp[i * 3 + 2] = spr.Rgba[i * 4 + 2];
        }
        return outp;
    }

    /// <summary>Opaque sprite pixels only (ignores BG priority). For sprite-only diagnostics.</summary>
    public static byte[] SpriteRgbaVisible(SpriteLayer spr) => spr.Rgba;

    public static byte[] IsolatedSpritesPngRgba(byte[] chr, byte[] oam, byte[] pal32, int sprPattern, bool spr16, int yMode, Func<int, int, int, int, bool>? include, out int outW, out int outH, out int originX, out int originY)
    {
        int minX = Width, minY = Height, maxX = 0, maxY = 0;
        var kept = new List<(int y, int tile, int attr, int x)>();
        for (int i = 0; i < 64; i++)
        {
            int o = i * 4;
            if (o + 3 >= oam.Length) break;
            int y = oam[o];
            if (y >= 0xEF) continue;
            int tile = oam[o + 1];
            int attr = oam[o + 2];
            int x = oam[o + 3];
            if (include != null && !include(y, tile, attr, x)) continue;
            int dy = y + yMode;
            int h = spr16 ? 16 : 8;
            kept.Add((dy, tile, attr, x));
            if (x < minX) minX = x;
            if (dy < minY) minY = dy;
            if (x + 8 > maxX) maxX = x + 8;
            if (dy + h > maxY) maxY = dy + h;
        }
        if (kept.Count == 0)
        {
            outW = 8; outH = 8; originX = 0; originY = 0;
            return new byte[8 * 8 * 4];
        }
        originX = minX;
        originY = minY;
        outW = Math.Max(8, maxX - minX);
        outH = Math.Max(8, maxY - minY);
        var rgba = new byte[outW * outH * 4];
        // Draw back-to-front (high OAM index first) to match NES.
        for (int i = kept.Count - 1; i >= 0; i--)
        {
            var s = kept[i];
            bool flipH = (s.attr & 0x40) != 0;
            bool flipV = (s.attr & 0x80) != 0;
            int palIdx = 4 + (s.attr & 3);
            if (spr16)
            {
                int top = s.tile & 0xFE;
                int bot = top | 1;
                DrawTile(chr, sprPattern, flipV ? bot : top, pal32, palIdx, flipH, flipV, s.x - originX, s.y - originY, outW, null, rgba, null);
                DrawTile(chr, sprPattern, flipV ? top : bot, pal32, palIdx, flipH, flipV, s.x - originX, s.y - originY + 8, outW, null, rgba, null);
            }
            else
                DrawTile(chr, sprPattern, s.tile, pal32, palIdx, flipH, flipV, s.x - originX, s.y - originY, outW, null, rgba, null);
        }
        return rgba;
    }

    public static void DrawTile(
        byte[] chr, int patternBase, int tile, byte[] pal32, int palette, bool flipH, bool flipV,
        int dx, int dy, int stridePx, byte[]? rgb, byte[]? alpha, byte[]? colorIndex)
    {
        int tileOff = patternBase + (tile & 0xFF) * 16;
        if (tileOff + 16 > chr.Length) return;
        int palBase = (palette & 7) * 4;
        int heightLimit = rgb != null ? Height : (alpha != null ? alpha.Length / (stridePx * 4) : Height);
        if (colorIndex != null) heightLimit = Height;
        for (int row = 0; row < 8; row++)
        {
            int srcRow = flipV ? 7 - row : row;
            byte p0 = chr[tileOff + srcRow];
            byte p1 = chr[tileOff + 8 + srcRow];
            for (int col = 0; col < 8; col++)
            {
                int bit = flipH ? col : 7 - col;
                int idx = ((p1 >> bit) & 1) << 1 | ((p0 >> bit) & 1);
                int px = dx + col;
                int py = dy + row;
                if (px < 0 || py < 0 || px >= stridePx || py >= heightLimit) continue;
                if (colorIndex != null && stridePx == Width && py < Height)
                    colorIndex[py * Width + px] = (byte)idx;
                if (alpha != null)
                {
                    if (idx == 0) continue;
                    int o = (py * stridePx + px) * 4;
                    byte nes = pal32[palBase + idx];
                    GetRgb(nes, out byte r, out byte g, out byte b);
                    alpha[o] = r; alpha[o + 1] = g; alpha[o + 2] = b; alpha[o + 3] = 255;
                }
                else if (rgb != null)
                {
                    int o = (py * stridePx + px) * 3;
                    byte nes = idx == 0 ? pal32[0] : pal32[palBase + idx];
                    GetRgb(nes, out byte r, out byte g, out byte b);
                    rgb[o] = r; rgb[o + 1] = g; rgb[o + 2] = b;
                }
            }
        }
    }

    private static void DrawSpriteTile(
        byte[] chr, int pattern, int tile, byte[] pal32, int palIdx, bool flipH, bool flipV,
        int x, int y, SpriteLayer layer, bool behind)
    {
        int tileOff = pattern + (tile & 0xFF) * 16;
        if (tileOff + 16 > chr.Length) return;
        int palBase = (palIdx & 7) * 4;
        for (int row = 0; row < 8; row++)
        {
            int srcRow = flipV ? 7 - row : row;
            byte p0 = chr[tileOff + srcRow];
            byte p1 = chr[tileOff + 8 + srcRow];
            for (int col = 0; col < 8; col++)
            {
                int bit = flipH ? col : 7 - col;
                int idx = ((p1 >> bit) & 1) << 1 | ((p0 >> bit) & 1);
                if (idx == 0) continue;
                int px = x + col;
                int py = y + row;
                if ((uint)px >= Width || (uint)py >= Height) continue;
                int i = py * Width + px;
                layer.ColorIndex[i] = (byte)idx;
                layer.Behind[i] = behind ? (byte)1 : (byte)0;
                int o = i * 4;
                byte nes = pal32[palBase + idx];
                GetRgb(nes, out byte r, out byte g, out byte b);
                layer.Rgba[o] = r; layer.Rgba[o + 1] = g; layer.Rgba[o + 2] = b; layer.Rgba[o + 3] = 255;
            }
        }
    }

    public static int NearestNesIndex(byte r, byte g, byte b)
    {
        int best = 0, bestD = int.MaxValue;
        for (int i = 0; i < 64; i++)
        {
            int dr = r - NesRgb[i * 3];
            int dg = g - NesRgb[i * 3 + 1];
            int db = b - NesRgb[i * 3 + 2];
            int d = dr * dr + dg * dg + db * db;
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }
}

