// Editing a running circuit: after a recompile, Simulator::carryOver keeps the state of the parts
// that are still there (counters, latches made of gates, RAM, clock phase, chips) and starts new
// or changed parts as at power-on.
#include "bench.hpp"

using namespace test;

namespace {

// What the editor does after an edit: recompile and continue the previous simulation
void recompile(Bench& b) {
	Compiled next = compile(b.doc.main, b.doc.library);
	Simulator sim(next.netlist);
	sim.carryOver(b.sim);
	b.compiled = std::move(next);
	b.sim = std::move(sim);
	b.settle();
}

Props widthProps(Kind kind, int width) {
	Props p = defaultProps(kind);
	p.width = width;
	return p;
}

// Flips a switch the way the editor does: in the circuit too, which the next recompile starts from
void set(Bench& b, int id, std::uint32_t value) {
	b.circuit().find(id)->props.value = value;
	b.set(id, value);
}

void pulse(Bench& b, int clk) {
	set(b, clk, 1);
	b.settle();
	set(b, clk, 0);
	b.settle();
}

void testCounterKeepsCount() {
	Bench b;
	int clk = b.input(1, "CLK");
	int cnt = b.add(Kind::Counter, widthProps(Kind::Counter, 4));
	int q = b.output(4, "Q");
	b.wire(clk, "out", cnt, "CLK");
	b.wire(cnt, "Q", q, "in");
	b.build();
	b.settle();
	for (int i = 0; i < 5; i++) {
		pulse(b, clk);
	}
	check(b.get(q) == 5, "counter at 5");
	Tick before = b.sim.now();

	// A new part and wire elsewhere in the circuit
	int led = b.add(Kind::Led);
	b.wire(cnt, "CO", led, "in");
	recompile(b);
	check(b.get(q) == 5, std::format("count kept after an edit ({})", b.get(q)));
	check(b.sim.now() >= before, "time goes on");
	check(b.read(led) == Value::of(0, 1), "the new LED shows the carry");
	pulse(b, clk);
	check(b.get(q) == 6, "and counting continues");

	// A counter whose width changed starts again from 0
	b.circuit().find(cnt)->props.width = 8;
	b.circuit().find(q)->props.width = 8;
	recompile(b);
	check(b.get(q) == 0, std::format("an 8-bit counter instead of a 4-bit one starts at 0 ({})", b.get(q)));
}

void testGateLatchKeepsValue() {
	// The state of a latch made of NOR gates is in its wires, not in a stored value
	Bench b;
	int s = b.input(1, "S"), r = b.input(1, "R");
	int top = b.gate(Kind::Nor), bottom = b.gate(Kind::Nor);
	int q = b.output(1, "Q");
	b.wire(r, "out", top, "in0");
	b.wire(bottom, "out", top, "in1");
	b.wire(s, "out", bottom, "in0");
	b.wire(top, "out", bottom, "in1");
	b.wire(top, "out", q, "in");
	b.build();
	b.settle();
	set(b, s, 1);
	b.settle();
	set(b, s, 0);
	b.settle();
	check(b.get(q) == 1, "latch set");

	int qn = b.output(1, "Qn");
	b.wire(bottom, "out", qn, "in");
	recompile(b);
	check(b.get(q) == 1 && b.get(qn) == 0, "latch still set after adding an output");
}

void testRamKeepsContents() {
	Bench b;
	Props ram = widthProps(Kind::Ram, 8);
	ram.addressBits = 2;
	int r = b.add(Kind::Ram, ram);
	int a = b.input(2, "A"), d = b.input(8, "D"), we = b.input(1, "WE"), clk = b.input(1, "CLK");
	int q = b.output(8, "Q");
	b.wire(a, "out", r, "A");
	b.wire(d, "out", r, "D");
	b.wire(we, "out", r, "WE");
	b.wire(clk, "out", r, "CLK");
	b.wire(r, "Q", q, "in");
	b.build();
	b.settle();
	set(b, we, 1);
	for (std::uint32_t i = 0; i < 4; i++) {
		set(b, a, i);
		set(b, d, 0x10 + i);
		b.settle();
		pulse(b, clk);
	}
	set(b, we, 0);
	b.settle();

	b.add(Kind::Led);
	recompile(b);
	bool kept = true;
	for (std::uint32_t i = 0; i < 4; i++) {
		set(b, a, i);
		b.settle();
		kept = kept && b.get(q) == 0x10 + i;
	}
	check(kept, "RAM contents kept after an edit");
}

void testClockPhase() {
	Bench b;
	Props p = defaultProps(Kind::Clock);
	p.period = 8;
	int clk = b.add(Kind::Clock, p);
	int led = b.add(Kind::Led);
	b.wire(clk, "out", led, "in");
	b.build();
	std::string trace;
	for (int t = 0; t < 6; t++) {
		b.sim.advance(1);
		trace += b.read(led).binary();
	}
	b.add(Kind::Led);
	recompile(b);
	for (int t = 6; t < 16; t++) {
		b.sim.advance(1);
		trace += b.read(led).binary();
	}
	check(trace == "0000111100001111", "the clock keeps its phase across an edit: " + trace);
}

void testChipInstances() {
	// Two instances of a chip with a flip-flop inside keep their own state
	Bench b;
	ChipDef def;
	Circuit& c = def.circuit;
	int d = c.add(Kind::InputPin, 0, 0, defaultProps(Kind::InputPin), "D");
	int ck = c.add(Kind::InputPin, 0, 4, defaultProps(Kind::InputPin), "CLK");
	int ff = c.add(Kind::DFlipFlop, 6, 0);
	int out = c.add(Kind::OutputPin, 12, 0, defaultProps(Kind::OutputPin), "Q");
	check(connect(c, b.doc.library, d, "out", ff, "D"), "chip wire D");
	check(connect(c, b.doc.library, ck, "out", ff, "CLK"), "chip wire CLK");
	check(connect(c, b.doc.library, ff, "Q", out, "in"), "chip wire Q");
	b.doc.library.chips["Bit"] = std::move(def);

	Props chip = defaultProps(Kind::Chip);
	chip.chip = "Bit";
	int one = b.add(Kind::Chip, chip), two = b.add(Kind::Chip, chip);
	int data = b.input(1, "D"), clk = b.input(1, "CLK");
	int q1 = b.output(1, "Q1"), q2 = b.output(1, "Q2");
	b.wire(data, "out", one, "D");
	b.wire(clk, "out", one, "CLK");
	b.wire(one, "Q", q1, "in");
	b.wire(two, "Q", q2, "in");
	b.build();
	b.settle();
	set(b, data, 1);
	pulse(b, clk);
	check(b.get(q1) == 1 && b.get(q2) == 0, "only the clocked instance stored 1");

	b.wire(data, "out", two, "D");
	recompile(b);
	check(b.get(q1) == 1 && b.get(q2) == 0, "each instance keeps its own bit after an edit");

	// Removing the first instance leaves the second one's state alone
	b.circuit().remove(one);
	b.circuit().remove(q1);
	b.wire(clk, "out", two, "CLK");
	recompile(b);
	check(b.get(q2) == 0, "the second instance still holds 0");
	pulse(b, clk);
	check(b.get(q2) == 1, "and stores 1 once clocked");
}

} // namespace

int main() {
	testCounterKeepsCount();
	testGateLatchKeepsValue();
	testRamKeepsContents();
	testClockPhase();
	testChipInstances();
	return report("carry_test");
}
