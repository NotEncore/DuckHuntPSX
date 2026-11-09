# Duck Hunt PSX

**A PlayStation homebrew project for my son, Oliver. Built with AI, tested on real hardware.**

*Created and directed by Encore (NotEncore)*

## Why I Started This Project

I started this project for my son, Oliver.

I've always had a fascination with older video game consoles, especially the original PlayStation. There's something special about the hardware, the games, and the experience of playing them on an old CRT television.

Duck Hunt is one of those classic games that I think deserves to be experienced the way it was originally intended: with a real lightgun, pointing at a real CRT.

I wanted to bring that experience to the original PlayStation.

But more than anything, I wanted to make something for my son.

I hope he enjoys it.


---

## I Didn't Write a Single Line of Code

I want to be completely transparent about how this project was made.

**I have not personally written a single line of code for Duck Hunt PS1.**

I'm not a programmer, and I'm perfectly okay with that.

This project has been developed using AI coding assistants under my direction.

I came up with the idea, established the goals, decided how the game should look and behave, assembled the development hardware, tested the builds, identified problems, and guided the development process.

AI tools handled the actual programming.

That doesn't mean the AI magically created a finished game with one prompt.

Development has involved countless iterations, failed experiments, debugging sessions, hardware tests, and corrections.

I've spent a great deal of time trying to understand what the game is doing, why things aren't working, and how to communicate those problems so they can be fixed.

I don't want credit for writing code I didn't write.

I do want to share what has been possible through this unusual approach to development.

---

## My Development Environment

One of the most important decisions I made was to develop and test the game on an actual PlayStation.

Emulators are incredibly useful, but I wanted to know whether the game really worked on the hardware it was being designed for.

### Hardware

| Hardware | Role |
|---|---|
| Original Sony PlayStation | Runs and validates the homebrew builds |
| OrionSoft PS1USB Dev Cart | USB communication and development interface |
| Logi's PS1 Dev Cart firmware 1.1 | Development cartridge firmware enabling advanced testing and control |
| XStation | Optical drive emulator for loading games and homebrew |
| Namco GunCon 1 | Original PlayStation lightgun |
| Sony PVM-20L5 | Professional CRT monitor for testing |
| Composite CRT television | Additional real-world video and overscan testing |
| MemCard PRO | Memory-card functionality and calibration persistence |
| USB video capture adapter | Captures real PlayStation video and audio |
| Windows development PC | Builds, tooling, automation, and analysis |

### Software

| Software | Purpose |
|---|---|
| PSn00bSDK | PlayStation homebrew development SDK |
| CMake | Build configuration and compilation |
| mkpsxiso | PlayStation disc-image generation |
| DuckStation | PS1 emulation and debugging |
| Mesen | NES emulation and reference analysis |
| OBS Studio | Recording real PlayStation footage and audio |
| Git and GitHub | Source control and project history |
| AI coding assistants, including Codex | Code generation, debugging, analysis, and development |

The development PC runs Windows, with the project built locally and tested through both emulation and original hardware.

---

## Orion's Dev Cart and Logi's Firmware 1.1

One of the most useful pieces of hardware in this project has been the **OrionSoft PS1USB development cartridge**, running **Logi's PS1 Dev Cart firmware 1.1**.

I want to specifically recognize both Orion and Logi because their work has played an important role in making this development process possible.

The cartridge provides a USB communication path between my Windows PC and the original PlayStation.

This allows us to do more than simply burn a disc image, insert it into the console, and see whether it works.

With the development cartridge and firmware, we've been able to establish a more automated hardware testing workflow.

### What we've used it for

- Transferring development executables to the PlayStation.
- Starting and controlling test sessions.
- Sending virtual controller inputs.
- Simulating GunCon trigger events for repeatable tests.
- Reading diagnostic information from console memory.
- Verifying internal game states and audio events.
- Running repeatable tests on actual PS1 hardware.
- Coordinating hardware tests with OBS recordings.

Firmware 1.1 has been especially valuable because of its support for remote input and live memory inspection.

