# Building and running

Every way to build, run and test LogicSim: **CMake**, the **Makefile** wrapper and **CLion**, on
Windows (MSYS2), Linux/WSL and macOS. Commands run from the project root.

- [1. Install the tools](#1-install-the-tools)
- [2. Build with CMake](#2-build-with-cmake)
- [3. The Makefile wrapper](#3-the-makefile-wrapper)
- [4. CLion](#4-clion)
- [5. Format and check the code](#5-format-and-check-the-code)
- [6. The icon](#6-the-icon)
- [7. Continuous integration, packages and releases](#7-continuous-integration-packages-and-releases)
- [8. Troubleshooting](#8-troubleshooting)

## 1. Install the tools

You need CMake 3.28 or newer, Ninja (or make) and a C++23 compiler: GCC 13+, Clang 17+ or
Apple Clang 15+, or MSVC 19.38+. SFML 3.1 is used when it is installed and downloaded and built
otherwise. Dear ImGui and ImGui-SFML are always downloaded by CMake (first configure only), and
nlohmann/json when it is not installed.

### Windows (MSYS2)

```shell
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,gdb,cmake,ninja,make,pkgconf,sfml}
```

Put `C:\msys64\ucrt64\bin` in your user PATH, **before** other compilers (Strawberry Perl, for
example, brings its own `g++` and `cmake`), so PowerShell and CLion find the right compiler and the
programs find the SFML DLLs. A program that stops at once with `0xC0000139` found the wrong DLLs.

### Linux and WSL (Ubuntu/Debian)

```shell
sudo apt install build-essential cmake ninja-build pkg-config gdb \
  libxrandr-dev libxcursor-dev libxi-dev libudev-dev libfreetype-dev libharfbuzz-dev libgl1-mesa-dev libegl1-mesa-dev
```

The X11/OpenGL/FreeType/HarfBuzz packages are what SFML needs when CMake builds it (no Ubuntu
release ships SFML 3.1 yet). The app needs a display; under WSL use WSLg (Windows 11).

### macOS

```shell
brew install cmake ninja sfml
```

## 2. Build with CMake

```shell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
build/bin/logicsim examples/basics.json
```

Targets: `logicsim` (the app), `logicsim_core` (the engine, no graphics), the tests (`*_test`,
one per file in `tests/`), and `logicsim_icons` (see [the icon](#6-the-icon); not built by
default). The examples are copied next to the program (into the app bundle on macOS), so
File > Examples finds them.

| Option | Default | Meaning |
|---|---|---|
| `LOGICSIM_BUILD_GUI` | `ON` | `OFF` builds only the engine and its tests (no SFML or ImGui needed) |
| `LOGICSIM_FETCH_SFML` | `ON` | download and build SFML 3.1 when it is not installed |
| `LOGICSIM_MACOS_BUNDLE` | `OFF` | macOS: build `bin/LogicSim.app` (icon, Info.plist, examples inside) instead of `bin/logicsim` |
| `LOGICSIM_SANITIZE` | `OFF` | build with AddressSanitizer + UndefinedBehaviorSanitizer and libstdc++ bounds checks (GCC/Clang on Linux and macOS; `/fsanitize=address` with MSVC) |
| `BUILD_TESTING` | `ON` | build the tests |

### Command line

```text
logicsim [circuit.json]                               open the app (and the circuit)
logicsim circuit.json --screenshot out.png --ticks 50 save a picture after 50 ticks and quit
logicsim --version | --help
```

`--about` shows the About dialog (for a screenshot of it). On Windows `--version` and `--help`
print to the console the program was started from.

## 3. The Makefile wrapper

`make` calls CMake for you:

| Command | Does |
|---|---|
| `make` | configure (first time) and build into `build/` |
| `make run` | build and start the app |
| `make test` | build and run the tests |
| `make format` | clang-format every source file |
| `make format-check` | report formatting differences without changing anything |
| `make sanitize` | build and test the engine with sanitizers in `build-sanitize/` (GCC or Clang) |
| `make icons` | redraw `res/logicsim.png`, `.ico` and `.icns` from the logo in code |
| `make clean` | delete `build/` |

`make BUILD_TYPE=Debug` and `make BUILD_DIR=build-wsl` change the build type and folder.

## 4. CLion

Open the folder as a CMake project. On Windows choose the **MSYS2 UCRT64** toolchain (Settings →
Build, Execution, Deployment → Toolchains). Both the CMake bundled with CLion and MSYS2's own CMake
work. Run the **logicsim** configuration for the app (add `examples/basics.json` as program
argument to open a circuit) and the test targets (or CTest) for the tests.

## 5. Format and check the code

The style is in `.clang-format` (LLVM based, tabs, 110 columns). Versions of clang-format differ in
small details, so the CI uses **clang-format 21.1.8** and fails on any difference. Install the same
version and format from a terminal:

```shell
pip install clang-format==21.1.8    # or: pipx install clang-format==21.1.8
make format
```

CLion's bundled clang-format may format a few lines differently; point CLion at the pinned one
(Settings → Editor → Code Style → ClangFormat) or run `make format` before committing.

Sanitizers: `make sanitize` (or configure with `-DLOGICSIM_SANITIZE=ON`) runs the engine's tests
under AddressSanitizer and UndefinedBehaviorSanitizer. MinGW has no AddressSanitizer; on Windows use
WSL.

## 6. The icon

The logo (an AND gate with lit wires) is drawn in code, in `src/gui/logo.cpp`. The app draws it at
start-up for the window icon and Help > About. The icon files in `res/` are made from the same
drawing by the `logicsim_icons` tool and committed:

- `logicsim.png` (512 × 512): the README, the Linux `.desktop` entry and the AppImage
- `logicsim.ico` (16–256 px): compiled into `logicsim.exe` with its version information
  (`cmake/logicsim.rc.in`), so Explorer and the taskbar show it
- `logicsim.icns` (16–1024 px): the macOS app bundle

After changing the drawing run `make icons` (or build the `logicsim_icons` target and run it with
`res` as argument) and commit the new files.

## 7. Continuous integration, packages and releases

- **CI** (`.github/workflows/ci.yml`): builds and tests on Ubuntu 24.04 and 26.04, Windows
  (MSYS2 UCRT64) and macOS, takes a screenshot of every example under a virtual display (GUI smoke
  test), builds the engine alone, runs its tests with sanitizers (GCC and Clang) and checks the
  formatting. Tests run without a window.
- **Package** (`package.yml`): builds downloads with SFML linked statically (and on Windows the
  compiler runtime too), so `logicsim.exe` runs without any DLLs next to it. Start it by hand under
  *Actions → Package → Run workflow*; the archives appear as artifacts of the run.
- The downloads: Windows `logicsim.exe` (with its icon and version information) in a zip; Linux the
  program in a tar.gz and an AppImage (made with linuxdeploy); macOS `LogicSim.app` (an ad-hoc
  signed bundle, configured with `-DLOGICSIM_MACOS_BUNDLE=ON`) in a zip. Each contains the example
  circuits, the README and a `licenses` folder with the licence texts of the libraries linked into
  it (SFML, FreeType, HarfBuzz, SheenBidi, Dear ImGui, ImGui-SFML, nlohmann/json).
- **Release** (`release.yml`): pushing a version tag builds the packages and publishes a GitHub
  release with them and a `SHA256SUMS.txt` (check a download with `sha256sum -c SHA256SUMS.txt
  --ignore-missing`). The version in `project()` in `CMakeLists.txt` should match the tag:

```shell
git tag -s v1.0.0 -m "LogicSim 1.0.0"
git push origin v1.0.0
```

## 8. Troubleshooting

- **`Could NOT find Freetype` with the CMake bundled with CLion**: fixed in `CMakeLists.txt`
  (ImGui-SFML reuses the SFML targets instead of searching again). If an old build folder still
  fails, delete it and reload.
- **`ld: cannot open output file logicsim.exe: Permission denied`**: the app is still running;
  close it and build again.
- **The program stops at once with `0xC0000139` (Windows)**: it found other DLLs than the ones it
  was built with; put `C:\msys64\ucrt64\bin` first in PATH (see above).
- **File > Examples is empty**: the app looks for `examples/` next to the program, in
  `../share/logicsim/examples`, in the app bundle's Resources and in the working folder; run it
  from the build folder or the project root.
- **The app starts without text**: it loads a system font (Segoe UI on Windows, SF or Helvetica on
  macOS, DejaVu Sans on Linux) and falls back to ImGui's built-in font when it is missing; install
  `fonts-dejavu` on Linux.
- **macOS says the app is damaged or from an unidentified developer**: the downloads are not
  signed with an Apple certificate; right-click the app and choose Open, or run
  `xattr -dr com.apple.quarantine LogicSim.app`.
- **Linux: `libXcursor.so.1: cannot open shared object file`** (the tar.gz on a minimal system
  without a desktop): `sudo apt install libxcursor1 libxi6`, or use the AppImage, which brings
  them along.
