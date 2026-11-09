// SPDX-License-Identifier: GPL-3.0-or-later
// Mesen ABI reference: Mesen 2.1.1, Copyright (C) 2014-2025 Sour.
// See LICENSE-MESEN-WORKER.md and MESEN-NOTICE.md.
using System.Runtime.InteropServices;

/// <summary>
/// Capture authentic NES mixer PCM via Mesen 2.1.1 MesenCore.dll WaveRecord.
///
/// Silence root cause (Mesen2 Core/Shared/SettingTypes.h NesConfig):
/// ChannelVolumes[11] defaults to {}. The GUI calls SetNesConfig with 100s.
/// Headless WaveRecord wrote a correctly-sized 48 kHz WAV of zeros because
/// every APU channel was multiplied by 0. Lua has no WaveRecord.
/// </summary>
internal static class Program
{
	const string Dll = "MesenCore.dll";
	const ushort KeyA = 4110;
	const ushort KeyStart = 4101;
	const ushort KeyZap = 4112; /* keyboard X — KeyManager tracks this; mouse 512 does not */
	const ushort MouseLeft = 512;
	const int MemNesCpu = 8;          /* MemoryType.NesMemory */
	const int MemNesRam = 46;         /* MemoryType.NesInternalRam */

	static int Main(string[] args)
	{
		string root = Directory.GetCurrentDirectory();
		string mode = "gunshot";
		if (args.Length > 0 && args[0] == "fine")
		{
			string isol0 = Path.Combine(root, @"captures\audio_reference\isolated");
			if (args.Length > 1 && Directory.Exists(args[1]))
				isol0 = Path.Combine(args[1], @"captures\audio_reference\isolated");
			FineDump(isol0);
			return 0;
		}
		uint pokeLatch = 0xF1;
		int pokeBit = 1;
		string? suppliedRom = null, suppliedCore = null, captureDirectory = null;
		foreach (string a in args)
		{
			if (a.StartsWith("--rom=", StringComparison.Ordinal))
				suppliedRom = Path.GetFullPath(a[6..]);
			else if (a.StartsWith("--core=", StringComparison.Ordinal))
				suppliedCore = Path.GetFullPath(a[7..]);
			else if (a.StartsWith("--capture-dir=", StringComparison.Ordinal))
				captureDirectory = Path.GetFullPath(a[14..]);
			else if (a is "audithit" or "auditmiss" or "title" or "round" or "rounddrv" or "gunshot" or "flap" or "bark" or "sfxscan"
				or "gameshot" or "gameshotn" or "fullshot" or "fullshotn"
				or "trace-perfect" or "trace-mixed" or "trace-fail" or "trace-gapped")
				mode = a;
			else if (Directory.Exists(a))
				root = a;
			else if (a.StartsWith("F", StringComparison.OrdinalIgnoreCase) && a.Length == 2
				&& uint.TryParse(a.AsSpan(1), System.Globalization.NumberStyles.HexNumber, null, out uint lat))
				pokeLatch = 0xF0 + lat;
			else if (int.TryParse(a, out int bit) && bit is 1 or 2 or 4 or 8 or 16 or 32 or 64 or 128)
				pokeBit = bit;
		}

		bool isolated = suppliedRom != null || suppliedCore != null || captureDirectory != null;
		bool traceMode = mode.StartsWith("trace-", StringComparison.Ordinal);
		if (isolated && (suppliedRom == null || suppliedCore == null || captureDirectory == null ||
			!(traceMode || mode is "sfxscan" or "title" or "round")))
		{
			Console.Error.WriteLine("Supply --rom, --core and --capture-dir together for title, round, sfxscan or trace capture.");
			return 2;
		}
		if (traceMode && !isolated)
		{
			Console.Error.WriteLine("Trace capture requires explicit ROM, core and capture directory paths.");
			return 2;
		}
		string rom = suppliedRom ?? Path.Combine(root, @"roms\DuckHunt.nes");
		string coreSrc = suppliedCore ?? Path.Combine(AppContext.BaseDirectory, Dll);
		string isol = captureDirectory ?? Path.Combine(root, @"captures\audio_reference\isolated");
		string home = isolated ? Path.Combine(isol, mode == "sfxscan" ? $"mesen-F{pokeLatch - 0xf0}_{pokeBit:00}" : $"mesen-{mode}") :
			Path.Combine(root, @"captures\audio_reference\mesen_home");
		Directory.CreateDirectory(home);
		Directory.CreateDirectory(isol);
		string settingsSrc = Path.Combine(root, "settings.json");
		string settingsDst = Path.Combine(home, "settings.json");
		if (!isolated && File.Exists(settingsSrc))
			File.Copy(settingsSrc, settingsDst, true);
		if (!File.Exists(rom) || !File.Exists(coreSrc))
		{
			Console.WriteLine("missing ROM or MesenCore.dll");
			return 2;
		}
		if (isolated)
		{
			IntPtr core = NativeLibrary.Load(coreSrc);
			NativeLibrary.SetDllImportResolver(typeof(Program).Assembly,
				(name, _, _) => name == Dll ? core : IntPtr.Zero);
		}
		else
		{
			string localCore = Path.Combine(AppContext.BaseDirectory, Dll);
			if (!string.Equals(coreSrc, localCore, StringComparison.OrdinalIgnoreCase))
				File.Copy(coreSrc, localCore, true);
			NativeLibrary.Load(localCore);
		}
		InitDll();
		if (isolated)
		{
			uint version = GetMesenVersion();
			Console.WriteLine($"Mesen core {version >> 16}.{(version >> 8) & 255}.{version & 255}");
		}
		InitializeEmu(home, IntPtr.Zero, IntPtr.Zero, true, false, true, false);
		ApplyAudio();
		SetDebuggerFlag(1u << 8, true); /* NesDebuggerEnabled */
		if (isolated)
			ApplyNesVolumes(false, deterministic: true);
		if (isolated)
		{
			// Mesen starts the worker during LoadRom. Request the debugger's
			// startup break before loading so the host cannot lose a frame.
			SetEmulationFlag(0x10, true); // ConsoleMode.
			Pause();
		}
		if (!LoadRom(rom, null))
		{
			Console.WriteLine("LoadRom failed");
			Release();
			return 3;
		}
		Console.WriteLine("sizeof KeyMapping=" + Marshal.SizeOf<InteropKeyMapping>()
			+ " KeySet=" + Marshal.SizeOf<InteropKeyMappingSet>()
			+ " Controller=" + Marshal.SizeOf<InteropControllerConfig>());
		ApplyNesVolumes(mode == "gameshotn" || mode == "fullshotn", mode == "fullshotn", isolated);
		UpdateInputDevices();
		if (traceMode)
			return CaptureGameATrace(mode[6..], home, isol);
		if (isolated && mode == "sfxscan")
			return CaptureClayCue(home, isol, pokeLatch, pokeBit);
		try
		{
			var chk = GetNesConfig();
			Console.WriteLine("after Set Port1.Type=" + chk.Port1.Type + " Port2.Type=" + chk.Port2.Type
				+ " A=" + chk.Port1.Keys.Mapping1.A + " Start=" + chk.Port1.Keys.Mapping1.Start
				+ " vol1=" + chk.Square1Volume);
		}
		catch (Exception ex) { Console.WriteLine("GetNesConfig " + ex.Message); }
		Console.WriteLine("NesConfig pack2 volumes=100 Port1=NesController Port2=NesZapper autoInput=true");
		try
		{
			Console.WriteLine("GetMemorySize NesMemory=" + GetMemorySize(MemNesCpu)
				+ " NesInternalRam=" + GetMemorySize(MemNesRam));
			Console.WriteLine("HasControlDevice pad=" + HasControlDevice(6) + " zapper=" + HasControlDevice(9));
			foreach (string kn in new[] { "A", "Enter", "Return", "Start", "Left Mouse Button", "Mouse Left", "LeftMouseButton" })
				Console.WriteLine("GetKeyCode " + kn + "=" + GetKeyCode(kn));
			var knbuf = new byte[64];
			foreach (ushort kc in new ushort[] { 512, 513, 4110, 4101 })
			{
				GetKeyName(kc, knbuf, knbuf.Length);
				int z = Array.IndexOf(knbuf, (byte)0);
				Console.WriteLine("GetKeyName " + kc + "=" + System.Text.Encoding.UTF8.GetString(knbuf, 0, z < 0 ? 0 : z));
			}
		}
		catch (Exception ex) { Console.WriteLine("probe " + ex.Message); }

		string luaPath = Path.Combine(root, mode.StartsWith("audit") ? @"tools\mesen_wave\game_a_audit.lua" : mode == "round" || mode == "rounddrv"
			? @"tools\mesen_wave\round_start.lua"
			: @"tools\mesen_wave\game_a_input.lua");
		int scriptId = -1;
		if (isolated && mode != "title")
		{
			string input = """
				local frame = 0
				emu.addEventCallback(function()
					local start = frame >= START_FRAME and frame < RELEASE_FRAME
					emu.setInput({start = start, a = start}, 0, 0)
				end, emu.eventType.inputPolled)
				emu.addEventCallback(function() frame = frame + 1 end, emu.eventType.endFrame)
				""".Replace("START_FRAME", mode == "round" ? "250" : "80")
				.Replace("RELEASE_FRAME", mode == "round" ? "380" : "220");
			scriptId = LoadScript("user-rom-capture", Path.Combine(home, "input.lua"), input, -1);
			if (scriptId < 0)
			{
				Console.Error.WriteLine("Could not load the local capture input script.");
				Release();
				return 5;
			}
		}
		/* Title-only must not load Start+A (game_a_input.lua holds it on
		 * frames 80-219, during the boot jingle). Round mode loads
		 * round_start.lua (dual-port Start+A after the jingle). */
		if (!isolated && mode != "title" && File.Exists(luaPath))
		{
			string lua = File.ReadAllText(luaPath);
			if(mode.StartsWith("audit")) lua=lua.Replace("__AUDIT_HIT__",mode=="audithit"?"true":"false");
			string luaName = Path.GetFileNameWithoutExtension(luaPath);
			scriptId = LoadScript(luaName, luaPath, lua, -1);
			Console.WriteLine("LoadScript id=" + scriptId + " " + luaName);
			try
			{
				var slog = new byte[4000];
				GetScriptLog(scriptId, slog, slog.Length);
				int z = Array.IndexOf(slog, (byte)0);
				if (z > 0)
					Console.WriteLine("scriptLog " + System.Text.Encoding.UTF8.GetString(slog, 0, z));
			}
			catch { }
		}

		string tag = mode == "sfxscan"
			? "sfxscan_F" + (pokeLatch - 0xF0) + "_" + pokeBit.ToString("00")
			: mode;
		string wav = Path.Combine(isol, tag + "_session.wav");
		try { if (File.Exists(wav)) File.Delete(wav); } catch { }

		WaveRecord(wav);
		Console.WriteLine("WaveRecord " + wav + " rec=" + WaveIsRecording());
		if (isolated)
			Resume();

		var log = new System.Text.StringBuilder();
		log.AppendLine("mode=" + mode);
		log.AppendLine("frame,st,y,x,ba,f0,f1,b0,b5,b6,b7,b8,b9,be,aa,c2");
		int fired = -1;
		int accepted = -1;
		int baAtFire = -1;
		int injectAt = -1;
		uint lastFr = 0;
		byte[] zp = new byte[4096];
		var t0 = DateTime.UtcNow;
		int maxSec = isolated ? 120 : mode.StartsWith("audit") ? 42 : mode == "title" ? 8 : mode is "round" or "rounddrv" ? 14
			: mode is "sfxscan" or "gameshot" or "gameshotn" or "fullshot" or "fullshotn" ? 22 : 22;
		int pokedAt = -1;
		int lastDriverFr = -1;
		int auditBatch = -1;
		string auditTail = "";
		var auditCollected = new System.Text.StringBuilder();
		void CollectAudit() {
			var buffer = new byte[4 * 1024 * 1024]; GetScriptLog(scriptId, buffer, buffer.Length);
			int z = Array.IndexOf(buffer, (byte)0);
			string text = System.Text.Encoding.UTF8.GetString(buffer, 0, z < 0 ? buffer.Length : z);
			int at = auditTail.Length > 0 ? text.LastIndexOf(auditTail, StringComparison.Ordinal) : -1;
			auditCollected.Append(at < 0 ? text : text.Substring(at + auditTail.Length));
			auditTail = text.Length > 128 ? text.Substring(text.Length - 128) : text;
		}
		var driver = new System.Text.StringBuilder();
		driver.AppendLine("frame,st,f0,f3,ec,ed,a24,a26");
		try
		{
			while ((DateTime.UtcNow - t0).TotalSeconds < maxSec)
			{
				uint fr = SafeFrame(ref lastFr);
				if (mode.StartsWith("audit") && (int)fr / 60 != auditBatch) { CollectAudit(); auditBatch = (int)fr / 60; }
				ReadZp(zp, out int st, out int y, out int x, out int ba, out int f0, out int f1,
					out int b0, out int b5, out int b6, out int b7, out int b8, out int b9, out int be,
					out int aa, out int c2);
				int drvF2 = 0, drvF3 = 0;
				try { drvF2 = Ram(0xF2); drvF3 = Ram(0xF3); } catch { }
				bool dense = mode == "rounddrv";
				if (dense)
				{
					if (fr == 0)
						log.AppendLine("frame,st,y,x,ba,f0,f1,f2,f3,b0,b9,be,aa,c2");
					if (fr >= 240 || st != 0)
						log.AppendLine(string.Join(",", fr, st, y, x, ba, f0, f1, drvF2, drvF3, b0, b9, be, aa, c2));
				}
				else if (fr % 30 == 0 || (st != 0 && fr % 5 == 0) || (fired >= 0 && (int)fr <= fired + 30))
					log.AppendLine(string.Join(",", fr, st, y, x, ba, f0, f1, b0, b5, b6, b7, b8, b9, be, aa, c2));

				if (mode == "title")
				{
					if (fr > 400)
						break;
					continue;
				}

				/* After boot jingle (~3.9 s / fr 234), then Start+A.
				 * Stop before typical visible flight (~640). C# keys are
				 * backup; round_start.lua dual-port setInput is required. */
				if (mode == "round" || mode == "rounddrv")
				{
					bool startGame = fr >= 250 && fr < 380;
					SetKeyState(KeyStart, startGame);
					SetKeyState(KeyA, startGame);
					if (fr > 620)
						break;
					continue;
				}

				if (mode == "sfxscan" || mode == "fullshot" || mode == "fullshotn")
				{
					bool bootScan = fr >= 80 && fr < 220;
					SetKeyState(KeyStart, bootScan);
					SetKeyState(KeyA, bootScan);
					bool full = mode == "fullshot" || mode == "fullshotn";
					int hold = full ? 200 : 50;
					if (full)
					{
						pokeLatch = 0xF0;
						pokeBit = 2;
					}
					int a24 = Ram(0x24);
					int a26 = Ram(0x26);
					int f3 = Ram(0xF3);
					int ec = Ram(0xEC);
					int ed = Ram(0xED);
					/* st=2 at fr=520 is the dog-jump landing — bark still decaying.
					   Wait until the ready walk is settled, or until flight. */
					bool quietReady = full && fr >= 560 && st >= 1 && st <= 5;
					bool inFlight = full && fr >= 640 && st == 3;
					bool scanReady = !full && fr >= 520 && (st == 2 || (st >= 1 && st <= 5 && b9 != 0));
					if (pokedAt < 0 && (quietReady || inFlight || scanReady))
					{
						SetMemoryValue(MemNesRam, pokeLatch, (byte)pokeBit);
						SetMemoryValue(MemNesCpu, pokeLatch, (byte)pokeBit);
						pokedAt = (int)fr;
						log.AppendLine("POKE " + fr + " $" + pokeLatch.ToString("X2") + "=" + pokeBit
							+ " st=" + st + " b9=" + b9 + " f3=" + f3
							+ " a24=" + a24 + " a26=" + a26 + " ec=" + ec + " ed=" + ed);
					}
					if (full && pokedAt >= 0 && (int)fr != lastDriverFr)
					{
						lastDriverFr = (int)fr;
						driver.AppendLine(string.Join(",", fr, st, f0, f3, ec, ed, a24, a26));
						if ((int)fr == pokedAt || (int)fr == pokedAt + 1 || (int)fr % 10 == 0)
							log.AppendLine("DRIVER " + fr + " f0=" + f0 + " f3=" + f3
								+ " ec=" + ec + " ed=" + ed + " a24=" + a24 + " a26=" + a26);
					}
					if (pokedAt >= 0 && (int)fr >= pokedAt + hold)
						break;
					continue;
				}

				bool boot = fr >= 80 && fr < 220;
				SetKeyState(KeyStart, boot);
				SetKeyState(KeyA, boot);

				bool fly = st == 3 && y < 0x92 && y >= 0x18;
				if (mode.StartsWith("audit"))
				{
					/* Zapper is driven frame-exactly via Lua setInput, not host key polling. */
					if (fr >= 2200) break;
					continue; /* No RAM injection in either audit mode. */
				}
				if (mode is "gameshot" or "gameshotn")
				{
					if (fired < 0 && fly && fr > 620 && ba > 0)
					{
						fired = (int)fr;
						baAtFire = ba;
						SetMousePosition(0.08, 0.08);
						log.AppendLine("FIRE " + fr + " ba=" + ba + " y=" + y + " x=" + x
							+ " b5=" + b5 + " b6=" + b6 + " b7=" + b7 + " b9=" + b9
							+ " f0=" + f0 + " be=" + be);
					}
					if (fired >= 0)
					{
						int dt = (int)fr - fired;
						bool zapOn = dt < 3 || (dt >= 6 && dt < 9);
						SetKeyState(KeyZap, zapOn);
						SetKeyState(MouseLeft, zapOn);
						SetMousePosition(0.08, 0.08);
						if (accepted < 0 && ba >= 0 && baAtFire >= 0 && ba < baAtFire)
						{
							accepted = (int)fr;
							log.AppendLine("ACCEPT " + fr + " ba=" + ba + " from=" + baAtFire
								+ " b5=" + b5 + " b7=" + b7 + " b9=" + b9
								+ " f0=" + f0 + " be=" + be
								+ (injectAt >= 0 ? " via=inject" : " via=zapper"));
						}
						if (accepted < 0 && dt == 12)
						{
							if (b9 == 0)
							{
								SetMemoryValue(MemNesRam, 0xB9, 1);
								SetMemoryValue(MemNesCpu, 0xB9, 1);
							}
							SetMemoryValue(MemNesRam, 0xB5, 1);
							SetMemoryValue(MemNesCpu, 0xB5, 1);
							injectAt = (int)fr;
							log.AppendLine("INJECT_B5 " + fr + " b9was=" + b9 + " ba=" + ba);
						}
						if (accepted < 0 && dt >= 13 && dt <= 16)
						{
							if (b9 == 0)
							{
								SetMemoryValue(MemNesRam, 0xB9, 1);
								SetMemoryValue(MemNesCpu, 0xB9, 1);
							}
							SetMemoryValue(MemNesRam, 0xB5, 1);
							SetMemoryValue(MemNesCpu, 0xB5, 1);
						}
						if (accepted >= 0 && (int)fr >= accepted + 40)
							break;
						if (dt >= 90)
							break;
					}
					if (fr > 1400)
						break;
					continue;
				}
				if (mode == "bark")
				{
					/* dog jump is $31=9 during intro; just let the session run */
					if (fr > 700)
						break;
					continue;
				}

				if (fired < 0 && fly && fr > 620 && (mode != "flap" || fr > 700))
				{
					if (mode == "flap")
					{
						if (fr > 900)
							break;
						continue;
					}
					double nx = Math.Clamp(x / 256.0, 0.02, 0.98);
					double ny = Math.Clamp(y / 240.0, 0.02, 0.98);
					SetMousePosition(nx, ny);
					SetKeyState(MouseLeft, true);
					SetKeyState(513, true);
					fired = (int)fr;
					log.AppendLine("FIRE " + fr + " ba=" + ba + " y=" + y + " x=" + x
						+ " nx=" + nx.ToString("0.000") + " ny=" + ny.ToString("0.000")
						+ " b7=" + b7 + " f0=" + f0 + " f1=" + f1);
				}
				else if (fired >= 0 && (int)fr == fired + 12)
				{
					SetKeyState(MouseLeft, false);
					SetKeyState(513, false);
				}

				if (fired >= 0 && (int)fr >= fired + 90)
					break;
				if (fr > 1400)
					break;
			}
		}
		catch (Exception ex)
		{
			Console.WriteLine("loop " + ex.Message);
			log.AppendLine(ex.ToString());
		}

		if (mode.StartsWith("audit") && scriptId >= 0) {
			CollectAudit();
			File.WriteAllText(Path.Combine(isol, mode + "_lua.log"), auditCollected.ToString());
		}
		try { WaveStop(); } catch { }
		try { SetKeyState(KeyStart, false); } catch { }
		try { SetKeyState(KeyA, false); } catch { }
		try { SetKeyState(MouseLeft, false); } catch { }
		try { SetKeyState(KeyZap, false); } catch { }
		try { Stop(); } catch { }
		try { Release(); } catch { }

		int peak = File.Exists(wav) ? PeakOf(wav) : 0;
		long bytes = File.Exists(wav) ? new FileInfo(wav).Length : 0;
		Console.WriteLine("frames=" + lastFr + " fired=" + fired + " accepted=" + accepted
			+ " inject=" + injectAt + " wav=" + bytes + " peak=" + peak);
		log.AppendLine("# frames=" + lastFr + " fired=" + fired + " accepted=" + accepted
			+ " inject=" + injectAt + " baAtFire=" + baAtFire + " peak=" + peak + " bytes=" + bytes);
		if (File.Exists(wav))
			log.Append(WindowPeaks(wav, lastFr, fired));
		log.AppendLine("method=MesenCore.dll WaveRecord after SetNesConfig ChannelVolumes=100");
		log.AppendLine("rom=" + rom);
		File.WriteAllText(Path.Combine(isol, tag + "_session.log"), log.ToString());
		if (mode == "fullshot" || mode == "fullshotn")
			File.WriteAllText(Path.Combine(isol, tag + "_driver.csv"), driver.ToString());
		if (isolated && mode == "sfxscan" && pokedAt < 0)
		{
			Console.Error.WriteLine("The selected sound request never fired.");
			return 5;
		}
		if (peak < 200)
		{
			Console.WriteLine("SILENT — mixer still not producing samples");
			return 5;
		}
		Console.WriteLine("NONSILENT mixer WAV");
		return 0;
	}

