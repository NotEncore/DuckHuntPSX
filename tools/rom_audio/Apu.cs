namespace NesV15;

internal sealed class Apu2A03
{
	private static readonly int[] LengthLut =
	{
		10,254,20, 2,40, 4,80, 6,160, 8,60,10,14,12,26,14,
		12, 16,24,18,48,20,96,22,192,24,72,26,16,28,32,30
	};
	private static readonly int[] NoiseLut =
		{ 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
	private static readonly int[] Duty = { 1, 2, 4, 6 };

	private readonly byte[] r = new byte[0x18];
	private int p1t, p2t, nt, tt, p1seq, p2seq, tseq, lfsr = 1;
	private int p1len, p2len, nlen, tlen, tlin;
	private int p1env, p2env, nenv, p1div, p2div, ndiv;
	private bool p1start, p2start, nstart, treload;
	private int sweep1, sweep2, frameStep, frameTimer;

	public void Write(int addr, int val)
	{
		if (addr < 0x4000 || addr > 0x4017) return;
		val &= 0xFF;
		if (addr <= 0x4015 || addr == 0x4017)
			r[addr - 0x4000] = (byte)val;
		switch (addr)
		{
			case 0x4003:
				p1t = Period(r[2], r[3]);
				p1len = LengthLut[(val >> 3) & 0x1F];
				p1start = true;
				p1seq = 0;
				break;
			case 0x4007:
				p2t = Period(r[6], r[7]);
				p2len = LengthLut[(val >> 3) & 0x1F];
				p2start = true;
				p2seq = 0;
				break;
			case 0x400B:
				tt = Period(r[0xA], r[0xB]) + 1;
				tlen = LengthLut[(val >> 3) & 0x1F];
				treload = true;
				break;
			case 0x400F:
				nt = NoiseLut[r[0xE] & 0x0F];
				nlen = LengthLut[(val >> 3) & 0x1F];
				nstart = true;
				break;
			case 0x4008:
				if ((val & 0x80) != 0) tlin = val & 0x7F;
				treload = true;
				break;
			case 0x4015:
				if ((val & 1) == 0) p1len = 0;
				if ((val & 2) == 0) p2len = 0;
				if ((val & 4) == 0) tlen = 0;
				if ((val & 8) == 0) nlen = 0;
				break;
			case 0x4017:
				frameStep = 0;
				frameTimer = 0;
				break;
		}
	}

	private static int Period(int lo, int hi) => lo | ((hi & 7) << 8);

	public int Clock()
	{
		int p1per = Period(r[2], r[3]) + 1;
		int p2per = Period(r[6], r[7]) + 1;
		if (p1per > 8 && --p1t <= 0) { p1t = p1per * 2; p1seq = (p1seq + 1) & 7; }
		if (p2per > 8 && --p2t <= 0) { p2t = p2per * 2; p2seq = (p2seq + 1) & 7; }
		int tper = Period(r[0xA], r[0xB]) + 1;
		if (tper > 1 && --tt <= 0) { tt = tper; tseq = (tseq + 1) % 32; }
		int nper = NoiseLut[r[0xE] & 0x0F];
		if (--nt <= 0)
		{
			nt = Math.Max(1, nper);
			int bit = (r[0xE] & 0x80) != 0 ? ((lfsr ^ (lfsr >> 6)) & 1) : ((lfsr ^ (lfsr >> 1)) & 1);
			lfsr = (lfsr >> 1) | (bit << 14);
		}

		frameTimer++;
		int quarter = (r[0x17] & 0x80) != 0 ? 7456 : 7457;
		if (frameTimer >= quarter)
		{
			frameTimer = 0;
			Quarter();
			if ((r[0x17] & 0x80) != 0)
			{
				if (frameStep == 1 || frameStep == 4) Half();
				frameStep = (frameStep + 1) % 5;
			}
			else
			{
				if (frameStep == 1 || frameStep == 3) Half();
				frameStep = (frameStep + 1) & 3;
			}
		}

		int v1 = EnvVol(r[0], p1env, p1len);
		int v2 = EnvVol(r[4], p2env, p2len);
		int vn = EnvVol(r[0xC], nenv, nlen);
		int s1 = ((r[0x15] & 1) != 0 && p1len > 0 && p1seq < Duty[(r[0] >> 6) & 3]) ? v1 : 0;
		int s2 = ((r[0x15] & 2) != 0 && p2len > 0 && p2seq < Duty[(r[4] >> 6) & 3]) ? v2 : 0;
		int tri = ((r[0x15] & 4) != 0 && tlen > 0 && tlin > 0) ? (tseq < 16 ? 15 - tseq : tseq - 16) : 0;
		int noi = ((r[0x15] & 8) != 0 && nlen > 0 && (lfsr & 1) == 0) ? vn : 0;
		double pulse = 95.88 / ((8128.0 / Math.Max(1e-9, s1 + s2)) + 100);
		double tnd = 159.79 / ((1 / ((tri / 8227.0) + (noi / 12241.0) + 1e-9)) + 100);
		return (int)((pulse + tnd - 0.25) * 40000);
	}

	private static int EnvVol(int rx, int env, int len)
	{
		if (len <= 0) return 0;
		return (rx & 0x10) != 0 ? (rx & 0x0F) : env;
	}

	private void Quarter()
	{
		ClockEnv(r[0], ref p1env, ref p1div, ref p1start);
		ClockEnv(r[4], ref p2env, ref p2div, ref p2start);
		ClockEnv(r[0xC], ref nenv, ref ndiv, ref nstart);
		if (treload) { tlin = r[8] & 0x7F; treload = (r[8] & 0x80) != 0; }
		else if (tlin > 0) tlin--;
	}

	private void Half()
	{
		if ((r[0] & 0x20) == 0 && p1len > 0) p1len--;
		if ((r[4] & 0x20) == 0 && p2len > 0) p2len--;
		if ((r[0xC] & 0x20) == 0 && nlen > 0) nlen--;
		if ((r[8] & 0x80) == 0 && tlen > 0) tlen--;
		Sweep(true, ref p1t, r[1], r[2], r[3], ref sweep1, 2);
		Sweep(false, ref p2t, r[5], r[6], r[7], ref sweep2, 6);
	}

	private static void ClockEnv(int rx, ref int env, ref int div, ref bool start)
	{
		if (start) { start = false; env = 15; div = rx & 0x0F; return; }
		if (div > 0) { div--; return; }
		div = rx & 0x0F;
		if (env > 0) env--;
		else if ((rx & 0x20) != 0) env = 15;
	}

	private void Sweep(bool pulse1, ref int timer, int r1, int lo, int hi, ref int div, int reg)
	{
		if ((r1 & 0x80) == 0) return;
		if (div > 0) { div--; return; }
		div = (r1 >> 4) & 7;
		int shift = r1 & 7;
		if (shift == 0) return;
		int period = lo | ((hi & 7) << 8);
		int delta = period >> shift;
		if ((r1 & 8) != 0) delta = pulse1 ? -delta - 1 : -delta;
		int target = period + delta;
		if (target < 0 || target > 0x7FF) return;
		r[reg] = (byte)target; r[reg + 1] = (byte)((hi & 0xf8) | (target >> 8));
	}
}



