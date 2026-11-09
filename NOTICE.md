# Distribution notes

Duck Hunt is a Nintendo game. Nintendo and other respective rights holders retain their rights in the original game, including its artwork and audio. This unofficial project is independent of Nintendo and Sony.

The build requires a separately supplied supported Duck Hunt cartridge dump. Keep the dump, generated assets, executable, BIOS, and captured gameplay private. The SDK, emulator core, and media tools are separate dependencies obtained from their respective projects.

Project licensing is defined in LICENSE.md, LICENSE-MESEN-WORKER.md, and FILE-LICENSE-SCHEDULE.md. The custom noncommercial license applies only to designated project-original contributions. The three designated Mesen worker and orchestration files carry GPL-3.0-or-later for authorized project contributions. The project grant for tools/romconv/Reconstruct.Ps1.cs and tools/romconv/Reconstruct.Release.cs is subject to the reconstruction-file exceptions in FILE-LICENSE-SCHEDULE.md. Nintendo-owned expression, ROM contents, generated Nintendo assets, and other third-party material remain outside the project grant. Third-party terms and credits are documented in THIRD_PARTY_NOTICES.txt, MESEN-NOTICE.md, and the README.

## Game F and third-party material

The build generates Game F's geometry, textures, and footsteps locally. The geometry recipe reproduces the existing packed header. The footstep recipe synthesizes and encodes two new cues from fixed seeds. See tools/game_f/README.md for provenance, commands, and the limits of the review.

The NES RGB approximation is attributed in THIRD_PARTY_NOTICES.txt and in its source header.
