class Encoder {
static readonly int[] F0 = { 0, 60, 115, 98, 122 };
static readonly int[] F1 = { 0, 0, -52, -55, -60 };
public static byte[] EncodePcm22050(short[] pcm, out double err)
	{
		return Encode(pcm, false, out err);
	}

	static byte[] Encode(short[] pcm, bool loop, out double err)
	{
		int blocks = (pcm.Length + 27) / 28;
		var dst = new byte[blocks * 16];
		int h1 = 0, h2 = 0;
		long acc = 0;
		int n = 0;
		for (int blk = 0; blk < blocks; blk++)
		{
			int bestF = 0, bestS = 0;
			long bestE = long.MaxValue;
			int bh1 = 0, bh2 = 0;
			var nib = new int[28];
			var bestN = new int[28];
			for (int f = 0; f < 5; f++)
			{
				for (int sh = 0; sh <= 12; sh++)
				{
					int p1 = h1, p2 = h2;
					long e = 0;
					for (int i = 0; i < 28; i++)
					{
						int s = blk * 28 + i < pcm.Length ? pcm[blk * 28 + i] : 0;
						int pred = (p1 * F0[f] + p2 * F1[f] + 32) >> 6;
						int diff = s - pred;
						/* PS1: (nibble << 12) >> shift. Not nibble << shift. */
						int gain = 12 - sh;
						int q;
						if (gain <= 0)
							q = diff;
						else if (diff >= 0)
							q = (diff + (1 << (gain - 1))) >> gain;
						else
							q = -(((-diff) + (1 << (gain - 1))) >> gain);
						if (q > 7) q = 7;
						if (q < -8) q = -8;
						int y = pred + ((q << 12) >> sh);
						if (y > 32767) y = 32767;
						if (y < -32768) y = -32768;
						int d = s - y;
						e += (long)d * d;
						nib[i] = q & 15;
						p2 = p1;
						p1 = y;
					}
					if (e < bestE)
					{
						bestE = e;
						bestF = f;
						bestS = sh;
						bh1 = p1;
						bh2 = p2;
						Array.Copy(nib, bestN, 28);
					}
				}
			}
			h1 = bh1;
			h2 = bh2;
			acc += bestE;
			n += 28;
			int dp = blk * 16;
			dst[dp] = (byte)((bestF << 4) | bestS);
			byte flags = 0;
			if (blk == 0 && loop) flags |= 4;
			if (blk == blocks - 1) flags |= (byte)(loop ? 3 : 1);
			dst[dp + 1] = flags;
			for (int i = 0; i < 14; i++)
				dst[dp + 2 + i] = (byte)(bestN[i * 2] | (bestN[i * 2 + 1] << 4));
		}
		err = Math.Sqrt(acc / (double)Math.Max(1, n));
		return dst;
	}

	
}
