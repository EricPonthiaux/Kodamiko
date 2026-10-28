# Contributing to Kodamiko

Thank you for your interest. Bug reports, ideas and pull requests are all
welcome. Kodamiko is young: before a large change, open an issue to talk about
it, so that no work is lost.

## Reporting a bug

Open an issue with the *Bug report* template: what you did, what you
expected, what happened, and your system, Qt and Claude Code versions
(*Help ▸ About Kodamiko*). A security problem is not reported in an issue: see
[`SECURITY.md`](SECURITY.md).

## Building and testing

Kodamiko builds with CMake on Linux, Windows and macOS. Only Qt is installed
on the machine; the other libraries are downloaded and built on the first
configuration. The CI (`.github/workflows/ci.yml`) does every step below on
Ubuntu 24.04, Windows Server 2022 and macOS 15: when this file and the CI
disagree, the CI is right.

### What it is built from

Tools, on every system: **CMake 3.24+**, **Ninja**, git, and a **C++23
compiler**: GCC 13+, Clang 17+, Apple Clang 16+ or MSVC 2022.

| Library | Version | How it gets there | Used for |
|---|---|---|---|
| Qt 6: Core, Gui, Widgets, Network, PrintSupport, Test, WebEngineWidgets (with WebChannel and Positioning) | 6.10+ (the CI uses 6.10.3) | installed by you | the whole interface; WebEngine for the Markdown and HTML previews |
| Qt Svg | 6.10+ | optional | |
| QScintilla | 2.14.1 | downloaded, built statically | the code editor |
| tree-sitter, with the C, C++, Java, JavaScript, TypeScript and Python grammars | 0.26.13 | downloaded, built statically | highlighting, outlines |
| libvterm | 0.3.3 | downloaded, built statically | the terminals |
| libgit2 | 1.9.7 | downloaded, built statically | reading the repository |
| cmark-gfm | 0.29.0.gfm.13 | downloaded, built statically | Markdown |
| Graphviz (`libgvc`, `libcgraph`) | the system's; 16.1.0 on Windows | optional on Linux and macOS (found with pkg-config); downloaded on Windows | the diagram editor's layouts; without it, diagram files open as text |

The versions are pinned in `cmake/Dependencies.cmake` and
`cmake/GraphvizWindows.cmake`. The downloads land in `<build>/_deps/` and are
not checked again: configuring needs no network after the first time, and
changing a pinned version fetches the new one.

### Qt