	static int CaptureGameATrace(string profile, string home, string output)
	{
		const string template = """
			local profile = "PROFILE"
			local f, shot = -1, -100
			local seq, buffer = 0, {}
			local function rd(a) return emu.read(a, emu.memType.nesInternalRam) end
			local function emit(line)
				if f>8000 then return end
				seq = seq + 1
				buffer[#buffer + 1] = "TRACE " .. seq .. " " .. line
			end
			emu.addEventCallback(function()
				emu.setInput({start=f>=80 and f<220,a=f>=80 and f<220},0,0)
				local bird = rd(0x3b)
				local target = profile == "perfect" or (profile == "fail" and bird == 0)
					or (profile == "mixed" and bird <= 7) or (profile == "gapped" and bird >= 5)
				if target and f>610 and rd(0x31)==3 and rd(0xba)>0 and f-shot>40
					and rd(0x32)<138 and rd(0x33)>20 and rd(0x33)<220 then
					shot=f
					emit("INPUT " .. f)
				end
				local aim = profile == "perfect" or (profile == "mixed" and bird<=6 and rd(0xaa)<6)
					or (profile == "gapped" and bird>=5 and rd(0xaa)<6)
				emu.setInput({trigger=f-shot<12,x=aim and rd(0x33)+12 or 1,
					y=aim and rd(0x32)+12 or 1},0,1)
			end,emu.eventType.inputPolled)
			emu.addMemoryCallback(function(a,v)
				emit(string.format("APU %d,%04X,%02X",f,a,v))
			end,emu.callbackType.write,0x4000,0x4017)
			emu.addMemoryCallback(function() emit("HIT " .. f) end,emu.callbackType.exec,0xcc1f,0xcc1f)
			emu.addMemoryCallback(function() emit("TICK " .. f) end,emu.callbackType.exec,0xd519,0xd519)
			emu.addEventCallback(function()
				f=f+1
				if f==8000 then emit("END " .. f) end
				if f%30==0 and #buffer>0 then
					emu.log(table.concat(buffer,"\n"))
					buffer={}
				end
				if f==8000 then
					emu.log(table.concat(buffer,"\n"))
				end
			end,emu.eventType.endFrame)
			""";
		string path = Path.Combine(output, profile + ".trace");
		try
		{
			int id = LoadScript("rom-audio-trace", Path.Combine(home, "trace.lua"),
				template.Replace("PROFILE", profile), -1);
			if (id < 0)
				throw new InvalidOperationException("Mesen did not load the trace script.");
			SetEmulationFlag(4, true); // Mesen's MaximumSpeed flag.
			Resume();
			using var writer = new StreamWriter(new FileStream(path, FileMode.CreateNew));
			byte[] bytes = new byte[4 * 1024 * 1024];
			int last = 0;
			var deadline = DateTime.UtcNow.AddMinutes(3);
			while (DateTime.UtcNow < deadline)
			{
				Thread.Sleep(200);
				GetScriptLog(id, bytes, bytes.Length);
				int length = Array.IndexOf(bytes, (byte)0);
				foreach (string line in System.Text.Encoding.UTF8
					.GetString(bytes, 0, length < 0 ? bytes.Length : length).Split('\n'))
				{
					if (!line.StartsWith("TRACE ", StringComparison.Ordinal))
						continue;
					int split = line.IndexOf(' ', 6);
					if (split < 0 || !int.TryParse(line.AsSpan(6, split - 6), out int sequence) ||
						sequence <= last)
						continue;
					if (sequence != last + 1)
						throw new InvalidDataException($"Missing APU records after {last}.");
					last = sequence;
					string record = line[(split + 1)..].TrimEnd('\r');
					writer.WriteLine(record);
					if (record.StartsWith("END ", StringComparison.Ordinal))
					{
						Console.WriteLine($"Saved {last} records to {path}");
						return 0;
					}
				}
				writer.Flush();
			}
			throw new TimeoutException("Mesen did not finish the APU trace.");
		}
		finally
		{
			Stop();
			Release();
		}
	}

