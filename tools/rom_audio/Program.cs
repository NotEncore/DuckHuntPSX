using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using NesV15;

const int rate = 22050;
const double fps = 60.0988;

try
{
    if (args.Length is 4 or 5 && args[0] == "game-a")
    {
        RenderEffects(args[1], args[2], args[3], args.Length == 5 ? args[4] : null);
    }
    else if (args.Length == 3 && args[0] is "title" or "intro")
    {
        RenderMusic(args[1], args[2], args[0] == "title");
    }
    else
    {
        Console.Error.WriteLine("Usage: RomAudio game-a ROM TRACE_DIR OUTPUT_DIR [WINDOW_OFFSETS_CSV]");
        Console.Error.WriteLine("       RomAudio title|intro WAV OUTPUT_DIR");
        return 2;
    }
    return 0;
}
catch (Exception ex)
{
    Console.Error.WriteLine(ex.Message);
    return 1;
}

void RenderEffects(string romPath, string traceDirectory, string output, string? offsetsPath)
{
    byte[] rom = File.ReadAllBytes(romPath);
    if (rom.Length < 16 || !rom.AsSpan(0, 4).SequenceEqual(new byte[] { 78, 69, 83, 26 }))
        throw new InvalidDataException("Expected a cartridge dump with an iNES header.");
    int prg = 16 + ((rom[6] & 4) != 0 ? 512 : 0);
    const string expectedRom = "7026334A7E8742B61B450F4B3B182922C6A69FC723D7CD19C83DB365F15E45BA";
    if (rom.Length < prg + 24576 || Convert.ToHexString(SHA256.HashData(rom.AsSpan(prg, 24576))) != expectedRom)
        throw new InvalidDataException("The cartridge payload is not the verified Duck Hunt revision.");
    Directory.CreateDirectory(output);

    var cues = new[] {
        ("shot", "perfect", 628, 725, 8), ("flap", "perfect", 573, 581, 8),
        ("call", "perfect", 582, 590, 0), ("fall", "perfect", 651, 706, 1),
        ("land", "perfect", 706, 747, 3), ("retrieve", "perfect", 773, 829, 7),
        ("laugh", "fail", 985, 1069, 12), ("clear", "mixed", 4449, 4649, 15),
        ("bonus", "perfect", 3664, 3747, 3), ("over", "fail", 6539, 6904, 15),
        ("tally", "gapped", 4615, 4627, 3)
    };
    var offsets = new Dictionary<string, (int Shift, int? ActiveFrames)>();
    if (offsetsPath != null)
        foreach (string line in File.ReadLines(offsetsPath))
        {
            if (string.IsNullOrWhiteSpace(line))
                continue;
            string[] fields = line.Split(',');
            if (fields.Length is < 2 or > 3)
                throw new InvalidDataException($"Invalid cue window: {line}");
            offsets.Add(fields[0], (int.Parse(fields[1], CultureInfo.InvariantCulture),
                fields.Length == 3 ? int.Parse(fields[2], CultureInfo.InvariantCulture) : null));
        }
    var traces = new Dictionary<string, ILookup<int, (int Address, int Value)>>();
    var bank = new StringBuilder("/* Generated from local NES APU traces. */\n");
    foreach (var (name, profile, start, end, mask) in cues)
    {
        if (!traces.TryGetValue(profile, out var trace))
        {
            trace = File.ReadLines(Path.Combine(traceDirectory, profile + ".trace"))
                .Where(line => line.StartsWith("APU ", StringComparison.Ordinal))
                .Select(line => line[4..].Split(','))
                .ToLookup(fields => int.Parse(fields[0], CultureInfo.InvariantCulture),
                    fields => (Convert.ToInt32(fields[1], 16), Convert.ToInt32(fields[2], 16)));
            traces.Add(profile, trace);
        }
        var (shift, activeFrames) = offsets.GetValueOrDefault(name);
        if (start + shift < 0 || !trace.Any(group => group.Key >= end + shift - 1))
            throw new InvalidDataException($"Trace does not cover the full {name} window.");
        int length = end - start;
        int audible = activeFrames ?? length;
        if (audible <= 0 || audible > length)
            throw new InvalidDataException($"Invalid active frame count for {name}.");
        short[] pcm = RenderCue(trace, rom, prg, name == "call", start + shift, start + shift + audible, mask);
        if (!pcm.Any(sample => Math.Abs((int)sample) > 12))
            throw new InvalidDataException($"The {name} window is silent.");
        Array.Resize(ref pcm, (int)Math.Round(length * rate / fps) + 28);
        byte[] encoded = Encoder.EncodePcm22050(pcm, out _);
        int used = encoded.Length;
        Array.Resize(ref encoded, (used + 63) & ~63);
        if (name == "tally")
            bank.Clear().AppendLine("/* Generated from local NES APU traces. */");
        AppendArray(bank, "static const", "game_a_" + name, encoded, aligned: true);
        bank.AppendLine($"#define GAME_A_{name.ToUpperInvariant()}_ADPCM {used}");
        if (name == "over" || name == "tally")
            WriteNew(Path.Combine(output, name == "tally" ? "game_a_tally.h" : "game_a_sfx.h"), bank);
        Console.WriteLine($"{name}: frames {start + shift}–{end + shift}, {used} ADPCM bytes");
    }
}

