# File license schedule
GPL-WORKER and GPL-ORCHESTRATION use GPL-3.0-or-later. NONCOMMERCIAL designates only independently licensable project-original portions under LICENSE.md. MIXED preserves the OLC-3 palette and designates only separately authorized project-original portions. MIT, MIT-NOTICE and SEPARATE retain their separate terms.


SEPARATE  .gitignore
NONCOMMERCIAL  Bootstrap.ps1
NONCOMMERCIAL  BUILD_GAME.bat
SEPARATE  BUILD-DEPENDENCIES.md
NONCOMMERCIAL  Build-Game.ps1
NONCOMMERCIAL  guncal/guncal.c
NONCOMMERCIAL  guncal/guncal.h
NONCOMMERCIAL  huntps1/audio.c
NONCOMMERCIAL  huntps1/audio.h
NONCOMMERCIAL  huntps1/cal_save_codec.h
NONCOMMERCIAL  huntps1/cal_save.c
NONCOMMERCIAL  huntps1/cal_save.h
NONCOMMERCIAL  huntps1/clay_flight.c
NONCOMMERCIAL  huntps1/clay_flight.h
NONCOMMERCIAL  huntps1/clay_motion.c
NONCOMMERCIAL  huntps1/clay_motion.h
NONCOMMERCIAL  huntps1/clay_render.c
NONCOMMERCIAL  huntps1/clay_render.h
NONCOMMERCIAL  huntps1/clay_round.c
NONCOMMERCIAL  huntps1/clay_round.h
NONCOMMERCIAL  huntps1/deluxe_ducks.c
NONCOMMERCIAL  huntps1/deluxe_ducks.h
NONCOMMERCIAL  huntps1/game_b.c
NONCOMMERCIAL  huntps1/game_b.h
NONCOMMERCIAL  huntps1/gfx.c
NONCOMMERCIAL  huntps1/gfx.h
NONCOMMERCIAL  huntps1/logic.c
NONCOMMERCIAL  huntps1/logic.h
NONCOMMERCIAL  huntps1/main.c
NONCOMMERCIAL  huntps1/nes_fly_rom.h
NONCOMMERCIAL  huntps1/nes_fly.c
NONCOMMERCIAL  huntps1/nes_fly.h
NONCOMMERCIAL  huntps1/woods3d_render.c
NONCOMMERCIAL  huntps1/woods3d_render.h
NONCOMMERCIAL  huntps1/woods3d.c
SEPARATE  NOTICE.md
SEPARATE  output/README.txt
NONCOMMERCIAL  PortableTools.ps1
SEPARATE  README.md
SEPARATE  rom/README.txt
SEPARATE  source-files.txt
SEPARATE  THIRD_PARTY_NOTICES.txt
SEPARATE  tools.lock.json
GPL-ORCHESTRATION  tools/build_from_user_rom.ps1
NONCOMMERCIAL  tools/build_user_rom_candidate.ps1
NONCOMMERCIAL  tools/game_f/footsteps.cjs
NONCOMMERCIAL  tools/game_f/preview-meshes.cjs
SEPARATE  tools/game_f/README.md
NONCOMMERCIAL  tools/generate_user_rom_assets.ps1
NONCOMMERCIAL  tools/mesen_wave/BarkSpu.cs
NONCOMMERCIAL  tools/mesen_wave/BarkSpu.csproj
NONCOMMERCIAL  tools/mesen_wave/compose_rom_bark.ps1
NONCOMMERCIAL  tools/mesen_wave/decode_rom_dmc.ps1
NONCOMMERCIAL  tools/mesen_wave/encode_clay_audio.ps1
GPL-WORKER  tools/mesen_wave/MesenWave.csproj
NONCOMMERCIAL  tools/mesen_wave/prepare_rom_bark_ps1.ps1
GPL-WORKER  tools/mesen_wave/Program.cs
MIT-NOTICE  tools/nes_v15/SPU-ENCODER-LICENSE.txt
MIT  tools/nes_v15/SpuAdpcm.cs
NONCOMMERCIAL  tools/rom_audio/Apu.cs
NONCOMMERCIAL  tools/rom_audio/Encoder.cs
NONCOMMERCIAL  tools/rom_audio/Program.cs
NONCOMMERCIAL  tools/rom_audio/RomAudio.csproj
NONCOMMERCIAL  tools/rom_release/CMakeLists.txt
NONCOMMERCIAL  tools/romconv/ComparePpu.cs
MIXED  tools/romconv/NesPpu.cs
NONCOMMERCIAL  tools/romconv/Program.cs
SEPARATE  tools/romconv/README.md
NONCOMMERCIAL  tools/romconv/Reconstruct.cs
NONCOMMERCIAL-WITH-EXCLUSIONS  tools/romconv/Reconstruct.Ps1.cs
NONCOMMERCIAL-WITH-EXCLUSIONS  tools/romconv/Reconstruct.Release.cs
NONCOMMERCIAL  tools/romconv/RomConv.csproj

SEPARATE  GPL-3.0.txt
SEPARATE  LICENSE.md
SEPARATE  LICENSE-MESEN-WORKER.md
SEPARATE  MESEN-NOTICE.md
SEPARATE  FILE-LICENSE-SCHEDULE.md

## Reconstruction-file exceptions

NONCOMMERCIAL and NONCOMMERCIAL-WITH-EXCLUSIONS cover only independently licensable project-original contributions whose rights holders authorize LICENSE.md. For Reconstruct.Ps1.cs and Reconstruct.Release.cs, the grant excludes any Nintendo-owned expression reflected in the fixed HUD, panel, layout, and reference-color recipes identified in this schedule. ROM contents, generated assets, and all other third-party material remain outside the grant.

- tools/romconv/Reconstruct.Ps1.cs, StampCalibrationRow and ShiftScorePanel (lines 256 and 281).
- tools/romconv/Reconstruct.Release.cs, ROUND and GAME OVER layouts, clay reference-color mapping, and clay HUD recipes (lines 124, 185, and 305–322).
