# Build dependencies

The tool lock identifies the upstream downloads and their SHA-256 hashes. The tools are downloaded locally and are distributed by their respective projects.

Package notices and source locations for review:

- PowerShell 7.6.5, MIT and bundled third-party notices. https://github.com/PowerShell/PowerShell/tree/v7.6.5
- .NET SDK 7.0.203, Microsoft package terms and bundled third-party notices. https://github.com/dotnet/sdk/tree/v7.0.203
- Node.js 24.22.1, MIT and bundled third-party notices. https://github.com/nodejs/node/tree/v24.22.1
- CMake 4.4.4, BSD 3-Clause and bundled notices. https://github.com/Kitware/CMake/tree/v4.4.4
- SDK 0.24, MPL 2.0 core and separate component terms. GCC GPLv3, Ninja Apache-2.0, mkpsxiso GPLv2 or later. https://github.com/Lameguy64/PSn00bSDK/blob/v0.24/LICENSE.md
- Jellyfin FFmpeg 7.1.4-3, upstream Windows GPL build. https://github.com/jellyfin/jellyfin-ffmpeg/tree/v7.1.4-3
- Mesen 2.1.1, GPLv3. https://github.com/SourMesen/Mesen2/tree/137ae7ce3bf3f539d007e2c4ef3cb3b6c97672a1

This prototype references upstream downloads. Keep binaries out of the
source ZIP. Before mirroring or bundling binaries, verify notices and
corresponding source obligations for each exact binary and its components.
Windows runtime prerequisites still need validation on a fresh system.

