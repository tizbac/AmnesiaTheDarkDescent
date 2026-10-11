# Amnesia: The Dark Descent - Agent Instructions

## Project Overview
This is the open-source release of **Amnesia: The Dark Descent** (2010) and its **HPL2 engine** by Frictional Games. The engine is C++ with CMake build system.

✅ **FBX SDK 2012 replaced with Assimp** — The proprietary FBX SDK dependency has been replaced with **Assimp (Open Asset Import Library)**, enabling builds on modern Linux without unavailable proprietary SDKs.

## Repository Structure
```
amnesia/src/           # Amnesia game + launcher
  game/                # Main game executable (Amnesia)
  launcher/            # FLTK-based launcher (Linux/Windows)
  launcher-macosx/     # Cocoa-based launcher (macOS)
  lang_conv/           # Language conversion utility
  CacheFileDateFix/    # Cache utility
  BuildScripts/        # Linux mock-based build scripts (legacy)
HPL2/
  core/                # HPL2 engine (static library)
  tools/               # Editor tools (LevelEditor, ModelEditor, etc.)
  tests/               # Test applications
  dependencies/        # Prebuilt third-party libs (gitignored)
    lib/linux/lib64/   # 64-bit prebuilt libraries
```

## Build System
- **CMake ≥ 2.8.11** (legacy, uses `GLOB` for sources)
- Main entry: `amnesia/src/CMakeLists.txt`
- Builds: `HPL2` (static lib) → `Amnesia`, `Launcher`, editor tools
- **C++11** required (set in HPL2/core/CMakeLists.txt)

### Key CMake Options
| Option | Default | Description |
|--------|---------|-------------|
| `USE_SDL2` | ON | Use SDL2 vs SDL1.2 |
| `USERDIR_RESOURCES` | ON | Allow local user directory resources |
| `IS_APPLE_64` | OFF | 64-bit macOS build |
| `AMNESIA_WITH_SERIAL` | OFF | Enable serial key checking (needs extra source) |
| `USE_GAMEPAD` | ON | Enable gamepad support |
| `USE_OPENXR` | ON | Enable OpenXR VR support (requires libopenxr-dev) |

### Targets
- `Amnesia` - Main game executable
- `AmnesiaDemo` - Demo build (excluded from all)
- `Launcher` - Game launcher
- `LevelEditor`, `ModelEditor`, `ParticleEditor`, `MaterialEditor` - Content tools
- `MapViewer`, `ModelViewer` - Asset viewers
- `MshConverter` - Mesh converter
- `GameRelease` - Meta target: `Amnesia` + `Launcher`

## Modern Linux Build (Recommended)
The engine now builds on modern Linux using **system libraries** and **Assimp** instead of the FBX SDK:

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt install cmake g++ libsdl2-dev libopenal-dev libvorbis-dev libogg-dev \
  libtheora-dev libdevil-dev libglew-dev libfltk-dev libfltk-images-dev \
  libnewton-dev libangelscript-dev libassimp-dev libpugixml-dev zlib1g-dev

# Configure and build
mkdir build && cd build
cmake ../amnesia/src -DCMAKE_BUILD_TYPE=Release -DUSE_SDL2=ON
make -j4 Amnesia Launcher
```

The build uses system libraries for: SDL2, OpenAL, Vorbis, Ogg, Theora, DevIL, GLEW, FLTK, Newton, AngelScript, Assimp, pugixml, zlib.

### Legacy Mock/CentOS Build (Original)
The official Linux build used **mock** (CentOS chroot) for reproducible binaries:

```bash
cd amnesia/src/BuildScripts
./mock-prep.sh 32 init    # One-time setup (32 or 64)
./mock-sync.sh 32         # Sync code into chroot
./build-linux.sh 32       # Full build + extract binaries to Release/
```

## Dependencies
### System Libraries (Modern Build)
- SDL2, OpenAL, Vorbis, Ogg, Theora, DevIL, GLEW, FLTK/FLTK-images
- Newton Physics, AngelScript
- **Assimp** (replaces FBX SDK 2012) + pugixml
- zlib, libjpeg, binreloc

### Prebuilt Libraries (in `HPL2/dependencies/lib/linux/lib64/`)
Used as fallback when system libraries not available:
- libNewton.a, libangelscript.a, libGLEW.a, libfltk.a, libfltk_images.a
- libvorbisfile.so, libvorbis.so, libogg.so, libopenal.so, libSDL2.so
- libtheora.so, libIL.so, libjpeg.a

## Platform Notes
- **Linux**: Uses SDL2, X11, FLTK for launcher — **fully working on modern distros**
- **macOS**: Uses Cocoa launcher (`launcher-macosx/`), bundles frameworks via `PostBuildMacBundle()`
- **Windows**: Visual Studio 2010 project files (`.vcproj`, `.sln`) — outdated

## VR Support (OpenXR)
Experimental OpenXR VR support is available as a compile-time option:

```bash
# Install OpenXR development headers
sudo apt install libopenxr-dev

# Build with VR support
cmake ../amnesia/src -DCMAKE_BUILD_TYPE=Release -DUSE_SDL2=ON -DUSE_OPENXR=ON
```

**Current status**: Framework is implemented (`HPL2/core/include/graphics/OpenXR.h`, `HPL2/core/sources/graphics/OpenXR.cpp`) with OpenXR 1.0 loader integration. The implementation provides:
- OpenXR instance/session management
- View matrix retrieval for stereo rendering
- Controller input abstraction
- Config file integration (`VR.Enabled`, `VR.IPD`, `VR.NearZ`, `VR.FarZ`)

**Limitations**: Full stereo rendering pipeline and compositor integration not yet implemented. The current build links against `libopenxr_loader.so` and initializes the OpenXR runtime, but stereo rendering to the HMD is a work in progress.

**Dependencies**: `libopenxr-dev` (OpenXR 1.0 loader + headers)

## Key Changes (Modernization)
1. **FBX SDK → Assimp**: Created `MeshLoaderAssimp.h/cpp` using Assimp 5.x API to load FBX, Collada, OBJ, glTF, etc.
2. **C++11 support**: Added `CMAKE_CXX_STANDARD 11` to HPL2/core/CMakeLists.txt
3. **64-bit library paths**: Fixed CMake to use `lib64` directory for 64-bit builds
4. **PIE compatibility**: Disabled PIE (`-no-pie`) for compatibility with static prebuilt libraries
5. **Perl script permissions**: `buildcounter.pl` needs execute permission

## Known Issues
1. **CMake 2.8.11** — Very old; modern CMake may warn (deprecation warnings only)
2. **GLOB in CMake** — Source lists not auto-updated on file add/remove
3. **No test suite** — `HPL2/tests/` has test apps but no test runner
4. **Mock scripts** — CentOS 6 based; may not work on modern distros
5. **Deprecated C++** — Uses `std::binary_function` (deprecated in C++11, removed in C++17) — compiles with `-fpermissive`
6. **Prebuilt libs** — Some prebuilt static libs (GLEW, Newton, AngelScript) not compiled with `-fPIE`; requires `-no-pie` linker flag

## Useful Commands
```bash
# Configure (Linux, Release) - modern build with system libraries
cmake -B build -S amnesia/src -DCMAKE_BUILD_TYPE=Release -DUSE_SDL2=ON

# Build game + launcher
cmake --build build --target Amnesia Launcher

# Build all tools
cmake --build build --target LevelEditor ModelEditor ParticleEditor MaterialEditor MapViewer ModelViewer MshConverter

# Clean
rm -rf build
```

## License
GPL v3 — see `LICENSE`.