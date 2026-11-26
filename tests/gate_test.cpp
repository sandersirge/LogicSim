// Truth tables of every gate with 1 to 6 inputs, bitwise gates on buses, and X on open inputs.
#include "bench.hpp"

#include <bit>
#include <vector>

using namespace test;

namespace {

int expected(Kind kind, std::uint32_t inputs, int n) {
	std::uint32_t all = (1u << n) - 1;
	int ones = std::popcount(inputs);
	switch (kind) {
	case Kind::And:
		return inputs == all;
	case Kind::Nand:
		return inputs != all;
	case Kind::Or:
		return inputs != 0;
	case Kind::Nor:
		return inputs == 0;
	case Kind::Xor:
		return ones % 2;
	case Kind::Xnor:
		return 1 - ones % 2;
	case Kind::Not:
		return 1 - static_cast<int>(inputs);
	default:
		return static_cast<int>(inputs);
	}
}

void testTruthTable(Kind kind, int n) {
	Bench b;
	bool single = kind == Kind::Not || kind == Kind::Buffer;
	int g = b.gate(kind, single ? 2 : n);
	std::vector<int> ins;
	for (int i = 0; i < n; i++) {
		ins.push_back(b.input());
		b.wire(ins.back(), "out", g, single ? "in" : std::format("in{}", i));
	}
	int o = b.output();
	b.wire(g, "out", o, "in");
	b.build();
	for (std::uint32_t v = 0; v < (1u << n); v++) {
		for (int i = 0; i < n; i++) {
			b.set(ins[static_cast<size_t>(i)], v >> i & 1);
		}
		b.settle();
		int want = expected(kind, v, n);
		check(b.get(o) == static_cast<std::uint32_t>(want),
		      std::format("{} {} inputs, inputs {:0{}b}: got {}", info(kind).name, n, v, n,
		                  b.read(o).binary()));
	}
}

void testBus() {
	// 8-bit AND and XOR, all values of a against a few of b
	for (Kind kind : {Kind::And, Kind::Xor, Kind::Nor}) {
		Bench b;
		int g = b.gate(kind, 2, 8);
		int x = b.input(8), y = b.input(8), o = b.output(8);
		b.wire(x, "out", g, "in0");
		b.wire(y, "out", g, "in1");
		b.wire(g, "out", o, "in");
		b.build();
		for (std::uint32_t v = 0; v < 256; v++) {
			std::uint32_t w = (v * 37 + 11) & 0xFF;
			b.set(x, v);
			b.set(y, w);
			b.settle();
			std::uint32_t want = kind == Kind::And   ? (v & w)
			                     : kind == Kind::Xor ? (v ^ w)
			                                         : (~(v | w) & 0xFF);
			check(b.get(o) == want, std::format("8-bit {} {} {}", info(kind).name, v, w));
		}
	}
}

void testOpenInputs() {
	// An open input reads X; a known 0 on an AND (or 1 on an OR) still decides the output
	Bench b;
	int andGate = b.gate(Kind::And), orGate = b.gate(Kind::Or), xorGate = b.gate(Kind::Xor);
	int s = b.input();
	int oa = b.output(), oo = b.output(), ox = b.output();
	b.wire(s, "out", andGate, "in0");
	b.wire(s, "out", orGate, "in0");
	b.wire(s, "out", xorGate, "in0");
	b.wire(andGate, "out", oa, "in");
	b.wire(orGate, "out", oo, "in");
	b.wire(xorGate, "out", ox, "in");
	b.build();
	b.set(s, 0);
	b.settle();
	check(b.read(oa) == Value::of(0, 1), "AND(0, X) = 0");
	check(b.read(oo).isX(), "OR(0, X) = X");
	check(b.read(ox).isX(), "XOR(0, X) = X");
	b.set(s, 1);
	b.settle();
	check(b.read(oa).isX(), "AND(1, X) = X");
	check(b.read(oo) == Value::of(1, 1), "OR(1, X) = 1");
}

} // namespace

int main() {
	for (Kind kind : {Kind::And, Kind::Or, Kind::Nand, Kind::Nor, Kind::Xor, Kind::Xnor}) {
		for (int n = 2; n <= 6; n++) {
			testTruthTable(kind, n);
		}
	}
	testTruthTable(Kind::Not, 1);
	testTruthTable(Kind::Buffer, 1);
	testBus();
	testOpenInputs();
	return report("gate_test");
}
