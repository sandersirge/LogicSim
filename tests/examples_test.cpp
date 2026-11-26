// Every example circuit loads, compiles without errors and settles, and each one does what it is
// for: the shift register shifts, the ALU is right for every input (exhaustively), the latches
// latch, the ring oscillator oscillates, and so on.
#include "bench.hpp"
#include "core/json_io.hpp"

#include <filesystem>

using namespace test;

namespace {

int byLabel(const Circuit& c, std::string_view label) {
	for (const Component& component : c.components) {
		if (component.label == label) {
			return component.id;
		}
	}
	check(false, std::format("a component labelled {}", label));
	return -1;
}

void testAllExamples() {
	int count = 0;
	for (const auto& entry : std::filesystem::directory_iterator(LOGICSIM_EXAMPLES_DIR)) {
		if (entry.path().extension() != ".json") {
			continue;
		}
		std::string name = entry.path().filename().string();
		LoadResult loaded = loadFile(entry.path());
		check(loaded.ok, name + " loads: " + loaded.error);
		check(loaded.warnings.empty(), name + " loads without warnings");
		Bench b;
		b.doc = loaded.document;
		b.build();
		b.sim.advance(200, 1'000'000);
		count++;
	}
	check(count >= 3, std::format("found the examples ({})", count));
}

void testShiftRegister() {
	LoadResult loaded = loadFile(std::filesystem::path(LOGICSIM_EXAMPLES_DIR) / "shift-register.json");
	if (!check(loaded.ok, "shift-register.json loads")) {
		return;
	}
	Bench b;
	b.doc = loaded.document;
	b.build();
	const Circuit& c = b.doc.main;
	int d = byLabel(c, "D"), si = byLabel(c, "SI"), load = byLabel(c, "LOAD"), clk = byLabel(c, "CLK");
	int q = byLabel(c, "Q");
	// Inputs settle before the clock edge (changing them in the same tick as the edge would be
	// a setup-time violation: the flip-flops would still see the multiplexers' old outputs)
	auto pulse = [&] {
		b.settle();
		b.set(clk, 1);
		b.settle();
		b.set(clk, 0);
		b.settle();
	};
	b.settle();
	check(b.get(q) == 0, "starts at 0");

	b.set(d, 0b1011);
	b.set(load, 1);
	b.settle();
	check(b.get(q) == 0, "nothing happens before the clock");
	pulse();
	check(b.get(q) == 0b1011, std::format("parallel load 1011 (got {})", b.read(q).binary()));

	b.set(load, 0);
	b.set(si, 0);
	pulse();
	check(b.get(q) == 0b0110, std::format("shift in 0: 0110 (got {})", b.read(q).binary()));
	b.set(si, 1);
	pulse();
	check(b.get(q) == 0b1101, std::format("shift in 1: 1101 (got {})", b.read(q).binary()));
	for (int i = 0; i < 4; i++) {
		pulse();
	}
	check(b.get(q) == 0b1111, "four more 1s fill it");
}

// Loads an example into a bench (compiled and settled)
bool loadExample(Bench& b, const char* file) {
	LoadResult loaded = loadFile(std::filesystem::path(LOGICSIM_EXAMPLES_DIR) / file);
	if (!check(loaded.ok, std::format("{} loads", file))) {
		return false;
	}
	b.doc = loaded.document;
	b.build();
	b.settle();
	return true;
}

void pulse(Bench& b, int clk) {
	b.settle();
	b.set(clk, 1);
	b.settle();
	b.set(clk, 0);
	b.settle();
}

void testAlu() {
	Bench b;
	if (!loadExample(b, "alu.json")) {
		return;
	}
	const Circuit& c = b.doc.main;
	int a = byLabel(c, "A"), bb = byLabel(c, "B"), op = byLabel(c, "OP");
	int y = byLabel(c, "Y"), z = byLabel(c, "Z"), carry = byLabel(c, "C"), n = byLabel(c, "N"),
	    v = byLabel(c, "V");
	int wrong = 0;
	for (std::uint32_t o = 0; o < 4; o++) {
		for (std::uint32_t x = 0; x < 16; x++) {
			for (std::uint32_t w = 0; w < 16; w++) {
				b.set(a, x);
				b.set(bb, w);
				b.set(op, o);
				b.settle();
				std::uint32_t operand = o == 1 ? (~w & 15) : w;
				std::uint32_t sum = x + operand + (o == 1 ? 1 : 0);
				std::uint32_t result = o == 0 || o == 1 ? sum & 15 : o == 2 ? (x & w) : (x | w);
				std::uint32_t expectCarry = o < 2 ? sum >> 4 : 0;
				bool sameSign = (x >> 3) == (operand >> 3);
				std::uint32_t expectV = o < 2 && sameSign && (result >> 3) != (x >> 3) ? 1 : 0;
				bool ok = b.get(y) == result && b.get(z) == (result == 0 ? 1u : 0u) &&
				          b.get(carry) == expectCarry && b.get(n) == result >> 3 && b.get(v) == expectV;
				if (!ok && wrong++ < 5) {
					check(false, std::format("ALU op {} on {} and {}: Y {} Z {} C {} N {} V {}", o, x, w,
					                         b.read(y).binary(), b.read(z).binary(), b.read(carry).binary(),
					                         b.read(n).binary(), b.read(v).binary()));
				}
			}
		}
	}
	check(wrong == 0, std::format("the ALU is right for every A, B and OP ({} wrong)", wrong));
}

void testLatches() {
	Bench b;
	if (!loadExample(b, "latches.json")) {
		return;
	}
	const Circuit& c = b.doc.main;
	int d = byLabel(c, "D"), en = byLabel(c, "EN"), q = byLabel(c, "Q");
	b.set(d, 1);
	b.set(en, 1);
	b.settle();
	check(b.get(q) == 1, "gate-level D latch: Q follows D while EN is 1");
	b.set(en, 0);
	b.settle();
	b.set(d, 0);
	b.settle();
	check(b.get(q) == 1, "and holds it while EN is 0");
	b.set(en, 1);
	b.settle();
	check(b.get(q) == 0, "and follows again");

	int d2 = byLabel(c, "D2"), clk = byLabel(c, "CLK"), q2 = byLabel(c, "Q2");
	b.set(d2, 1);
	b.settle();
	pulse(b, clk);
	check(b.get(q2) == 1, "master-slave flip-flop stores 1 on a clock pulse");
	b.set(d2, 0);
	b.settle();
	check(b.get(q2) == 1, "D changing without a clock does nothing");
	b.set(clk, 1);
	b.settle();
	check(b.get(q2) == 0, "the rising edge stores 0");
}

void testRingOscillator() {
	Bench b;
	if (!loadExample(b, "ring-oscillator.json")) {
		return;
	}
	const Circuit& c = b.doc.main;
	int en = byLabel(c, "EN"), out = byLabel(c, "OUT");
	check(b.settle() == SettleResult::Stable, "stable while EN is off");
	b.set(en, 1);
	b.sim.advance(40);
	int net = b.compiled.net(out, 0);
	std::vector<Tick> changes;
	Tick last = b.sim.lastChange(net);
	for (int t = 0; t < 100; t++) {
		b.sim.advance(1);
		if (b.sim.lastChange(net) != last) {
			last = b.sim.lastChange(net);
			changes.push_back(last);
		}
	}
	bool even = changes.size() >= 8;
	for (size_t i = 1; i < changes.size(); i++) {
		even = even && changes[i] - changes[i - 1] == 10;
	}
	check(even, std::format("OUT changes every 10 ticks ({} changes)", changes.size()));
	b.set(en, 0);
	check(b.settle() == SettleResult::Stable, "EN off stops it");
}

void testCounterDisplay() {
	Bench b;
	if (!loadExample(b, "counter-display.json")) {
		return;
	}
	const Circuit& c = b.doc.main;
	int hex = byLabel(c, "hex"), seg = byLabel(c, "display");
	const std::uint32_t patterns[16] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
	                                    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};
	const char* names[] = {"a", "b", "c", "d", "e", "f", "g"};
	std::vector<std::uint32_t> counts;
	bool segmentsMatch = true;
	for (int i = 0; i < 20; i++) {
		b.sim.advance(16, 1'000'000);
		Value count = b.read(hex);
		if (!count.fullyKnown()) {
			continue;
		}
		counts.push_back(count.bits);
		std::uint32_t lit = 0;
		for (int s = 0; s < 7; s++) {
			lit |= b.at(seg, names[s]).isOne() ? 1u << s : 0u;
		}
		segmentsMatch = segmentsMatch && lit == patterns[count.bits];
	}
	bool counting = counts.size() >= 18;
	for (size_t i = 1; i < counts.size(); i++) {
		counting = counting && counts[i] == ((counts[i - 1] + 1) & 15);
	}
	check(counting, std::format("the counter steps by one every clock ({} samples)", counts.size()));
	check(segmentsMatch, "the display shows the counter's digit");
}