	static int CaptureClayCue(string home, string output, uint latch, int bit)
	{
		const string script = """
			local frame = 0
			local writes = {}
			local ram = emu.memType.nesInternalRam
			emu.addEventCallback(function()
				local start = frame >= 80 and frame < 220
				emu.setInput({start = start, a = start}, 0, 0)
			end, emu.eventType.inputPolled)
			emu.addMemoryCallback(function(address, value)
				if frame >= 519 and frame < 570 then
					writes[#writes + 1] = string.format("%d,%04X,%02X", frame, address, value)
				end
			end, emu.callbackType.write, 0x4000, 0x4017)
			emu.addEventCallback(function()
				frame = frame + 1
				if frame == 520 then
					local state = emu.read(0x31, ram)
					local active = emu.read(0xb9, ram)
					if state ~= 2 or active == 0 then
						emu.log(string.format("INVALID CLAY STATE %d,%d", state, active))
						emu.breakExecution()
						return
					end
					emu.write(LATCH, VALUE, ram)
					emu.log("CLAY REQUEST 520")
				elseif frame == 570 then
					emu.log(table.concat(writes, "\n"))
					emu.log("CLAY COMPLETE 570")
					emu.breakExecution()
				end
			end, emu.eventType.endFrame)
			""";
		string stem = Path.Combine(output, $"sfxscan_F{latch - 0xf0}_{bit:00}_session");
		try
		{
			int id = LoadScript("clay-cue", Path.Combine(home, "clay.lua"),
				script.Replace("LATCH", latch.ToString()).Replace("VALUE", bit.ToString()), -1);
			if (id < 0)
				throw new InvalidOperationException("Mesen did not load the clay capture script.");
			WaveRecord(stem + ".wav");
			if (!WaveIsRecording())
				throw new InvalidOperationException("Mesen did not start the clay WAV recording.");
			Resume();
			var log = new byte[65536];
			var timer = System.Diagnostics.Stopwatch.StartNew();
			while (timer.Elapsed < TimeSpan.FromSeconds(120))
			{
				Thread.Sleep(100);
				GetScriptLog(id, log, log.Length);
				int end = Array.IndexOf(log, (byte)0);
				string events = System.Text.Encoding.UTF8.GetString(log, 0, end < 0 ? log.Length : end);
				if (events.Contains("INVALID CLAY STATE"))
					throw new InvalidDataException(events);
				if (!events.Contains("CLAY REQUEST 520") || !events.Contains("CLAY COMPLETE 570") || !IsPaused())
					continue;
				WaveStop();
				File.WriteAllText(stem + ".trace", events);
				return 0;
			}
			throw new TimeoutException("Mesen did not reach the clay cue capture boundary.");
		}
		finally
		{
			if (WaveIsRecording())
				WaveStop();
			Stop();
			Release();
		}
	}
	static void ApplyAudio()
	{
		var a = new InteropAudioConfig
		{
			AudioDevice = "",
			EnableAudio = true,
			MasterVolume = 100,
			SampleRate = 48000,
			AudioLatency = 60,
			MuteSoundInBackground = false,
			ReduceSoundInBackground = false,
			VolumeReduction = 0
		};
		SetAudioConfig(a);
	}