Kodamiko needs Qt 6.10 or newer, which most distributions do not ship yet.
Install it with the [Qt Online Installer](https://www.qt.io/download-qt-installer)
(the *Qt WebEngine*, *Qt WebChannel* and *Qt Positioning* modules included), or
with [aqtinstall](https://github.com/miurahr/aqtinstall), as the CI does:

```sh
pip install aqtinstall
aqt install-qt linux   desktop 6.10.3 linux_gcc_64      -m qtwebengine qtwebchannel qtpositioning -O ~/Qt
aqt install-qt mac     desktop 6.10.3 clang_64          -m qtwebengine qtwebchannel qtpositioning -O ~/Qt
aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -m qtwebengine qtwebchannel qtpositioning -O C:\Qt
```

Then point CMake at the kit, through `CMAKE_PREFIX_PATH`
(`~/Qt/6.10.3/gcc_64`, `~/Qt/6.10.3/macos`, `C:\Qt\6.10.3\msvc2022_64`).

### Linux

On Ubuntu 24.04 or Debian, the packages the CI installs:

```sh
sudo apt-get install build-essential ninja-build cmake git pkg-config \
    libgl1-mesa-dev libxkbcommon-dev libgraphviz-dev
```

Other distributions need the same: a compiler, Ninja, OpenGL and
xkbcommon headers, and Graphviz's development files for the diagrams.

```sh
export CMAKE_PREFIX_PATH=~/Qt/6.10.3/gcc_64
cmake --preset debug
cmake --build --preset debug
./build-debug/bin/kodamiko [project-folder]
```

### macOS

Install the Xcode Command Line Tools (`xcode-select --install`, for Apple
Clang), then the tools with [Homebrew](https://brew.sh):

```sh
brew install cmake ninja graphviz pkg-config
export CMAKE_PREFIX_PATH=~/Qt/6.10.3/macos
cmake --preset debug
cmake --build --preset debug
open build-debug/bin/Kodamiko.app --args [project-folder]
```

The build is an application bundle, signed ad hoc: each rebuild asks again
for access to the Desktop and Documents folders. Signed with a stable
identity, it asks once: create a code-signing certificate in *Keychain
Access ▸ Certificate Assistant ▸ Create a Certificate…* (type *Code
Signing*), or use an Apple Development one, then configure with its name:

```sh
cmake --preset debug -DKDK_CODESIGN_IDENTITY="Kodamiko Development"
```

### Windows

Install Visual Studio 2022 or later with the *Desktop development with C++*
workload, which brings MSVC, CMake and Ninja, and Qt's `msvc2022_64` kit.
Graphviz is downloaded on the first configuration. From an *x64 Native
Tools Command Prompt*:

```bat
set CMAKE_PREFIX_PATH=C:\Qt\6.10.3\msvc2022_64
set PATH=%CMAKE_PREFIX_PATH%\bin;%PATH%
cmake --preset windows-debug
cmake --build --preset windows-debug
build-win-debug\bin\kodamiko.exe [project-folder]
```

Qt's `bin` folder in `PATH` lets the tests and Kodamiko find Qt's DLLs.

### Presets and options

| Preset | Build | Folder | Notes |
|---|---|---|---|
| `debug` | Debug | `build-debug` | for development |
| `default` | Release, link-time optimization, `-march=native` | `build` | fastest, for this machine only |
| `profile` | optimized, with symbols and frame pointers | `build-profile` | for a profiler |
| `windows-debug` | Debug, MSVC | `build-win-debug` | Windows only |
| `windows` | Release, link-time optimization, MSVC | `build-win` | Windows only |

Each has a build and a test preset of the same name. The options:

- `KDK_LTO`: link-time optimization (on in `default` and `windows`).
- `KDK_NATIVE`: code for this machine's processor (on in `default`); never
  for a package meant for other machines: the CI configures `default` with
  `-DKDK_LTO=OFF -DKDK_NATIVE=OFF`.
- `KDK_CLANG_TIDY`: analyze each source with clang-tidy as it compiles.
- `KDK_CODESIGN_IDENTITY` (macOS): the identity that signs the bundle.

### Tests

```sh
ctest --preset debug             # QT_QPA_PLATFORM=offscreen without a display
```

Each test writes its report to `<build>/test-logs/`. Some tests need a tool
of the machine and are skipped without it: git, tar, a POSIX shell, clangd,
clang-format, jdtls. The tests that download from the Internet run only
with `KDK_NETWORK_TESTS=1`. A feature of the system (terminals, processes,
git credentials, toolchains) is tested once per system, in `tests/unix/`
and `tests/windows/`: see `CLAUDE.md` §11.

### Packages

The CI installs the `kodamiko` component, then bundles Qt with Qt's own
tools:

- **Linux**: `cmake --install build --component kodamiko --prefix AppDir/usr`,
  then [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) and its Qt
  plugin make an AppImage (`libfuse2` runs them).
- **Windows**: `cmake --install` into `Kodamiko/`, `windeployqt`, then a zip.
- **macOS**: `cmake --install` into `dist/`, `macdeployqt`, an ad hoc
  signature that seals the bundle (`codesign --sign -`), then `hdiutil`
  makes the dmg.

`kodamiko-hook`, `kodamiko-mcp` and `kodamiko-askpass` ship beside the
executable: Kodamiko finds them there.

### Running

Kodamiko needs [Claude Code](https://claude.com/claude-code) 2.1+ and git
2.23+. If either is missing, *Help ▸ Check Prerequisites* opens on startup.

## Before you open a pull request

The conventions in [`CLAUDE.md`](CLAUDE.md) are not suggestions: every
change follows them, whether a person or Claude wrote it. In short:
functions of cognitive complexity ≤ 15 and nesting ≤ 3, names that need no
comment, ownership readable from the types, comments that say *why*, and
everything in English. **If you are thinking of submitting a pull request
that does not follow these rules, please don't: it will not be merged.**

Also check that:

- A behavior change comes with a Qt Test named after the behavior
  (`highlightsEveryOccurrenceOfTheNameAtTheCursor`).
- It builds without warnings, and these report nothing:
  ```sh
  cmake --build build-debug --target format-check
  cmake --build build-debug --target tidy
  ctest --preset debug
  ```
  `git config core.hooksPath tools/git-hooks` checks the formatting before
  each commit.
- One pull request does one thing. Its commit messages say what changes
  for the user, in the style of the history (`git log`).

## License

Kodamiko is distributed under the GNU GPL v3 with an additional permission for
Graphviz (see [`README.md`](README.md#license)). By contributing, you agree
that your contribution is distributed under the same terms. Every new source
file starts with `// SPDX-License-Identifier: GPL-3.0-only`.
