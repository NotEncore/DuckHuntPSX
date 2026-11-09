# Game F build inputs

Run these commands from the source root with Node.js on PATH. The normal ROM build invokes them automatically in its private build directory.

```powershell
node tools/game_f/preview-meshes.cjs --export-ps1 woods3d_approved_assets.h
node tools/game_f/footsteps.cjs woods_footsteps.h
```

Keep both output headers local. The preview script was recovered from the project's local Game F mesh review. Its dog poses, trees, foliage, and terrain are built from code-defined shapes, color values, and deterministic texture algorithms. The export reads no ROM, external meshes, texture files, or third-party packages. The only imports are Node's filesystem, path, and compression modules. The exported header must match SHA-256 6949DAA6F9A6B1A10537468A15EC90F58A934A434A144C12A4307C4444006AB7. The source review found no identifiable copied third-party material. The original authoring history and rights were not independently established.

The replacement footsteps were written for this release using only fixed-seed noise, filtering, and decaying crackles. Neither old cue, downloaded audio, nor Nintendo audio is an input. The script encodes 28 samples per PS1 ADPCM block with predictor zero at a 22,050 Hz playback rate. The two decoded cues are 96.5 and 106.7 ms long, with RMS levels of approximately -34 dBFS. Existing playback code and trigger timing are retained.