	static void EnsureKeys(ref InteropControllerConfig c)
	{
		c.Keys.Mapping1.CustomKeys ??= new ushort[100];
		c.Keys.Mapping2.CustomKeys ??= new ushort[100];
		c.Keys.Mapping3.CustomKeys ??= new ushort[100];
		c.Keys.Mapping4.CustomKeys ??= new ushort[100];
	}

	static void ApplyNesVolumes(bool noiseOnly, bool keepDmc = false, bool deterministic = false)
	{
		InteropNesConfig cfg = GetNesConfig();
		EnsureKeys(ref cfg.Port1);
		EnsureKeys(ref cfg.Port2);
		EnsureKeys(ref cfg.ExpPort);
		EnsureKeys(ref cfg.Port1A);
		EnsureKeys(ref cfg.Port1B);
		EnsureKeys(ref cfg.Port1C);
		EnsureKeys(ref cfg.Port1D);
		EnsureKeys(ref cfg.ExpPortA);
		EnsureKeys(ref cfg.ExpPortB);
		EnsureKeys(ref cfg.ExpPortC);
		EnsureKeys(ref cfg.ExpPortD);
		EnsureKeys(ref cfg.MapperInput);
		cfg.Port1.Type = 6; /* NesController */
		cfg.Port1.Keys.Mapping1.A = KeyA;
		cfg.Port1.Keys.Mapping1.Start = KeyStart;
		cfg.Port2.Type = 9; /* NesZapper */
		/* Keyboard X reaches KeyManager. Mouse 512 does not, so $4017.D4 stayed 0. */
		cfg.Port2.Keys.Mapping1.CustomKeys[0] = KeyZap;
		cfg.Port2.Keys.Mapping1.CustomKeys[1] = 513;
		cfg.SpritesEnabled = true;
		cfg.BackgroundEnabled = true;
		cfg.AutoConfigureInput = !deterministic;
		if (deterministic)
		{
			cfg.RamPowerOnState = 1; // AllZeros
			cfg.RandomizeCpuPpuAlignment = false;
			cfg.RandomizeMapperPowerOnState = false;
		}
		uint tone = noiseOnly ? 0u : 100u;
		cfg.Square1Volume = tone;
		cfg.Square2Volume = tone;
		cfg.TriangleVolume = tone;
		cfg.NoiseVolume = 100;
		/* $4011 DAC pop is the DMC channel. Keep it for a complete gunshot. */
		cfg.DmcVolume = (noiseOnly && !keepDmc) ? 0u : 100u;
		cfg.FdsVolume = 100;
		cfg.Mmc5Volume = 100;
		cfg.Vrc6Volume = 100;
		cfg.Vrc7Volume = 100;
		cfg.Namco163Volume = 100;
		cfg.Sunsoft5bVolume = 100;
		cfg.EpsmVolume = 100;
		if (cfg.UserPalette == null || cfg.UserPalette.Length != 512)
			cfg.UserPalette = new uint[512];
		SetNesConfig(cfg);
	}