For example, we can start a build, send a series of controller inputs, trigger a particular gameplay event, and inspect the resulting state.

This has helped us investigate issues that would otherwise require manually repeating the same steps over and over.

**This is real PlayStation hardware executing the game.** The virtual inputs make tests repeatable, but they do not replace physical GunCon accuracy testing.

---

## How We Let AI See a Real PlayStation

One of the biggest challenges of AI-assisted retro development is that the AI doesn't automatically know what is happening on the television.

The code might compile successfully.

An emulator might run it.

But the real PlayStation could still display something incorrectly, play the wrong sound, miss a lightgun shot, or behave differently from what we expected.

I needed a way to get useful information from the actual console back into the development process.

### Capturing the PlayStation

I connected the PlayStation's analog video and audio output to a USB capture adapter connected to my Windows PC.

I use OBS Studio to record the console's output.

That gives us actual footage and sound from the hardware, rather than relying entirely on screenshots from an emulator.

### The testing workflow

1. An AI coding assistant generates or updates a test build.
2. The executable is compiled on the Windows PC.
3. The build is loaded onto the original PlayStation.
4. Logi's firmware 1.1 and the development cartridge can send repeatable controller inputs and inspect console state.
5. OBS records the PlayStation's actual video and audio output.
6. Captured frames, audio measurements, and hardware diagnostics are analyzed using local tools and AI-assisted workflows.
7. Problems are identified and translated into code changes.
8. The process repeats until the behavior is acceptable.

This creates a feedback loop between AI-generated software and physical hardware.

### Why the footage matters

A development log might say that an animation completed successfully.

But a recording can reveal that the animation looks wrong.

A sound effect might be playing at the correct time but be much louder than the original NES version.

A duck might be correctly positioned internally but appear several pixels away from where the GunCon hit detection expects it.

Real hardware recordings help us identify these differences.

We have used recordings and frame comparisons to investigate dog animations, opening-sequence timing, audio levels, title-screen graphics, and gameplay behavior.

The AI doesn't directly watch the CRT through a camera or automatically see everything on my computer. Instead, the workflow provides captured media, extracted measurements, and diagnostic reports to the AI tools when those materials are available.

That distinction is important.

### Hardware testing is not the same as optical testing

The development cartridge can simulate controller and GunCon inputs, which is incredibly useful for repeatable testing.

But that doesn't prove a physical GunCon will hit a target correctly when aimed at a CRT.

Physical lightgun testing remains a separate requirement.

Both kinds of testing are important, and I want to be transparent about which tests have actually been completed.

## Six game modes

Duck Hunt PS1 is an unofficial PlayStation homebrew recreation and expansion of the NES game. It builds its game data locally from the supported ROM you provide.

- **Game A — One Duck.** Hunt one duck at a time.
- **Game B — Two Ducks.** Track two ducks in the same encounter.
- **Game C — Clay Shooting.** Aim at launched clay targets.
- **Game D — Duck Duel.** Race to ten hits in the two-player duel, or play solo.
- **Game E — Shootout.** Play a solo shooting challenge or compete with player-specific targets in two-player mode.
- **Game F — Into the Woods.** Follow the dog through a 3D woodland encounter, solo or cooperatively.

## Build from your ROM

You need a 64-bit Windows PC with Windows PowerShell and the built-in Windows curl.exe, a writable folder, and your own supported NES Duck Hunt cartridge dump. The launcher downloads the development tools automatically. You do not need to install them yourself or run as administrator. DuckStation and its BIOS are separate if you want to play in the emulator.

