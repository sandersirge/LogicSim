// Flip-flops, latches, registers, counters and memories: edge triggering, async set/reset, and a
// gate-level master-slave D flip-flop (made of NAND chips) that behaves like the built-in one.
#include "bench.hpp"
#include "chips.hpp"

#include <map>
#include <random>
#include <vector>

using namespace test;

namespace {

// A built-in component with its inputs on switches/pins; returns the component id
struct Part {
	Bench& b;
	int id;
	std::map<std::string, int> ins;
	std::map<std::string, int> outs;

	Part(Bench& bench, Kind kind, Props props, std::vector<std::pair<std::string, int>> inputs,
	     std::vector<std::pair<std::string, int>> outputs)
	    : b(bench), id(bench.add(kind, props)) {
		for (auto& [name, width] : inputs) {
			ins[name] = b.input(width);
			b.wire(ins[name], "out", id, name);
		}
		for (auto& [name, width] : outputs) {
			outs[name] = b.output(width);
			b.wire(id, name, outs[name], "in");
		}
	}
	void set(const std::string& pin, std::uint32_t v) { b.set(ins.at(pin), v); }
	std::uint32_t get(const std::string& pin) { return b.get(outs.at(pin)); }
	// A full clock pulse 0 -> 1 -> 0 on the named pin
	void pulse(const std::string& clk = "CLK") {
		set(clk, 1);
		b.settle();
		set(clk, 0);
		b.settle();
	}
};

Props props(Kind kind, int width = 1) {
	Props p = defaultProps(kind);
	p.width = width;
	return p;
}

void testDFlipFlop() {
	Bench b;
	Part ff(b, Kind::DFlipFlop, props(Kind::DFlipFlop), {{"D", 1}, {"CLK", 1}, {"S", 1}, {"R", 1}},
	        {{"Q", 1}, {"Qn", 1}});
	b.build();
	for (auto pin : {"D", "CLK", "S", "R"}) {
		ff.set(pin, 0);
	}
	b.settle();
	check(ff.get("Q") == 0 && ff.get("Qn") == 1, "DFF powers on at 0");
	ff.set("D", 1);
	b.settle();
	check(ff.get("Q") == 0, "D alone does nothing");
	ff.set("CLK", 1);
	b.settle();
	check(ff.get("Q") == 1, "rising edge stores D");
	ff.set("D", 0);
	b.settle();
	check(ff.get("Q") == 1, "D changing while CLK is high does nothing");
	ff.set("CLK", 0);
	b.settle();
	check(ff.get("Q") == 1, "falling edge does nothing");
	ff.pulse();
	check(ff.get("Q") == 0, "next edge stores 0");
	ff.set("S", 1);
	b.settle();
	check(ff.get("Q") == 1, "async set");
	ff.set("S", 0);
	ff.set("R", 1);
	b.settle();
	check(ff.get("Q") == 0, "async reset");
	ff.set("D", 1);
	ff.pulse();
	check(ff.get("Q") == 0, "reset wins over the clock");
}

void testLatches() {
	Bench b;
	Part sr(b, Kind::SrLatch, props(Kind::SrLatch), {{"S", 1}, {"R", 1}}, {{"Q", 1}, {"Qn", 1}});
	Part d(b, Kind::DLatch, props(Kind::DLatch), {{"D", 1}, {"EN", 1}}, {{"Q", 1}});
	b.build();
	sr.set("S", 0);
	sr.set("R", 0);
	d.set("D", 0);
	d.set("EN", 0);
	b.settle();
	sr.set("S", 1);
	b.settle();
	check(sr.get("Q") == 1 && sr.get("Qn") == 0, "SR set");
	sr.set("S", 0);
	b.settle();
	check(sr.get("Q") == 1, "SR hold");
	sr.set("R", 1);
	b.settle();
	check(sr.get("Q") == 0 && sr.get("Qn") == 1, "SR reset");
	sr.set("S", 1);
	b.settle();
	check(sr.get("Q") == 0 && sr.get("Qn") == 0, "S and R both 1: both outputs 0");
	sr.set("S", 0);
	b.settle();
	check(sr.get("Q") == 0 && sr.get("Qn") == 1, "releasing S first leaves it reset");

	d.set("D", 1);
	b.settle();
	check(d.get("Q") == 0, "D latch closed");
	d.set("EN", 1);
	b.settle();
	check(d.get("Q") == 1, "D latch open follows D");
	d.set("D", 0);
	b.settle();
	check(d.get("Q") == 0, "and keeps following");
	d.set("EN", 0);
	d.set("D", 1);
	b.settle();
	check(d.get("Q") == 0, "closed again: holds");
}

void testJkAndT() {
	Bench b;
	Part jk(b, Kind::JkFlipFlop, props(Kind::JkFlipFlop), {{"J", 1}, {"K", 1}, {"CLK", 1}}, {{"Q", 1}});
	Part t(b, Kind::TFlipFlop, props(Kind::TFlipFlop), {{"T", 1}, {"CLK", 1}}, {{"Q", 1}});
	b.build();
	jk.set("CLK", 0);
	t.set("CLK", 0);
	jk.set("J", 1);
	jk.set("K", 0);
	b.settle();
	jk.pulse();
	check(jk.get("Q") == 1, "JK: J sets");
	jk.set("J", 0);
	jk.set("K", 1);
	jk.pulse();
	check(jk.get("Q") == 0, "JK: K resets");
	jk.set("K", 0);
	jk.pulse();
	check(jk.get("Q") == 0, "JK: 00 holds");
	jk.set("J", 1);
	jk.set("K", 1);
	std::string toggles;
	for (int i = 0; i < 4; i++) {
		jk.pulse();
		toggles += std::to_string(jk.get("Q"));
	}
	check(toggles == "1010", "JK: 11 toggles " + toggles);

	t.set("T", 0);
	t.pulse();
	check(t.get("Q") == 0, "T=0 holds");
	t.set("T", 1);
	t.pulse();
	check(t.get("Q") == 1, "T=1 toggles");
	t.pulse();
	check(t.get("Q") == 0, "and back");
}

void testRippleCounter() {
	// Four T flip-flops (T=1), each clocked by the previous Q-bar: a 4-bit up counter
	Bench b;
	int one = b.add(Kind::Constant, [] {
		Props p = defaultProps(Kind::Constant);
		p.value = 1;
		return p;
	}());
	int clk = b.input();
	std::vector<int> ffs, leds;
	for (int i = 0; i < 4; i++) {
		ffs.push_back(b.add(Kind::TFlipFlop));
		b.wire(one, "out", ffs[i], "T");
		b.wire(i == 0 ? clk : ffs[i - 1], i == 0 ? "out" : "Qn", ffs[i], "CLK");
		leds.push_back(b.add(Kind::Led));
		b.wire(ffs[i], "Q", leds[i], "in");
	}
	b.build();
	b.set(clk, 0);
	b.settle();
	bool ok = true;
	for (std::uint32_t n = 1; n <= 20; n++) {
		b.set(clk, 1);
		b.settle();
		b.set(clk, 0);
		b.settle();
		std::uint32_t value = 0;
		for (int i = 0; i < 4; i++) {
			value |= b.get(leds[i]) << i;
		}
		ok = ok && value == n % 16;
	}
	check(ok, "ripple counter counts 1..20 mod 16");
}

void testRegisterAndCounter() {
	Bench b;
	Part reg(b, Kind::Register, props(Kind::Register, 8), {{"D", 8}, {"EN", 1}, {"CLK", 1}, {"RST", 1}},
	         {{"Q", 8}});
	Part cnt(b, Kind::Counter, props(Kind::Counter, 3),
	         {{"D", 3}, {"LD", 1}, {"EN", 1}, {"CLK", 1}, {"RST", 1}}, {{"Q", 3}, {"CO", 1}});
	b.build();
	for (auto pin : {"EN", "CLK", "RST"}) {
		reg.set(pin, 0);
	}
	for (auto pin : {"D", "LD", "CLK", "RST"}) {
		cnt.set(pin, 0);
	}
	cnt.set("EN", 1);
	reg.set("D", 0x5A);
	b.settle();
	reg.pulse();
	check(reg.get("Q") == 0, "register with EN=0 holds");
	reg.set("EN", 1);
	reg.pulse();
	check(reg.get("Q") == 0x5A, "register stores D");
	reg.set("RST", 1);
	b.settle();
	check(reg.get("Q") == 0, "async reset clears");

	std::string counts, carries;
	for (int i = 0; i < 9; i++) {
		counts += std::to_string(cnt.get("Q"));
		carries += std::to_string(cnt.get("CO"));
		cnt.pulse();
	}
	check(counts == "012345670", "3-bit counter wraps: " + counts);
	check(carries == "000000010", "carry out at 7: " + carries);
	cnt.set("EN", 0);
	cnt.pulse();
	check(cnt.get("Q") == 1, "EN=0 holds");
	cnt.set("D", 6);
	cnt.set("LD", 1);
	cnt.pulse();
	check(cnt.get("Q") == 6, "LD loads D");
	cnt.set("RST", 1);
	b.settle();
	check(cnt.get("Q") == 0, "counter reset");
}

void testMemories() {
	Bench b;
	Props ram = props(Kind::Ram, 8);
	ram.addressBits = 4;
	Part r(b, Kind::Ram, ram, {{"A", 4}, {"D", 8}, {"WE", 1}, {"CLK", 1}}, {{"Q", 8}});
	Props rom = props(Kind::Rom, 8);
	rom.addressBits = 3;
	rom.data = {10, 20, 30, 40, 50};
	Part o(b, Kind::Rom, rom, {{"A", 3}}, {{"Q", 8}});
	b.build();
	r.set("CLK", 0);
	r.set("WE", 1);
	for (std::uint32_t a = 0; a < 16; a++) {
		r.set("A", a);
		r.set("D", a * 7 + 1);
		b.settle();
		r.pulse();
	}
	r.set("WE", 0);
	bool ok = true;
	for (std::uint32_t a = 0; a < 16; a++) {
		r.set("A", a);
		r.set("D", 0xEE);
		b.settle();
		r.pulse();
		ok = ok && r.get("Q") == a * 7 + 1;
	}
	check(ok, "RAM reads back what was written, WE=0 does not write");

	std::string words;
	for (std::uint32_t a = 0; a < 8; a++) {
		o.set("A", a);
		b.settle();
		words += std::to_string(o.get("Q")) + " ";
	}
	check(words == "10 20 30 40 50 0 0 0 ", "ROM contents: " + words);
}

// Gated D latch from NAND gates, as a chip with inputs D, EN and outputs Q, Qn
void addNandLatch(Library& lib) {
	ChipDef def;
	Circuit& c = def.circuit;
	int d = pin(c, Kind::InputPin, 0, "D"), en = pin(c, Kind::InputPin, 2, "EN");
	int q = pin(c, Kind::OutputPin, 0, "Q"), qn = pin(c, Kind::OutputPin, 2, "Qn");
	int inv = c.add(Kind::Not, 4, 4);
	int s = c.add(Kind::Nand, 8, 0), r = c.add(Kind::Nand, 8, 4);
	int top = c.add(Kind::Nand, 14, 0), bottom = c.add(Kind::Nand, 14, 4);
	link(c, lib, d, "out", s, "in0");
	link(c, lib, en, "out", s, "in1");
	link(c, lib, d, "out", inv, "in");
	link(c, lib, inv, "out", r, "in0");
	link(c, lib, en, "out", r, "in1");
	link(c, lib, s, "out", top, "in0");
	link(c, lib, bottom, "out", top, "in1");
	link(c, lib, r, "out", bottom, "in0");
	link(c, lib, top, "out", bottom, "in1");
	link(c, lib, top, "out", q, "in");
	link(c, lib, bottom, "out", qn, "in");
	lib.chips["NAND latch"] = std::move(def);
}

// Master-slave D flip-flop: master open while CLK is 0, slave open while CLK is 1
void addMasterSlave(Library& lib) {
	ChipDef def;
	Circuit& c = def.circuit;
	int d = pin(c, Kind::InputPin, 0, "D"), clk = pin(c, Kind::InputPin, 2, "CLK");
	int q = pin(c, Kind::OutputPin, 0, "Q");
	int inv = c.add(Kind::Not, 4, 4);
	int master = chip(c, "NAND latch", 8, 0), slave = chip(c, "NAND latch", 16, 0);
	link(c, lib, clk, "out", inv, "in");
	link(c, lib, d, "out", master, "D");
	link(c, lib, inv, "out", master, "EN");
	link(c, lib, master, "Q", slave, "D");
	link(c, lib, clk, "out", slave, "EN");
	link(c, lib, slave, "Q", q, "in");
	lib.chips["Master-slave DFF"] = std::move(def);
}

void testGateLevelFlipFlop() {
	Bench b;
	addNandLatch(b.doc.library);
	addMasterSlave(b.doc.library);
	Props chipProps = defaultProps(Kind::Chip);
	chipProps.chip = "Master-slave DFF";
	int gates = b.add(Kind::Chip, chipProps);
	int builtin = b.add(Kind::DFlipFlop);
	int d = b.input(), clk = b.input(), q1 = b.output(), q2 = b.output();
	b.wire(d, "out", gates, "D");
	b.wire(clk, "out", gates, "CLK");
	b.wire(d, "out", builtin, "D");
	b.wire(clk, "out", builtin, "CLK");
	b.wire(gates, "Q", q1, "in");
	b.wire(builtin, "Q", q2, "in");
	b.build();
	b.set(d, 0);
	b.set(clk, 0);
	check(b.settle() == SettleResult::Stable, "gate-level flip-flop settles");
	check(b.read(q1).isX(), "gate-level flip-flop starts unknown");
	b.set(clk, 1);
	b.settle();
	check(b.get(q1) == 0, "first edge makes it known");

	std::mt19937 rng(7);
	bool same = true;
	for (int i = 0; i < 300; i++) {
		if (rng() % 2) {
			b.set(d, rng() % 2);
		} else {
			b.set(clk, rng() % 2);
		}
		check(b.settle() == SettleResult::Stable, "settles");
		same = same && b.get(q1) == b.get(q2);
	}
	check(same, "the NAND master-slave flip-flop matches the built-in D flip-flop");
}

} // namespace

int main() {
	testDFlipFlop();
	testLatches();
	testJkAndT();
	testRippleCounter();
	testRegisterAndCounter();
	testMemories();
	testGateLevelFlipFlop();
	return report("seq_test");
}