	static uint SafeFrame(ref uint last)
	{
		try
		{
			uint fr = GetTimingInfo(8).FrameCount;
			if (fr == last)
				Thread.Sleep(2);
			else
				last = fr;
			return last;
		}
		catch
		{
			last++;
			return last;
		}
	}

	static int Ram(uint addr)
	{
		int v = GetMemoryValue(MemNesRam, addr);
		if (v != 0) return v;
		return GetMemoryValue(MemNesCpu, addr);
	}

	static void ReadZp(byte[] zp, out int st, out int y, out int x, out int ba, out int f0, out int f1,
		out int b0, out int b5, out int b6, out int b7, out int b8, out int b9, out int be,
		out int aa, out int c2)
	{
		st = y = x = ba = f0 = f1 = b0 = b5 = b6 = b7 = b8 = b9 = be = aa = c2 = 0;
		try
		{
			st = Ram(0x31);
			y = Ram(0x32);
			x = Ram(0x33);
			ba = Ram(0xBA);
			f0 = Ram(0xF0);
			f1 = Ram(0xF1);
			b0 = Ram(0xB0);
			b5 = Ram(0xB5);
			b6 = Ram(0xB6);
			b7 = Ram(0xB7);
			b8 = Ram(0xB8);
			b9 = Ram(0xB9);
			be = Ram(0xBE);
			aa = Ram(0xAA);
			c2 = Ram(0xC2);
			_ = zp;
		}
		catch { }
	}

