rlvm: A RealLive interpreter.
===============================================

## INTRODUCTION

rlvm is a Free Software reimplementation of the VisualArt's KK's RealLive
interpreter. It is meant to provide a compatible, portable interpreter to play
VisualArts games, specifically those released under the Key label. It is
licensed to you under version 3 or later of the GNU General Public License as
published by the Free Software Foundation.

rlvm is meant to be a compatibility utility; it is not an excuse for
copyright infringement. Please do not acquire games from BitTorrent /
Share / {insert name of popular P2P app in your locale}.

## STATUS

| Titles | Status |
|--------|--------|
| Rewrite | Not Supported |
| Rewrite Harvest festa! | Not Supported |
| Angel Beats! 1st Beat | Partial Support |
| Harmonia | Not Supported |
| Summer Pockets | Not Supported |
| Summer Pockets Reflection Blue | Partial Support |
| Loopers | Partial Support |
| Planetarian HD | Not Supported |
| Planetarian: Snow Globe | Not Supported |
| Lunaria: Virtualized Moonchild | Partial Support |
| Stella of The End | Full Support |
| Prima Doll: Fuyuzora Hanabi / Sekka Monyō | Not Supported |
| Prima Doll: Mumei Tenrei | Not Supported |
| Kōsai Toshi: Augment Protocol | Not Supported |
| Anemoi | Not Supported |

Current rlvm development focuses on supporting newer Siglus games.
Changes may temporarily break RealLive support. I'll address these issues
and update the support status once the new features have stabilized and
I've done more testing.

## USING RLVM

If you are using Ubuntu or OSX, rlvm makes an effort to find a system
Japanese Gothic font. If rlvm is complaining that it can't find a font,
it will also look for the file "msgothic.ttc" in either the game
directory nor your home directory. If you are using Linux, you can
manually specify a font with the --font option.

    $ rlvm [/path/to/GameDirectory/]

The rlvm binary should be self contained and movable anywhere. To install rlvm,
go into the build directory and run:

	$ sudo make install

If you don't have the file "msgothic.ttc" in either the game directory
nor your home directory, please specify a Japanese font on the command
line with --font.

For Siglus games, use `--hook` to run a custom script on the game's machine
before loading the start scene:

```bash
rlvm --hook hooks.sr /path/to/GameDirectory/
```

The hook's globals, state changes, and debug registrations remain available to
the game. Hook errors stop startup.

Siglus exposes the startup arguments as VM globals before running the hook:
`__debug` is a boolean (default `false`), and `__scene` is the integer passed to
`--scene`, or `nil` when omitted. These values are available without a hook too;
assigning to them does not change the engine's startup settings.

Instruction tracing is separate from `--debug` and `--log-level`. Use `--trace`
or `--trace=script` for script instructions, `--trace=frame-action` for Siglus
frame-action callbacks, or `--trace=all` for both. Tracing is off by default.

rlvm uses Boost.Serialization archives for save files and other persistent
metadata. Compatibility with the original RealLive and Siglus serialization
formats is outside the scope of this project and will not be supported.

Archives remain unversioned while serialization support is under development.

## COMPILING RLVM

### Prerequisites

Before you begin, ensure you have the following libraries and utilities installed:

- **CMake** (version 3.24 or higher):
  [https://cmake.org/](https://cmake.org/)
- **Ninja**:
  [https://ninja-build.org/](https://ninja-build.org/)
- **Boost** (version 1.40 or higher):
  [https://www.boost.org/](https://www.boost.org/)
- **SDL3** (version 3.2.0 or higher) and **SDL3_ttf**:
  [https://www.libsdl.org/](https://www.libsdl.org/)
- **OpenGL** and **GLEW**.
- **FreeType**:
  [https://www.freetype.org/](https://www.freetype.org/)
- **Fontconfig**
- **GNU gettext**
- **zlib**
- **libpng**
- **vorbisfile** (part of the Ogg Vorbis audio codec):
  [https://xiph.org/vorbis/](https://xiph.org/vorbis/)
- **theoradec** (part of the Theora video codec):
  [https://www.theora.org/](https://www.theora.org/)

### Obtaining the Source Code

Clone the RLVM repository and initialize its submodules:

```bash
git clone https://github.com/YanekOwO/SuperEngine.git rlvm
cd rlvm
git submodule update --init --recursive
```

### Configuring and Building RLVM

Use the release CMake preset to configure and build the project:

1. **Configure the Build**

```bash
cmake --preset release
```

2. **Build the Project**

```bash
cmake --build --preset release
```

The project presets write build trees under `build/<preset-name>`. Put
machine-specific settings, such as custom compilers or `CMAKE_PREFIX_PATH`,
in a local `CMakeUserPresets.json` file.

### Running RLVM

After a successful build, you can run RLVM using the following command:

```bash
./build/release/rlvm
```

*Note:* RLVM runs without icons or localization support by default.

### Building and Running Unit Tests

To run unit tests, additional dependencies are required:

- **GoogleTest** and **GoogleMock**
- **Python 3** with the following packages: `numpy`, `wave`, `soundfile`

1. **Configure the Build with Tests Enabled**

   ```bash
   cmake --preset debug
   ```

2. **Build the Project**

   ```bash
   cmake --build --preset debug
   ```

3. **Run the Unit Tests**

   ```bash
   ctest --preset debug
   ```

All tests should pass successfully.

### Editing G00 Assets

The `g00` CLI inspects G00 images, exports editable PNGs with region metadata,
encodes PNGs and sprite sheets as lossless G00 files, and extracts individual
sprites. See [tools/README.md](tools/README.md) for build instructions and examples.

### Dumping Game Data with rlkp

The `rlkp` tool dumps RealLive and Siglus scripts as readable text and extracts
audio, images, and movies from a game directory. Use `-o` to write files to an
output directory; output defaults to standard output. For Siglus games, use
`--scenario` to dump a single scenario by number.

```bash
./build/release/rlkp /path/to/GameDirectory/ -o dump
./build/release/rlkp /path/to/SiglusGameDirectory/ --scenario 0 -o dump
```
