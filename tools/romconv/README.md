# tools/romconv

Duck Hunt PS1 is an unofficial, noncommercial fan project. The source package requires a supported ROM dump supplied by the user.

This project is a host (.NET 7) tool. Do not add it to the PSn00bSDK
CMake tree; the MIPS toolchain must not compile it.
# Local PS1 asset generation

Supply your own verified Duck Hunt ROM dump and choose a new output directory.

```powershell
dotnet run --project tools/romconv -- --rom 'C:/path/to/DuckHunt.nes' --ps1pack-only --out build/user-rom-assets
```

This produces the main graphics pack, score sprites, Game A banners, all clay graphics and HUD elements, duck flight tables, and clay movement tables. It leaves the ROM and existing generated files untouched and skips raw PRG/CHR dumps and image previews.

To also generate the approved bark in the same pass, use the wrapper with a new output directory.

```powershell
pwsh -File tools/generate_user_rom_assets.ps1 -Rom 'C:/path/to/DuckHunt.nes' -OutputDirectory build/user-rom-assets
```

The wrapper verifies the bark against the approved payload hash. Keep all generated files, including audio intermediates, and any linked PS-X executable local.

## Build the complete local A–F candidate

Use PowerShell 7 with the .NET 7 SDK, the Windows MesenCore 2.1.1 DLL,
FFmpeg, PSn00bSDK with its MIPS toolchain, CMake, and Ninja.

```powershell
pwsh -File tools/build_from_user_rom.ps1 `
  -Rom 'C:/path/to/DuckHunt.nes' `
  -MesenCore 'C:/path/to/MesenCore.dll' `
  -SdkDirectory 'C:/path/to/PSn00bSDK' `
  -Ffmpeg 'C:/path/to/ffmpeg.exe' `
  -Cmake 'C:/path/to/cmake.exe' `
  -OutputDirectory build/user-rom-candidate
```

Choose a new output directory. The default Ninja location is `bin/ninja.exe` under the SDK. Pass `-Ninja` with an explicit path if needed.
The supported revision is mapper 0 with 16 KiB PRG and 8 KiB CHR. The tool verifies the PRG and CHR payload against its revision allowlist. A file with the same size but a different payload is rejected before output creation. The script checks the ROM revision first, then extracts graphics and movement
data, captures and encodes the NES audio, and builds and validates
`candidate/build/duckhunt.exe` inside the output directory. It prints the
executable's SHA-256 on success. The build uses freshly generated NES assets.

If a capture fails, inspect the corresponding log in `audio-work`. A trace mismatch stops the build before audio encoding. Use a fresh output directory when retrying and keep the failed log for diagnosis. This candidate still needs audio and gameplay verification. The output
directory, including the executable, contains ROM-derived data and must
stay local. A source release needs a separate, audited file allowlist.

## Run the local build

Open the generated PS-X executable in DuckStation using its file-open command.
Use a BIOS you dumped from your own console and configure the emulated
controller or GunCon before starting a game. Use an isolated memory-card image
if testing calibration.

For original hardware, verify the executable hash before using PS1 Dev Cart
firmware 1.1 and its matching Windows transfer tool. Use a disposable memory
card for persistence tests and keep your personal saves disconnected. Virtual
GunCon input can check gameplay logic. Physical aiming requires a GunCon and
a compatible CRT.

## Clay audio capture experiment

The Mesen capture tool accepts explicit ROM, MesenCore 2.1.1 DLL, and output paths for isolated sound requests. Use a private scratch directory and run the three requests `F1 1`, `F0 32`, and `F0 4`.

```powershell
dotnet run --project tools/mesen_wave/MesenWave.csproj -- sfxscan F1 1 '--rom=C:/path/to/DuckHunt.nes' '--core=C:/path/to/MesenCore.dll' '--capture-dir=build/clay-audio'
pwsh -File tools/mesen_wave/encode_clay_audio.ps1 -CaptureDirectory build/clay-audio -Output build/clay-audio/candidate-clay-sfx.h
```

The encoder requires FFmpeg on PATH or an explicit `-Ffmpeg` path. The capture invokes a sound request in the local emulator's RAM. The encoder reproduces the approved bank from its original recordings, but fresh captures have not matched that bank. Treat fresh output as experimental and keep the approved game audio in place.