	static void FineDump(string isol)
	{
		foreach (string path in Directory.GetFiles(isol, "sfxscan_F*_session.wav"))
		{
			byte[] wav = File.ReadAllBytes(path);
			if (wav.Length < 48) continue;
			int ch = BitConverter.ToInt16(wav, 22);
			int rate = BitConverter.ToInt32(wav, 24);
			if (ch < 1) ch = 1;
			int n = (wav.Length - 44) / (2 * ch);
			int win = Math.Max(1, rate / 20);
			Console.WriteLine("=== " + Path.GetFileName(path) + " ===");
			int a = (int)(8.40 * rate);
			int b = Math.Min(n, (int)(9.40 * rate));
			for (int i = a; i + win <= b; i += win)
			{
				int peak = 0, zc = 0;
				short prev = 0;
				for (int k = 0; k < win; k++)
				{
					short s = BitConverter.ToInt16(wav, 44 + (i + k) * 2 * ch);
					int v = s < 0 ? -s : s;
					if (v > peak) peak = v;
					if ((prev < 0 && s >= 0) || (prev >= 0 && s < 0)) zc++;
					prev = s;
				}
				Console.WriteLine(string.Format(System.Globalization.CultureInfo.InvariantCulture,
					"  {0:F2}s peak={1} zcr={2:F2}", i / (double)rate, peak, zc / (double)win));
			}
		}
	}

	static string WindowPeaks(string path, uint frames, int fired)
	{
		var sb = new System.Text.StringBuilder();
		byte[] wav = File.ReadAllBytes(path);
		if (wav.Length < 48) return "";
		int ch = BitConverter.ToInt16(wav, 22);
		int rate = BitConverter.ToInt32(wav, 24);
		if (ch < 1) ch = 1;
		int n = (wav.Length - 44) / (2 * ch);
		int win = Math.Max(1, rate / 4);
		sb.AppendLine("# wav_windows rate=" + rate + " frames=" + frames + " fired=" + fired);
		for (int i = 0, w = 0; i < n; i += win, w++)
		{
			int peak = 0, zc = 0;
			short prev = 0;
			int take = Math.Min(win, n - i);
			for (int k = 0; k < take; k++)
			{
				int v = Math.Abs(BitConverter.ToInt16(wav, 44 + (i + k) * 2 * ch));
				if (v > peak) peak = v;
				short s = BitConverter.ToInt16(wav, 44 + (i + k) * 2 * ch);
				if ((prev < 0 && s >= 0) || (prev >= 0 && s < 0)) zc++;
				prev = s;
			}
			double t = i / (double)rate;
			double zcr = zc / (double)Math.Max(1, take);
			int estFr = frames > 0 ? (int)(t * frames / (n / (double)rate)) : 0;
			string mark = (fired >= 0 && Math.Abs(estFr - fired) < 20) ? " NEAR_FIRE" : "";
			if (zcr > 0.18 && peak > 2500) mark += " NOISE";
			sb.AppendLine(string.Format(System.Globalization.CultureInfo.InvariantCulture,
				"# t={0:F2}s fr~{1} peak={2} zcr={3:F2}{4}", t, estFr, peak, zcr, mark));
		}
		return sb.ToString();
	}

