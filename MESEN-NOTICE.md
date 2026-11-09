# Third-party distribution notices

Retain the existing THIRD_PARTY_NOTICES.txt, BUILD-DEPENDENCIES.md, and tools/nes_v15/SPU-ENCODER-LICENSE.txt unchanged. This supplement distinguishes the licenses in the file schedule. The olcNES palette remains OLC-3 and SpuAdpcm.cs remains separately MIT-noticed. Upstream tools remain under their own notices. The custom license covers only expressly designated project-original material. No Nintendo or Sony rights are granted.

## Mesen 2.1.1

Copyright (C) 2014-2025 Sour. Mesen is offered under GNU GPL version 3 or later. Retain the full GPL text and applicable upstream component notices.

- Exact source revision: 137ae7ce3bf3f539d007e2c4ef3cb3b6c97672a1.
- [Upstream version and downloads](https://github.com/SourMesen/Mesen2/releases/tag/2.1.1).
- [Exact source tree and notices](https://github.com/SourMesen/Mesen2/tree/137ae7ce3bf3f539d007e2c4ef3cb3b6c97672a1).
- [Exact source ZIP](https://codeload.github.com/SourMesen/Mesen2/zip/137ae7ce3bf3f539d007e2c4ef3cb3b6c97672a1), verified SHA-256 8AA5B9432618E7B0D8DBA1979293412073C0F36A7748B0219C8186A3BBE4D28C.
- [Upstream Windows build instructions](https://github.com/SourMesen/Mesen2/blob/137ae7ce3bf3f539d007e2c4ef3cb3b6c97672a1/COMPILING.md) and [build workflow](https://github.com/SourMesen/Mesen2/blob/137ae7ce3bf3f539d007e2c4ef3cb3b6c97672a1/.github/workflows/build.yml).
- Mesen_2.1.1_Windows.zip SHA-256 23CCC2BC060B663C68DAD3A8C5D6DA7D23A50F872D04F135BAFA2B04FF7D5CBE. Extracted MesenCore.dll SHA-256 65F69A7BDB25C35FDCA77C9C73E71EF3E8EC233380204D94CFC0179933048C74.

The release supplies capture-worker source, its project, and orchestration source. The Windows launcher downloads the upstream archive to the user's cache, verifies it, and extracts the core without patching it. The worker loads that core in-process. No Mesen binary is bundled in the source-only distribution. The worker and core do not run on the PlayStation.

To build the worker, use the pinned .NET SDK and run dotnet build tools/mesen_wave/MesenWave.csproj. The invocation and nine capture profiles are documented by tools/build_from_user_rom.ps1. See tools/romconv/README.md for a standalone capture example. Use your own verified ROM and a private output directory.

This distribution plan keeps binaries upstream. If the project later conveys the core, the combined worker binary, or a mirror, verify and meet GPL object-code corresponding-source requirements for that exact distribution method, including complete required sources and scripts and sustained source availability where applicable. Do not assume that a link to the upstream repository alone satisfies those requirements.
