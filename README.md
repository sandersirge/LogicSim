# LogicSim

A logic-gate simulator: place gates on a grid, wire them together and watch the signals
propagate, from single gates to adders, latches, counters and memory.

C++23, SFML 3.1 and Dear ImGui. The simulation engine (`src/core`) has no GUI dependency and is
covered by unit tests.

## Features

- Gates: AND, OR, NOT, NAND, NOR, XOR, XNOR, buffer (2–8 inputs, 1–32 bit buses)
- Inputs and outputs: switch, button, constant, clock, LED, 7-segment display, hex digit, probe,
  input and output pins
- Wiring: splitter, merger, tunnel (named connection), text notes
- Plexers: multiplexer, demultiplexer, decoder, priority encoder
- Flip-flops: SR latch, D latch, D / JK / T flip-flops with asynchronous set and reset
- Memory: register, counter, RAM, ROM
- Event-driven simulation with gate delays: feedback loops work (latches hold, ring
  oscillators oscillate), every wire is unknown (X, blue) until something drives it
- Run, pause, step one tick or one event, speed from 1 to 100 000 ticks per second
- Undo / redo, copy / paste, rotate, save and open circuits as JSON

Custom chips (a selection of gates as a reusable part) work in the engine and the file format; the
editor support for making them comes in a later version.

## Building

Needs CMake 3.28+, a C++23 compiler and SFML 3.1. CMake downloads Dear ImGui, ImGui-SFML and
nlohmann/json, and also SFML if it is not installed.

Windows (MSYS2 UCRT64):

```
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-sfml
make            # build into build/
make run        # build and start the app
make test       # build and run the tests
```

Linux / WSL / macOS: the same `make` targets (install SFML 3 with your package manager, or let
CMake build it). Without make:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
build/bin/logicsim examples/basics.json
```

`-DLOGICSIM_BUILD_GUI=OFF` builds only the engine and its tests (no SFML or ImGui needed).

## Using it

| Do this | To |
|---|---|
| Click a part in the toolbox, then click on the grid | place it (hold Shift to place more) |
| Drag from a pin to another pin | wire them; clicks on the grid on the way add bends |
| Drag from the middle of a wire | branch it: a new wire from the same output, with a dot where it splits |
| Click a switch, or a bit of an input pin | flip it (hold a button down) |
| Drag a part / drag on empty grid | move it / select an area |
| Right or middle drag, mouse wheel | pan, zoom |
| Right click or Esc | cancel wiring or placing |
| R, Del, Ctrl+C / X / V / D, Ctrl+Z / Y | rotate, delete, copy / cut / paste / duplicate, undo / redo |
| Space, `.` | run or pause, step one tick |
| F | fit the circuit in view |

Wire colours: bright green 1, dark green 0, blue unknown (X), yellow partly known. Hover a pin
to see its value in the status bar.

Example circuits are in `examples/` (File > Examples).

`logicsim circuit.json --screenshot out.png --ticks 50` saves a picture after 50 ticks and quits.
