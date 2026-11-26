# Development plan

What is done and what is left. The full original plan had seven milestones; 1–4 are done.

## Done

| Milestone | What |
|---|---|
| 1. Engine | 0/1/X values on 1–32 bit buses, gates, event-driven simulator with gate delays (latches hold, oscillators oscillate, no zero-width glitches) |
| 2. Circuits | Circuit document, netlist compiler (chips flattened, tunnels, splitters, error messages), JSON files |
| 3. Sequential parts | Flip-flops, latches, register, counter, RAM, ROM, multiplexer, demultiplexer, decoder, priority encoder, all tested |
| 4. Editor (MVP) | Canvas with pan/zoom, placing, wiring (with bends and branches), moving, rotating, undo/redo, copy/paste, live simulation with run/pause/step/speed, properties panel, open/save, examples |

## Next: fixes and polish

1. **Keep the simulation state when editing.** Moving or rotating a part recompiles the circuit
   and restarts the simulation, so flip-flops, registers, counters and RAM go back to 0. Moving
   and rotating do not change the netlist and should not recompile. A real change (a new wire or
   part) should carry over the stored values of the parts that still exist.
2. **Text notes:** size the box to the text, so long notes can be clicked and selected.
3. **Overlapping wires:** wires of different nets lying on the same grid line look like one wire.
   Warn about it (or highlight them), or keep new wires from running along existing ones.
4. **Line endings:** add a `.gitattributes` (`* text=auto`, LF for sources) so git stops warning
   about CRLF on every commit.
5. **Readability when zoomed out:** pin labels on boxes become unreadable at the fit-to-window
   zoom of large circuits; hide them below a size or draw them larger.
6. **A more obvious Exit button:** a red "Exit" button at the right end of the toolbar (as in
   AlgorithmVisualizer), next to File > Quit and Ctrl+Q; it asks before discarding unsaved changes.

## Milestone 5: custom chips in the editor and more examples

- **Create chip from selection:** the selected Input pin / Output pin components become the chip's
  pins (top to bottom); ask for a name and colour; add it to the circuit's library.
- **Chips in the toolbox** (a "Chips" section) to place instances.
- **Edit a chip:** open its inside circuit with a breadcrumb back to the parent; changes update
  every instance. Reject a chip that would contain itself (the engine already does).
- **Import / export chip libraries** (File menu); the JSON format already has `saveLibrary`.
- **Examples:** half adder → full adder → 4-bit adder → 4-bit ALU (add, subtract, AND, OR, with
  flags) as nested chips, D latch and master-slave D flip-flop from gates, ring oscillator,
  4-bit counter on a 7-segment display, multiplexer/decoder demo, register file, RAM read/write.
  `examples_test` checks each one (the ALU exhaustively).

## Milestone 6: release

- **Logo:** a small logo drawn in code (e.g. an AND gate with a lit green output wire on the dark
  canvas colour), made into an `sf::Image` at startup and set as the window icon
  (`window.setIcon`), shown in Help > About and on the start screen of the learning mode. A
  generated `.ico` (Windows resource file, so the .exe has the icon in Explorer) and a PNG for the
  README and the macOS/Linux packages.
- **README:** screenshot, feature list, controls (mostly there already).
- **docs/building.md:** MSYS2, WSL/Linux, macOS steps (like Minesweeper).
- **CI** (`.github/workflows/ci.yml`): build and test on Windows (MSYS2 UCRT64), Linux (Ubuntu
  with SFML 3 from apt, and one with SFML downloaded) and macOS (Homebrew). Tests run headless;
  `editor_test` needs no window.
- **Package** (`package.yml`): static SFML builds for Windows x64, Linux x64 and macOS arm64,
  archive = program + `examples/` + README.
- **Release** (`release.yml`): a `v*` tag builds the packages and publishes a GitHub release.
- Tag **v1.0.0**.

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
