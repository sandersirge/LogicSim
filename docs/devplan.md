# Development plan

What is done and what is left. The full original plan had seven milestones; 1–5 and the polish
rounds are done, and milestone 6 (the release) up to tagging v1.0.0.

## Done

| Milestone | What |
|---|---|
| 1. Engine | 0/1/X values on 1–32 bit buses, gates, event-driven simulator with gate delays (latches hold, oscillators oscillate, no zero-width glitches) |
| 2. Circuits | Circuit document, netlist compiler (chips flattened, tunnels, splitters, error messages), JSON files |
| 3. Sequential parts | Flip-flops, latches, register, counter, RAM, ROM, multiplexer, demultiplexer, decoder, priority encoder, all tested |
| 4. Editor (MVP) | Canvas with pan/zoom, placing, wiring (with bends and branches), moving, rotating, undo/redo, copy/paste, live simulation with run/pause/step/speed, properties panel, open/save, examples |
| Fixes and polish | Editing keeps the simulation state (moving/rotating does not recompile; other edits carry stored values, RAM, clock phase and wire values over to the recompiled circuit, matched by component path; Reset powers on); text notes sized to their text; wires of different signals on top of each other highlighted in orange with a warning (examples checked by `editor_test`); `.gitattributes` with LF line endings; pin labels hidden and names/values/notes kept at a readable size when zoomed out; red Exit button in the toolbar |
| Audit fixes | Undo/redo and other commands during a drag end it first (Esc takes it back); saving over an existing file asks; a new wire drawn without bends takes a column free of other signals' wires and pins; junction dots cached instead of recomputed every frame; text notes do not rotate; the simulator no longer applies (and double-counts) an event that was replaced at the same tick when a later one follows, which could end `settle()` early |
| 5. Chips in the editor | New chip from selection (name, colour, description); a Chips section in the toolbox; double-click or Open chip edits its inside with breadcrumbs back, its inputs clickable to try it out; wires to instances follow renamed or moved pins; rename, colour, description and delete (unused chips); a chip cannot contain itself; File > Import / Export chips (name clashes renamed); examples: 4-bit ALU from nested adder chips, gate-level D latch and master-slave flip-flop, ring oscillator, counter on a 7-segment display, multiplexer/decoder, register file, RAM, each checked by `examples_test` (the ALU exhaustively) |
| 6. Release (all but the tag) | Logo drawn in code (`src/gui/logo.cpp`, tested by `logo_test`): window icon and Help > About; `logicsim_icons` / `make icons` writes `res/logicsim.png`, `.ico` (in `logicsim.exe` with version information) and `.icns`; `--version`, `--help`; macOS app bundle option; README with logo, screenshot and downloads; `docs/building.md`; CI (Ubuntu 24.04 and 26.04 with SFML downloaded, as no Ubuntu ships SFML 3.1; Windows MSYS2; macOS Homebrew; engine alone; sanitizers; pinned clang-format; screenshots of every example under Xvfb); Package (static SFML, Windows zip with no DLLs, Linux tar.gz and AppImage, macOS `LogicSim.app`, each with the examples and licence texts); Release on a `v*` tag |

## Milestone 6: what is left

- Run the Package workflow once by hand and try the downloads on each system.
- Tag **v1.0.0** (`git tag -s v1.0.0 -m "LogicSim 1.0.0" && git push origin v1.0.0`); the Release
  workflow publishes the downloads.
- The start screen of the learning mode shows the logo (milestone 7).
- Toolbox: long part names ("Multiplexer", "Demultiplexer", "Priority encoder") are clipped in
  their half-width buttons, more so with Linux's wider DejaVu font; shorten them or give them a
  full row.

## Milestone 7 (v1.1): learning mode

Two parts: a short guided tour of the app, and a course that teaches digital logic from the
ground up in lessons of increasing difficulty.

### App tour

