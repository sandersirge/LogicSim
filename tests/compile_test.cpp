// The netlist compiler: errors, tunnels, splitters and mergers, optional pins, chip problems.
#include "bench.hpp"
#include "chips.hpp"

#include <algorithm>

using namespace test;

namespace {

bool hasError(const Compiled& c, int component) {
	return std::ranges::any_of(c.diagnostics,
	                           [&](const Diagnostic& d) { return d.error && d.component == component; });
}

void testWidthMismatch() {
	Bench b;
	int x = b.input(4), g = b.gate(Kind::And), o = b.output();
	b.wire(x, "out", g, "in0");
	b.wire(g, "out", o, "in");
	check(!b.build(true), "a 4-bit input on a 1-bit gate is an error");
	check(hasError(b.compiled, x) || hasError(b.compiled, g), "the error names one of the two");
	b.settle();
	check(b.read(o).isX(), "the gate still runs and reads X");
}

void testTwoDrivers() {
	Bench b;
	int x = b.input(), y = b.input(), o = b.output();
	b.wire(x, "out", o, "in");
	b.wire(y, "out", o, "in");
	check(!b.build(true), "two outputs on one wire is an error");
}

void testTunnels() {
	Bench b;
	Props t = defaultProps(Kind::Tunnel);
	t.text = "bus";
	t.width = 8;
	int x = b.input(8), t1 = b.add(Kind::Tunnel, t), t2 = b.add(Kind::Tunnel, t), o = b.output(8);
	t.text = "other";
	int t3 = b.add(Kind::Tunnel, t), o2 = b.output(8);
	b.wire(x, "out", t1, "io");
	b.wire(t2, "io", o, "in");
	b.wire(t3, "io", o2, "in");
	b.build();
	b.set(x, 0xA5);
	b.settle();
	check(b.get(o) == 0xA5, "tunnels with the same name are connected");
	check(b.read(o2).isX(), "a tunnel with another name is not");
}

void testSplitMerge() {
	Bench b;
	Props p = defaultProps(Kind::Splitter);
	p.width = 8;
	p.parts = {3, 1, 4};
	int x = b.input(8), sp = b.add(Kind::Splitter, p), m = b.add(Kind::Merger, p), o = b.output(8);
	int low = b.output(3), mid = b.output(1), high = b.output(4);
	b.wire(x, "out", sp, "in");
	b.wire(sp, "0-2", m, "0-2");
	b.wire(sp, "3", m, "3");
	b.wire(sp, "4-7", m, "4-7");
	b.wire(sp, "0-2", low, "in");
	b.wire(sp, "3", mid, "in");
	b.wire(sp, "4-7", high, "in");
	b.wire(m, "out", o, "in");
	b.build();
	for (std::uint32_t v : {0x00u, 0xFFu, 0x5Au, 0xC3u}) {
		b.set(x, v);
		b.settle();
		check(b.get(o) == v, std::format("split + merge round trip {:#x}", v));
		check(b.get(low) == (v & 7) && b.get(mid) == (v >> 3 & 1) && b.get(high) == v >> 4,
		      std::format("parts of {:#x}", v));
	}
}

void testOptionalPins() {
	// A register with only D and CLK connected: EN defaults to 1 and RST to 0
	Bench b;
	Props p = defaultProps(Kind::Register);
	p.width = 4;
	int r = b.add(Kind::Register, p), d = b.input(4), clk = b.input(), o = b.output(4);
	b.wire(d, "out", r, "D");
	b.wire(clk, "out", r, "CLK");
	b.wire(r, "Q", o, "in");
	b.build();
	b.set(d, 9);
	b.settle();
	check(b.get(o) == 0, "register starts at 0");
	b.set(clk, 1);
	b.settle();
	check(b.get(o) == 9, "unconnected EN means enabled");
}

void testChipProblems() {
	Bench b;
	Props p = defaultProps(Kind::Chip);
	p.chip = "Missing";
	int missing = b.add(Kind::Chip, p);
	check(!b.build(true) && hasError(b.compiled, missing), "unknown chip is an error");

	// A chip that contains itself
	Bench r;
	ChipDef loop;
	Props self = defaultProps(Kind::Chip);
	self.chip = "Loop";
	loop.circuit.add(Kind::Chip, 0, 0, self);
	r.doc.library.chips["Loop"] = loop;
	check(r.doc.library.uses("Loop", "Loop"), "library sees the chip uses itself");
	int top = r.add(Kind::Chip, self);
	check(!r.build(true) && hasError(r.compiled, top), "recursive chip is an error, not a hang");
}

void testPinNets() {
	// Every top-level pin gets a net, and chip pins are joined to the parts inside
	Bench b;
	addAdders(b.doc.library);
	Props p = defaultProps(Kind::Chip);
	p.chip = "Half adder";
	int h = b.add(Kind::Chip, p);
	int x = b.input();
	b.wire(x, "out", h, "A");
	b.build();
	check(b.compiled.net(h, 0) == b.compiled.net(x, 0), "wired pins share a net");
	check(b.compiled.pinNets.at(h).size() == 4, "a half adder has 4 pins");
	ComponentSpec spec = specOf(*b.doc.main.find(h), b.doc.library);
	check(spec.pins[0].name == "A" && spec.pins[1].name == "B" && spec.pins[2].name == "S" &&
	          spec.pins[3].name == "C" && spec.pins[2].output,
	      "chip pins in order: inputs then outputs");
}

} // namespace

int main() {
	testWidthMismatch();
	testTwoDrivers();
	testTunnels();
	testSplitMerge();
	testOptionalPins();
	testChipProblems();
	testPinNets();
	return report("compile_test");
}