	static int PeakOf(string path)
	{
		byte[] wav = File.ReadAllBytes(path);
		if (wav.Length < 48) return 0;
		int ch = BitConverter.ToInt16(wav, 22);
		if (ch < 1) ch = 1;
		int peak = 0;
		for (int i = 44; i + 1 < wav.Length; i += 2 * ch)
		{
			int v = Math.Abs(BitConverter.ToInt16(wav, i));
			if (v > peak) peak = v;
		}
		return peak;
	}

	[DllImport(Dll)] static extern void InitDll();
	[DllImport(Dll)] static extern uint GetMesenVersion();
	[DllImport(Dll)] static extern void SetEmulationFlag(uint flag, bool enabled);
	[DllImport(Dll)] static extern void Release();
	[DllImport(Dll)] static extern void InitializeEmu(
		[MarshalAs(UnmanagedType.LPUTF8Str)] string homeFolder,
		IntPtr windowHandle, IntPtr dxViewerHandle,
		[MarshalAs(UnmanagedType.I1)] bool useSoftwareRenderer,
		[MarshalAs(UnmanagedType.I1)] bool noAudio,
		[MarshalAs(UnmanagedType.I1)] bool noVideo,
		[MarshalAs(UnmanagedType.I1)] bool noInput);
	[DllImport(Dll)] [return: MarshalAs(UnmanagedType.I1)]
	static extern bool LoadRom(
		[MarshalAs(UnmanagedType.LPUTF8Str)] string filepath,
		[MarshalAs(UnmanagedType.LPUTF8Str)] string? patchFile);
	[DllImport(Dll)] static extern void Stop();
	[DllImport(Dll)] static extern void Pause();
	[DllImport(Dll)] static extern void Resume();
	[DllImport(Dll)] [return: MarshalAs(UnmanagedType.I1)] static extern bool IsPaused();
	[DllImport(Dll)] static extern void WaveRecord([MarshalAs(UnmanagedType.LPUTF8Str)] string filename);
	[DllImport(Dll)] static extern void WaveStop();
	[DllImport(Dll)] [return: MarshalAs(UnmanagedType.I1)] static extern bool WaveIsRecording();
	[DllImport(Dll)] static extern void SetKeyState(ushort scanCode, [MarshalAs(UnmanagedType.I1)] bool pressed);
	[DllImport(Dll)] static extern void SetMousePosition(double x, double y);
	[DllImport(Dll)] static extern TimingInfo GetTimingInfo(int cpuType);
	[DllImport(Dll, EntryPoint = "GetMemoryValues")]
	static extern void GetMemoryValues(int type, uint start, uint end, [In, Out] byte[] buffer);
	[DllImport(Dll)] static extern void SetAudioConfig(InteropAudioConfig config);
	[DllImport(Dll)] static extern void SetNesConfig(InteropNesConfig config);
	[DllImport(Dll)] static extern InteropNesConfig GetNesConfig();
	[DllImport(Dll)] static extern void SetDebuggerFlag(uint flag, [MarshalAs(UnmanagedType.I1)] bool enabled);
	[DllImport(Dll)] static extern void UpdateInputDevices();
	[DllImport(Dll)] static extern void InitializeDebugger();
	[DllImport(Dll)] static extern int GetMemorySize(int type);
	[DllImport(Dll)] static extern byte GetMemoryValue(int type, uint address);
	[DllImport(Dll)] static extern void SetMemoryValue(int type, uint address, byte value);
	[DllImport(Dll)] static extern int LoadScript(
		[MarshalAs(UnmanagedType.LPUTF8Str)] string name,
		[MarshalAs(UnmanagedType.LPUTF8Str)] string path,
		[MarshalAs(UnmanagedType.LPUTF8Str)] string content,
		int scriptId);
	[DllImport(Dll)] static extern void GetScriptLog(int scriptId, [Out] byte[] buffer, int maxLength);
	[DllImport(Dll)] [return: MarshalAs(UnmanagedType.I1)] static extern bool HasControlDevice(int type);
	[DllImport(Dll)] static extern ushort GetKeyCode([MarshalAs(UnmanagedType.LPUTF8Str)] string keyName);
	[DllImport(Dll)] static extern void GetKeyName(ushort key, [Out] byte[] buffer, int maxLength);

	[StructLayout(LayoutKind.Sequential)]
	struct TimingInfo
	{
		public double Fps;
		public ulong MasterClock;
		public uint MasterClockRate;
		public uint FrameCount;
		public uint ScanlineCount;
		public int FirstScanline;
		public uint CycleCount;
	}

	[StructLayout(LayoutKind.Sequential)]
	struct InteropAudioConfig
	{
		[MarshalAs(UnmanagedType.LPStr)] public string AudioDevice;
		[MarshalAs(UnmanagedType.I1)] public bool EnableAudio;
		[MarshalAs(UnmanagedType.I1)] public bool DisableDynamicSampleRate;
		public uint MasterVolume;
		public uint SampleRate;
		public uint AudioLatency;
		[MarshalAs(UnmanagedType.I1)] public bool MuteSoundInBackground;
		[MarshalAs(UnmanagedType.I1)] public bool ReduceSoundInBackground;
		[MarshalAs(UnmanagedType.I1)] public bool ReduceSoundInFastForward;
		public int VolumeReduction;
		[MarshalAs(UnmanagedType.I1)] public bool ReverbEnabled;
		public uint ReverbStrength;
		public uint ReverbDelay;
		[MarshalAs(UnmanagedType.I1)] public bool CrossFeedEnabled;
		public uint CrossFeedRatio;
		[MarshalAs(UnmanagedType.I1)] public bool EnableEqualizer;
		public double Band1Gain, Band2Gain, Band3Gain, Band4Gain, Band5Gain;
		public double Band6Gain, Band7Gain, Band8Gain, Band9Gain, Band10Gain;
		public double Band11Gain, Band12Gain, Band13Gain, Band14Gain, Band15Gain;
		public double Band16Gain, Band17Gain, Band18Gain, Band19Gain, Band20Gain;
		[MarshalAs(UnmanagedType.I1)] public bool AudioPlayerEnableTrackLength;
		public uint AudioPlayerTrackLength;
		[MarshalAs(UnmanagedType.I1)] public bool AudioPlayerAutoDetectSilence;
		public uint AudioPlayerSilenceDelay;
	}