short[] RenderCue(ILookup<int, (int Address, int Value)> trace, byte[] rom, int prg,
    bool dmc, int start, int end, int mask)
{
    var pcm = new short[(int)Math.Round((end - start) * rate / fps)];
    var apu = new Apu2A03();
    apu.Write(0x4015, mask);
    double fractionalClocks = 0, dc = 0, dmcClock = 0;
    int bit = 0, level = 0, frame = start - 1;
    for (int i = 0; i < pcm.Length; i++)
    {
        int targetFrame = start + (int)(i * fps / rate);
        while (frame < targetFrame)
        foreach (var (address, value) in trace[++frame])
        {
            if (address == 0x4017)
                apu.Write(address, value);
            else if (address == 0x4015)
                apu.Write(address, value & mask);
            else if (address >= 0x4000 && address <= 0x400f &&
                (mask & (1 << ((address - 0x4000) / 4))) != 0)
                apu.Write(address, value);
        }
        fractionalClocks += 1789773.0 / rate;
        int clocks = (int)fractionalClocks;
        fractionalClocks -= clocks;
        int sum = 0;
        for (int j = 0; j < clocks; j++)
        {
            if (dmc && bit < 513 * 8 && dmcClock++ >= 54)
            {
                dmcClock -= 54;
                bool up = (rom[prg + 0x3d00 + bit / 8] & (1 << (bit % 8))) != 0;
                level = Math.Clamp(level + (up ? 2 : -2), 0, 126);
                bit++;
            }
            sum += dmc ? level * 115 : apu.Clock() + 10000;
        }
        double valueAtSample = (double)sum / clocks;
        dc += (valueAtSample - dc) * .008;
        double fade = Math.Min(1, Math.Min(i, pcm.Length - 1 - i) / 96.0);
        pcm[i] = (short)Math.Clamp((valueAtSample - dc) * fade, -22000, 22000);
    }
    return pcm;
}