- Opens on the first start (and from Help > Tour): a sequence of tooltips pointing at the toolbox,
  canvas, toolbar, properties and status bar, each with Next / Back / Skip.
- Interactive steps that wait for the user to do the thing ("place a switch", "wire it to the
  LED", "click the switch", "press Space to pause") and move on by themselves.

### Course

- **A Learn window** (Help > Learn, and a button in the toolbar): topics in order, each with its
  lessons, a difficulty mark (1�3 dots) and the state of each lesson. Opening a lesson starts a
  fresh canvas, possibly with parts already placed and locked (given inputs and outputs).
- **Lessons are step by step.** A side panel shows the current step's explanation (text and
  small pictures/diagrams), the goal and a "Check" button. Steps are checked automatically
  where possible:
  - *Action steps* check the document ("an AND gate is placed", "the LED is wired to the gate").
  - *Circuit steps* check behaviour with the engine: the lesson names the input and output pins
    and gives a truth table or a clock-by-clock sequence; the user's circuit is compiled and run
    against it (like the tests do), and a failing row is shown ("A=1, B=0: expected S=1, got 0").
- **Hints:** each step has up to three hints that get more specific (from "think about when the
  output must be 1" to "use an XOR gate"); a "Show me" as the last hint places the solution for
  that step. Using hints is remembered but does not block completion.
- **Progress:**
  - a progress bar per lesson (steps done / steps), per topic and for the whole course;
  - a completed lesson gets a **tick in a green circle** in the lesson list (topics too, once
    all their lessons are done);
  - progress is saved in the user's settings folder (`%APPDATA%/LogicSim/progress.json`,
    `~/.config/LogicSim/` on Linux, `~/Library/Application Support/LogicSim/` on macOS), with
    "Reset progress" in the Learn window.
- **Lesson files** are JSON in `lessons/` (shipped next to the program like `examples/`): topic,
  title, difficulty, starting circuit, and steps (text, check, hints, solution). A `lessons_test`
  runs every lesson's own solution through its checks, so broken lessons fail CI.

### Topics and lessons (easiest first)

1. **Signals and gates:** switches and LEDs; AND, OR, NOT; wires and branches; the X (unknown)
   value; gate delays and watching a signal travel.
2. **Combining gates:** NAND and NOR; building every gate from NAND alone; XOR from basic gates;
   truth tables; De Morgan's laws; a 2-input multiplexer from gates.
3. **Numbers and buses:** binary and hexadecimal; multi-bit wires; splitters and mergers;
   tunnels; decoders, encoders and multiplexers on buses; 7-segment and hex displays.
4. **Arithmetic:** half adder; full adder (as a chip); 4-bit ripple adder; two's complement and
   subtraction; comparing numbers; a 4-bit ALU with flags.
5. **Memory:** feedback and the SR latch; D latch; edge-triggered D flip-flop (master-slave);
   JK and T flip-flops; registers; counters; shift registers (serial and parallel load).
6. **Clocks and state machines:** clocks and timing (setup time, glitches); designing a state
   machine; a traffic-light controller; a sequence detector.
7. **Memory and computers** (with milestone 8): RAM and ROM; a register file; a program counter;
   fetching and decoding instructions; the tiny CPU.

## Milestone 8 (v1.2): a tiny CPU

- Built-in arithmetic parts: adder, ALU, comparator (faster and smaller than gate-level chips).
- An 8-bit accumulator CPU with about 16 instructions (load, store, add, subtract, logic, jumps,
  conditional jumps, halt) built from registers, a program counter, ROM, RAM and the ALU, as an
  example circuit.
- A demo program in ROM (e.g. counting, or Fibonacci numbers on a display), entered as hex in the
  ROM properties.
- Possibly a tiny assembler (text → ROM contents) in the properties panel.

## Ideas for later

- Tri-state buffers and shared buses (a Z value next to 0/1/X).
- Timing diagram view: record chosen wires over time.
- Truth table generator for a selected combinational circuit.
- Wire auto-routing around parts.
