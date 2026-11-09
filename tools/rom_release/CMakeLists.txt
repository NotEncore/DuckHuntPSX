cmake_minimum_required(VERSION 3.21)
project(DuckHuntLocalRom LANGUAGES C)

foreach(asset IN ITEMS nes_fly_rom_data.h game_b_flight.h rom_hit_score_tables.h hunt_ps1_assets.c)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/huntps1/generated/${asset}")
    message(FATAL_ERROR "Missing generated ${asset}. Run tools/build_from_user_rom.ps1 with a supported user-supplied Duck Hunt ROM.")
  endif()
endforeach()

# Configured in a private staging directory by build_user_rom_candidate.ps1.
psn00bsdk_add_executable(duckhunt GPREL
  huntps1/main.c huntps1/cal_save.c huntps1/gfx.c huntps1/logic.c
  huntps1/nes_fly.c huntps1/audio.c guncal/guncal.c
  huntps1/game_b.c huntps1/clay_motion.c huntps1/clay_flight.c
  huntps1/clay_round.c huntps1/clay_render.c huntps1/deluxe_ducks.c
  huntps1/woods3d.c huntps1/woods3d_render.c
  huntps1/generated/hunt_ps1_assets.c
  huntps1/generated/hunt_sfx_m2_title.c
  huntps1/generated/hunt_sfx_m3_intro.c
  huntps1/generated/rom_bark_spu.c
)
target_include_directories(duckhunt PRIVATE huntps1 huntps1/generated)
target_compile_definitions(duckhunt PRIVATE
  HUNTPS1_ROM_BARK HUNTPS1_GAME_A_AUDIO HUNTPS1_GAME_B HUNTPS1_GAME_C
  HUNTPS1_EXPERIMENTAL_OPTIONS HUNTPS1_EXPERIMENTAL_ZAPPER_FLASH
  HUNTPS1_DELUXE_MENU
  HUNTPS1_WOODS_INTEGRATED HUNTPS1_WOODS_SCENE_ONE HUNTPS1_WOODS_CINEMATIC
  HUNTPS1_WOODS_ART HUNTPS1_WOODS_LEFT_FLUSH HUNTPS1_WOODS_DETAIL
  HUNTPS1_WOODS_MEADOW HUNTPS1_WOODS_LAKE HUNTPS1_WOODS_SHOWCASE
  HUNTPS1_WOODS_MARSH HUNTPS1_WOODS_APPROVED_ASSETS HUNTPS1_WOODS_TREE_STUDY
  HUNTPS1_WOODS_LEAN_GROVE HUNTPS1_WOODS_SOUND HUNTPS1_WOODS_WALK_CACHE
  HUNTPS1_WOODS_PAN_CACHE HUNTPS1_WOODS_FAST_PAN HUNTPS1_WOODS_FAST_SHOOT
  HUNTPS1_WOODS_PACKED_WATER HUNTPS1_WOODS_WIDE_WATER HUNTPS1_WOODS_RENDER_O3
  HUNTPS1_WOODS_SPLIT_PAN HUNTPS1_WOODS_SPLIT_FALL HUNTPS1_WOODS_EARLY_DOG_BACKGROUND
  HUNTPS1_WOODS_LOW_OVERDRAW HUNTPS1_WOODS_DOG_BATCH HUNTPS1_WOODS_SWIM_FRAMING
)