void RenderMusic(string path, string output, bool title)
{
    (short[] source, int sourceRate) = ReadWave(path);
    if (title)
    {
        int first = Array.FindIndex(source, sample => Math.Abs((int)sample) > 80);
        int last = Array.FindLastIndex(source, sample => Math.Abs((int)sample) > 200);
        if (first < 0 || last <= first)
            throw new InvalidDataException("Title recording has no usable jingle.");
        int start = Math.Max(0, first - sourceRate / 50);
        source = source[start..Math.Min(source.Length, last + sourceRate / 25)];
    }
    double duration = source.Length / (double)sourceRate;
    if (title ? duration < 3.5 || duration > 4.1 : duration < 5.2 || duration > 5.8)
        throw new InvalidDataException($"Unexpected music duration: {duration:F3}s.");
    double step = sourceRate / (double)rate;
    var pcm = new short[(int)(source.Length / step)];
    for (int i = 0; i < pcm.Length; i++)
    {
        double position = i * step;
        int lo = (int)position, hi = Math.Min(source.Length - 1, lo + 1);
        double fraction = position - lo;
        pcm[i] = (short)(source[lo] * (1 - fraction) + source[hi] * fraction);
    }
    byte[] encoded = Encoder.EncodePcm22050(pcm, out _);
    encoded[^15] = 5;
    int used = encoded.Length;
    Array.Resize(ref encoded, (used + 63) & ~63);
    string symbol = title ? "m2_title" : "m3_intro";
    string stem = symbol.ToUpperInvariant();
    Directory.CreateDirectory(output);
    var header = new StringBuilder($"#pragma once\n#include <stdint.h>\n#define {stem}_ADPCM {used}\n" +
        $"#define {stem}_BYTES {encoded.Length}\n#define {stem}_RATE {rate}\n" +
        $"extern const uint8_t {symbol}[];\nextern const int {symbol}_bytes;\n");
    var code = new StringBuilder($"#include \"hunt_sfx_{symbol}.h\"\n");
    AppendArray(code, "const", symbol, encoded, aligned: false);
    code.AppendLine($"const int {symbol}_bytes = {encoded.Length};");
    WriteNew(Path.Combine(output, $"hunt_sfx_{symbol}.h"), header);
    WriteNew(Path.Combine(output, $"hunt_sfx_{symbol}.c"), code);
    Console.WriteLine($"{symbol}: {duration:F3}s, {used}/{encoded.Length} ADPCM bytes, " +
        Convert.ToHexString(SHA256.HashData(encoded)));
}

static (short[] Samples, int Rate) ReadWave(string path)
{
    using var reader = new BinaryReader(File.OpenRead(path));
    if (Encoding.ASCII.GetString(reader.ReadBytes(4)) != "RIFF")
        throw new InvalidDataException("Expected a RIFF WAV recording.");
    reader.ReadUInt32();
    if (Encoding.ASCII.GetString(reader.ReadBytes(4)) != "WAVE")
        throw new InvalidDataException("Expected a WAVE recording.");
    int channels = 0, sampleRate = 0;
    byte[] data = Array.Empty<byte>();
    while (reader.BaseStream.Position + 8 <= reader.BaseStream.Length)
    {
        string chunk = Encoding.ASCII.GetString(reader.ReadBytes(4));
        int size = reader.ReadInt32();
        long end = reader.BaseStream.Position + size;
        if (size < 0 || end > reader.BaseStream.Length)
            throw new InvalidDataException("Truncated WAV chunk.");
        if (chunk == "fmt " && size >= 16)
        {
            int format = reader.ReadUInt16();
            channels = reader.ReadUInt16();
            sampleRate = reader.ReadInt32();
            reader.ReadInt32();
            reader.ReadUInt16();
            if (format != 1 || reader.ReadUInt16() != 16)
                throw new InvalidDataException("Expected 16-bit PCM.");
        }
        if (chunk == "data") data = reader.ReadBytes(size);
        reader.BaseStream.Position = end + (size & 1);
    }
    if (channels is < 1 or > 2 || sampleRate <= 0 || data.Length == 0 || data.Length % (channels * 2) != 0)
        throw new InvalidDataException("Expected a complete mono or stereo recording.");
    var mono = new short[data.Length / (channels * 2)];
    for (int i = 0; i < mono.Length; i++)
    {
        int offset = i * channels * 2;
        int sum = BitConverter.ToInt16(data, offset);
        if (channels == 2) sum += BitConverter.ToInt16(data, offset + 2);
        mono[i] = (short)(sum / channels);
    }
    return (mono, sampleRate);
}

static void AppendArray(StringBuilder output, string storage, string name, byte[] bytes, bool aligned)
{
    output.AppendLine($"{storage} uint8_t {name}[]" +
        (aligned ? " __attribute__((aligned(64)))" : "") + " = {");
    foreach (byte[] line in bytes.Chunk(16))
        output.AppendLine(string.Join(",", line.Select(value => $"0x{value:X2}")) + ",");
    output.AppendLine("};");
}

static void WriteNew(string path, StringBuilder content)
{
    using var writer = new StreamWriter(new FileStream(path, FileMode.CreateNew));
    writer.Write(content.ToString());
}