void testPlexers() {
	Bench b;
	if (!loadExample(b, "plexers.json")) {
		return;
	}
	const Circuit& c = b.doc.main;
	int sel = byLabel(c, "SEL"), out = byLabel(c, "OUT");
	int d[4], y[4];
	for (int i = 0; i < 4; i++) {
		d[i] = byLabel(c, std::format("D{}", i));
		y[i] = byLabel(c, std::format("Y{}", i));
	}
	bool ok = true;
	for (std::uint32_t s = 0; s < 4; s++) {
		for (std::uint32_t data = 0; data < 16; data++) {
			for (int i = 0; i < 4; i++) {
				b.set(d[i], data >> i & 1);
			}
			b.set(sel, s);
			b.settle();
			ok = ok && b.get(out) == (data >> s & 1);
			for (std::uint32_t i = 0; i < 4; i++) {
				ok = ok && b.get(y[i]) == (i == s ? 1u : 0u);
			}
		}
	}
	check(ok, "the multiplexer passes the selected input and the decoder lights the selected output");
}

void testRegisterFile() {
	Bench b;
	if (!loadExample(b, "register-file.json")) {
		return;
	}
	const Circuit& c = b.doc.main;
	int d = byLabel(c, "D"), wa = byLabel(c, "WA"), we = byLabel(c, "WE"), clk = byLabel(c, "CLK");
	int ra = byLabel(c, "RA"), q = byLabel(c, "Q");
	const std::uint32_t values[4] = {3, 7, 9, 12};
	b.set(we, 1);
	for (std::uint32_t r = 0; r < 4; r++) {
		b.set(d, values[r]);
		b.set(wa, r);
		pulse(b, clk);
	}
	b.set(we, 0);
	b.set(d, 15);
	b.set(wa, 2);
	pulse(b, clk); // not written: WE is off
	bool ok = true;
	for (std::uint32_t r = 0; r < 4; r++) {
		b.set(ra, r);
		b.settle();
		ok = ok && b.get(q) == values[r];
	}
	check(ok, "each register keeps what was written to it, and nothing is written while WE is off");
}

void testRam() {
	Bench b;
	if (!loadExample(b, "ram.json")) {
		return;
	}
	const Circuit& c = b.doc.main;
	int a = byLabel(c, "A"), d = byLabel(c, "D"), we = byLabel(c, "WE"), clk = byLabel(c, "CLK");
	int q = byLabel(c, "Q");
	b.set(we, 1);
	for (std::uint32_t i = 0; i < 16; i++) {
		b.set(a, i);
		b.set(d, i * 17);
		pulse(b, clk);
	}
	b.set(we, 0);
	bool ok = true;
	for (std::uint32_t i = 0; i < 16; i++) {
		b.set(a, 15 - i);
		b.settle();
		ok = ok && b.get(q) == (15 - i) * 17;
	}
	check(ok, "the RAM reads back every word written to it");
}

} // namespace

int main() {
	testAllExamples();
	testShiftRegister();
	testAlu();
	testLatches();
	testRingOscillator();
	testCounterDisplay();
	testPlexers();
	testRegisterFile();
	testRam();
	return report("examples_test");
}
