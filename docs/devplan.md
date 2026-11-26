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

- **README:** screenshot, feature list, controls (mostly there already).
- **docs/building.md:** MSYS2, WSL/Linux, macOS steps (like Minesweeper).
- **CI** (`.github/workflows/ci.yml`): build and test on Windows (MSYS2 UCRT64), Linux (Ubuntu
  with SFML 3 from apt, and one with SFML downloaded) and macOS (Homebrew). Tests run headless;
  `editor_test` needs no window.
- **Package** (`package.yml`): static SFML builds for Windows x64, Linux x64 and macOS arm64,
  archive = program + `examples/` + README.
- **Release** (`release.yml`): a `v*` tag builds the packages and publishes a GitHub release.
- Tag **v1.0.0**.

## Milestone 7 (v1.1): a tiny CPU

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
