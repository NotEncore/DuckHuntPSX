using System;

internal static class SpuAdpcm
{
    private const int SamplesPerBlock = 28;
    private const int BytesPerBlock = 16;
    private static readonly (int Recent, int Older)[] Predictors =
        { (0, 0), (60, 0), (115, -52), (98, -55), (122, -60) };

    // Search the documented decoder's filter and range space using squared PCM error.
    public static byte[] Encode(ReadOnlySpan<short> samples, int? loopBlock = null)
    {
        int blocks = Math.Max(1, (samples.Length + SamplesPerBlock - 1) / SamplesPerBlock);
        if (loopBlock is int start && (start < 0 || start >= blocks))
            throw new ArgumentOutOfRangeException(nameof(loopBlock));

        byte[] result = new byte[(blocks + (loopBlock is null ? 1 : 0)) * BytesPerBlock];
        Span<short> pcm = stackalloc short[SamplesPerBlock];
        Span<byte> trial = stackalloc byte[BytesPerBlock];
        int recent = 0, older = 0;

        for (int block = 0; block < blocks; block++)
        {
            pcm.Clear();
            int offset = block * SamplesPerBlock;
            samples.Slice(offset, Math.Min(SamplesPerBlock, samples.Length - offset)).CopyTo(pcm);
            Span<byte> output = result.AsSpan(block * BytesPerBlock, BytesPerBlock);
            long bestError = long.MaxValue;
            int bestRecent = recent, bestOlder = older;

            for (int filter = 0; filter < Predictors.Length; filter++)
            for (int shift = 0; shift <= 12; shift++)
            {
                trial.Clear();
                int step = 1 << (12 - shift);
                int a = recent, b = older;
                long error = 0;
                for (int i = 0; i < SamplesPerBlock; i++)
                {
                    int prediction = Predict(filter, a, b);
                    int residual = pcm[i] - prediction;
                    int magnitude = (Math.Abs(residual) + step / 2) / step;
                    int nibble = Math.Clamp(Math.Sign(residual) * magnitude, -8, 7);
                    int sample = Math.Clamp(prediction + nibble * step, short.MinValue, short.MaxValue);
                    long difference = (long)pcm[i] - sample;
                    error += difference * difference;
                    trial[2 + i / 2] |= (byte)((nibble & 15) << (4 * (i % 2)));
                    b = a;
                    a = sample;
                }
                if (error >= bestError) continue;
                bestError = error;
                trial[0] = (byte)((filter << 4) | shift);
                trial.CopyTo(output);
                bestRecent = a;
                bestOlder = b;
            }
            recent = bestRecent;
            older = bestOlder;
            if (loopBlock == block) output[1] = 4;
            if (loopBlock is not null && block == blocks - 1) output[1] |= 3;
        }

        if (loopBlock is null) result[^15] = 7;
        return result;
    }

    private static int Predict(int filter, int recent, int older)
    {
        var coefficients = Predictors[filter];
        return (coefficients.Recent * recent + coefficients.Older * older + 32) >> 6;
    }
}