1. **Extract the source ZIP.** In File Explorer, right-click the ZIP and choose **Extract All**. Open the extracted folder that contains `BUILD_GAME.bat`. Run the launcher from that folder.
2. **Put your ROM in `rom`.** This folder is beside `BUILD_GAME.bat` and already contains a small instruction file. Copy exactly one `.nes` file into it. Any filename works, including names with spaces. Use a dump from your own lawfully obtained NES Duck Hunt cartridge. The supported dump uses mapper 0 with 16 KiB PRG and 8 KiB CHR. The builder checks the exact ROM revision. Other versions with the same size can still be rejected. Keep the original extension and leave the ROM in the folder.
3. **Double-click `BUILD_GAME.bat`.** A console opens and checks the ROM. Look for **ROM verified.** If Windows hides extensions, the launcher may appear as **BUILD_GAME** with type **Windows Batch File**.
4. **Choose an output.** At the prompt, press **Enter** for BIN/CUE, the recommended option for DuckStation. Enter **2** and press **Enter** for a PS-X EXE, the option for a compatible homebrew loader.
5. **Wait for setup and compilation.** The first run needs internet access to download about **604 MiB** of verified tools. Keep about **1.53 GiB** free for the installed tool cache, plus space for temporary downloads, build files and game output. The cache lives under `%LOCALAPPDATA%\DuckHuntPS1\tools`. Later builds reuse it while the verified packages remain available. Keep the console open until the build finishes.
6. **Find your game.** Success is marked by **Finished. Your local game is in …** followed by **Press any key to continue**. Open the folder shown in the message, under `output`. Every successful run creates a new dated subfolder. BIN/CUE creates `DuckHuntPS1.bin` and `DuckHuntPS1.cue`. EXE creates `DuckHuntPS1.exe`.
7. **Play.** In DuckStation, open `DuckHuntPS1.cue` as a disc image and keep its matching BIN beside it. Configure DuckStation with a BIOS you supply. For real hardware, send `DuckHuntPS1.exe` through a compatible PS1 homebrew loader such as the PS1 Dev Cart. BIN/CUE on real hardware requires a compatible disc-loading setup. A standard retail console may need a separate homebrew boot method. Physical GunCon 1 aiming requires a compatible CRT. Ordinary modern flat-panel displays do not support original GunCon 1 optical aiming.

### If the build stops

- **Missing ROM or more than one ROM:** Keep exactly one `.nes` file directly inside `rom`, beside its instruction file. Extract the ROM from any archive before copying it. Check the full extension if Explorer hides extensions.
- **Unsupported header or revision:** Check that this is a supported NES Duck Hunt cartridge dump. Renaming a different ROM or editing its header will not make its contents match.
- **Download failed or timed out:** Check internet access, free disk space and any network restrictions, then run the launcher again. Completed verified packages are reused. A failed integrity check means the download was rejected. If the error identifies a bad or incomplete cache entry, move only that identified entry aside before retrying.
- **Windows or antivirus blocks a script or tool:** Read the exact warning and confirm the ZIP came from the intended release and matches its published hash. Keep protection enabled. If a managed PC blocks the launcher or a tool, contact its administrator or use a PC that permits the build.
- **No new files in `output`:** Look for **Build stopped** or another error before the pause. Check the newest `builder-*.log` beside the launcher. If asking for help, share the error text with personal paths removed. Keep the ROM, BIOS, generated assets and game files local.

## Credits

- **OrionSoft** for the PS1 USB development cartridge hardware used to run builds on the console. [OrionSoft’s cartridge page](https://orionsoft.itch.io/ps1-usb)
- **logi-26** for PS1 Dev Cart firmware 1.1 and the matching transfer tools used for real-console testing. [PS1 Dev Cart firmware](https://github.com/logi-26/PS1-DEV-CART-Firmware)
- **Everyone involved in making PSn00bSDK** for the PlayStation homebrew SDK and toolchain. [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK)
- **Nicholas Noble** for maintaining the PSXDEV Discord server.
- **Connor McLaughlin and DuckStation contributors** for the PS1 emulator used for development checks. [DuckStation](https://github.com/stenzek/duckstation)
- **Sour and the Mesen contributors** for the NES reference emulator and core used for tracing and local audio generation. [Mesen 2](https://github.com/SourMesen/Mesen2)
- **FFmpeg and OBS Studio contributors** for audio processing and hardware recording tools. [FFmpeg](https://ffmpeg.org/) · [OBS Studio](https://obsproject.com/)

Duck Hunt and its original game artwork and audio belong to their respective rights holders. This project is independent of Nintendo and Sony.