	[StructLayout(LayoutKind.Sequential, Pack = 2)]
	struct InteropKeyMapping
	{
		public ushort A, B, X, Y, L, R, Up, Down, Left, Right, Start, Select, U, D;
		public ushort TurboA, TurboB, TurboX, TurboY, TurboL, TurboR, TurboSelect, TurboStart;
		public ushort GenericKey1;
		[MarshalAs(UnmanagedType.ByValArray, SizeConst = 100)]
		public ushort[] CustomKeys;
	}

	[StructLayout(LayoutKind.Sequential, Pack = 2)]
	struct InteropKeyMappingSet
	{
		public InteropKeyMapping Mapping1, Mapping2, Mapping3, Mapping4;
		public uint TurboSpeed;
	}

	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	struct InteropControllerConfig
	{
		public InteropKeyMappingSet Keys;
		public int Type;
	}

	[StructLayout(LayoutKind.Sequential)]
	struct InteropOverscan
	{
		public uint Left, Right, Top, Bottom;
	}

	[StructLayout(LayoutKind.Sequential)]
	struct InteropNesConfig
	{
		public InteropControllerConfig Port1, Port2, ExpPort;
		public InteropControllerConfig Port1A, Port1B, Port1C, Port1D;
		public InteropControllerConfig ExpPortA, ExpPortB, ExpPortC, ExpPortD;
		public InteropControllerConfig MapperInput;
		public uint LightDetectionRadius;
		[MarshalAs(UnmanagedType.I1)] public bool AutoConfigureInput;
		public int Region;
		[MarshalAs(UnmanagedType.I1)] public bool EnableHdPacks;
		[MarshalAs(UnmanagedType.I1)] public bool DisableGameDatabase;
		[MarshalAs(UnmanagedType.I1)] public bool FdsAutoLoadDisk;
		[MarshalAs(UnmanagedType.I1)] public bool FdsFastForwardOnLoad;
		[MarshalAs(UnmanagedType.I1)] public bool FdsAutoInsertDisk;
		public int VsDualVideoOutput, VsDualAudioOutput;
		[MarshalAs(UnmanagedType.I1)] public bool SpritesEnabled;
		[MarshalAs(UnmanagedType.I1)] public bool BackgroundEnabled;
		[MarshalAs(UnmanagedType.I1)] public bool ForceBackgroundFirstColumn;
		[MarshalAs(UnmanagedType.I1)] public bool ForceSpritesFirstColumn;
		[MarshalAs(UnmanagedType.I1)] public bool RemoveSpriteLimit;
		[MarshalAs(UnmanagedType.I1)] public bool AdaptiveSpriteLimit;
		[MarshalAs(UnmanagedType.I1)] public bool EnablePalBorders;
		[MarshalAs(UnmanagedType.I1)] public bool UseCustomVsPalette;
		public InteropOverscan NtscOverscan, PalOverscan;
		public int ConsoleType;
		[MarshalAs(UnmanagedType.I1)] public bool DisablePpuReset;
		[MarshalAs(UnmanagedType.I1)] public bool AllowInvalidInput;
		[MarshalAs(UnmanagedType.I1)] public bool DisableGameGenieBusConflicts;
		[MarshalAs(UnmanagedType.I1)] public bool DisableFlashSaves;
		[MarshalAs(UnmanagedType.I1)] public bool OverwriteOriginalRom;
		[MarshalAs(UnmanagedType.I1)] public bool EnableOamDecay;
		[MarshalAs(UnmanagedType.I1)] public bool EnablePpuOamRowCorruption;
		[MarshalAs(UnmanagedType.I1)] public bool EnablePpuSpriteEvalBug;
		[MarshalAs(UnmanagedType.I1)] public bool DisableOamAddrBug;
		[MarshalAs(UnmanagedType.I1)] public bool DisablePaletteRead;
		[MarshalAs(UnmanagedType.I1)] public bool DisablePpu2004Reads;
		[MarshalAs(UnmanagedType.I1)] public bool EnablePpu2000ScrollGlitch;
		[MarshalAs(UnmanagedType.I1)] public bool EnablePpu2006ScrollGlitch;
		[MarshalAs(UnmanagedType.I1)] public bool RestrictPpuAccessOnFirstFrame;
		[MarshalAs(UnmanagedType.I1)] public bool EnableDmcSampleDuplicationGlitch;
		[MarshalAs(UnmanagedType.I1)] public bool EnableCpuTestMode;
		[MarshalAs(UnmanagedType.I1)] public bool RandomizeMapperPowerOnState;
		[MarshalAs(UnmanagedType.I1)] public bool RandomizeCpuPpuAlignment;
		public int RamPowerOnState;
		public uint PpuExtraScanlinesBeforeNmi, PpuExtraScanlinesAfterNmi;
		[MarshalAs(UnmanagedType.I1)] public bool DisableNoiseModeFlag;
		[MarshalAs(UnmanagedType.I1)] public bool ReduceDmcPopping;
		[MarshalAs(UnmanagedType.I1)] public bool SilenceTriangleHighFreq;
		[MarshalAs(UnmanagedType.I1)] public bool SwapDutyCycles;
		[MarshalAs(UnmanagedType.I1)] public bool ReverseDpcmBitOrder;
		[MarshalAs(UnmanagedType.I1)] public bool BreakOnCrash;
		public int InputScanline;
		[MarshalAs(UnmanagedType.I1)] public bool IsFullColorPalette;
		[MarshalAs(UnmanagedType.ByValArray, SizeConst = 512)]
		public uint[] UserPalette;
		public uint Square1Volume, Square2Volume, TriangleVolume, NoiseVolume, DmcVolume;
		public uint FdsVolume, Mmc5Volume, Vrc6Volume, Vrc7Volume, Namco163Volume, Sunsoft5bVolume;
		public uint EpsmVolume;
		public int Square1Panning, Square2Panning, TrianglePanning, NoisePanning, DmcPanning;
		public int FdsPanning, Mmc5Panning, Vrc6Panning, Vrc7Panning, Namco163Panning, Sunsoft5bPanning;
		public int StereoFilter, StereoDelay, StereoPanningAngle, StereoCombFilterDelay, StereoCombFilterStrength;
	}
}
