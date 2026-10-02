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

| Titles                 | Status          | Notes                                                     |
|------------------------|-----------------|-----------------------------------------------------------|
| Kanon Standard Edition | Full Support    | Also supports [NDT's patch][kanon]                        |
| Air Standard Edition   | Full Support    |                                                           |
| CLANNAD                | Full Support    |                                                           |
| CLANNAD (Full Voice)   | Full Support    | [Licensed][clannad]                                       |
| CLANNAD Side Stories   | Partial Support | [Licensed][clannad_side_stories]                          |
| Planetarian CD         | Full Support    | [Discontinued][planetarian], Planetarian HD not supported |
| Tomoyo After           | Full Support    |                                                           |
| Little Busters         | Partial Support | Untested, Steam edition not supported                     |
| Kud Wafter             | Full Support    |                                                           |

[kanon]: http://radicalr.pestermom.com/vn.html
[clannad]: http://store.steampowered.com/app/324160/
[clannad_side_stories]: https://store.steampowered.com/app/420100/
[planetarian]: http://store.steampowered.com/app/316720/

rlvm is now at the point where enough commands are implemented that
other games *may* work. The above is only a list of what I've tested
first hand or what I've been told.

rlvm has an implementation of rlBabel; English patches compiled with
Haeleth's rlBabel should line break correctly. I've successfully tested
it with the Kanon patch from NDT.

rlvm supports KOE, NWK and OVK archives for voices, along with ogg
vorbis voice patches which follow the convention
<packnumber>/z<packnumber><sampleid>.ogg.

Fullscreen can be entered by pressing Alt+{F,Enter} on Linux and
Command+{F,Enter} on Mac.

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
- **GNU gettext**
- **zlib**
- **vorbisfile** (part of the Ogg Vorbis audio codec):
  [https://xiph.org/vorbis/](https://xiph.org/vorbis/)
- **theoradec** (part of the Theora video codec):
  [https://www.theora.org/](https://www.theora.org/)

### Obtaining the Source Code

Clone the RLVM repository and initialize its submodules:

```bash
git clone https://github.com/Mickey-snow/SuperEngine.git rlvm
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


## KNOWN ISSUES

- **Clannad Side Stories**: The game runs, but due to incomplete opcode support,
  several issues remain:
  - Missing sound effects.
  - Menu may not work.

  It is advised to delete `~/.rlvm/KEY_HIKARI_SAKA_STEAM_EN` before first running
  the game in newer versions of rlvm.

- **Little Busters' Baseball Minigame**: This minigame doesn't function because
  it relies on a Windows DLL, which rlvm does not support. Similarly, the
  **dungeon crawling minigame** in *Tomoyo After* is also affected for the same
  reason.

- **Steam Edition of Little Busters**: The asset files in this version are
  packed into `.PAK` archives, which rlvm currently cannot recognize. As a
  result, this edition of the game is not supported.
