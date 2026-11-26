// Adders built as nested chips, checked exhaustively: half adder, full adder, 4-bit ripple adder.
#include "bench.hpp"
#include "chips.hpp"

using namespace test;

namespace {

int instance(Bench& b, const std::string& name) {
	Props p = defaultProps(Kind::Chip);
	p.chip = name;
	return b.add(Kind::Chip, p);
}

void testHalfAdder() {
	Bench b;
	addAdders(b.doc.library);
	int h = instance(b, "Half adder");
	int x = b.input(), y = b.input(), s = b.output(), c = b.output();
	b.wire(x, "out", h, "A");
	b.wire(y, "out", h, "B");
	b.wire(h, "S", s, "in");
	b.wire(h, "C", c, "in");
	b.build();
	for (std::uint32_t v = 0; v < 4; v++) {
		b.set(x, v & 1);
		b.set(y, v >> 1);
		b.settle();
		std::uint32_t sum = (v & 1) + (v >> 1);
		check(b.get(s) == (sum & 1) && b.get(c) == sum >> 1, std::format("half adder {}", v));
	}
}

void testFullAdder() {
	Bench b;
	addAdders(b.doc.library);
	int f = instance(b, "Full adder");
	int x = b.input(), y = b.input(), z = b.input(), s = b.output(), c = b.output();
	b.wire(x, "out", f, "A");
	b.wire(y, "out", f, "B");
	b.wire(z, "out", f, "Cin");
	b.wire(f, "S", s, "in");
	b.wire(f, "Cout", c, "in");
	b.build();
	for (std::uint32_t v = 0; v < 8; v++) {
		b.set(x, v & 1);
		b.set(y, v >> 1 & 1);
		b.set(z, v >> 2);
		b.settle();
		std::uint32_t sum = (v & 1) + (v >> 1 & 1) + (v >> 2);
		check(b.get(s) == (sum & 1) && b.get(c) == sum >> 1, std::format("full adder {}", v));
	}
}

void testAdder4() {
	Bench b;
	addAdders(b.doc.library);
	int f = instance(b, "4-bit adder");
	int x = b.input(4), y = b.input(4), z = b.input(), s = b.output(4), c = b.output();
	b.wire(x, "out", f, "A");
	b.wire(y, "out", f, "B");
	b.wire(z, "out", f, "Cin");
	b.wire(f, "S", s, "in");
	b.wire(f, "Cout", c, "in");
	b.build();
	check(b.compiled.netlist.prims.size() > 4 * 5, "the chips were flattened into gates");
	for (std::uint32_t v = 0; v < 16; v++) {
		for (std::uint32_t w = 0; w < 16; w++) {
			for (std::uint32_t carry = 0; carry < 2; carry++) {
				b.set(x, v);
				b.set(y, w);
				b.set(z, carry);
				check(b.settle() == SettleResult::Stable, "adder settles");
				std::uint32_t sum = v + w + carry;
				check(b.get(s) == (sum & 15) && b.get(c) == sum >> 4,
				      std::format("{} + {} + {} = {} carry {}", v, w, carry, b.read(s).decimal(),
				                  b.read(c).decimal()));
			}
		}
	}
}

void testRippleDelay() {
	// 15 + 1: the carry ripples through all four full adders, so the result takes longer than 1 + 1
	Bench b;
	addAdders(b.doc.library);
	int f = instance(b, "4-bit adder");
	int x = b.input(4), y = b.input(4), z = b.input(), s = b.output(4), c = b.output();
	b.wire(x, "out", f, "A");
	b.wire(y, "out", f, "B");
	b.wire(z, "out", f, "Cin");
	b.wire(f, "S", s, "in");
	b.wire(f, "Cout", c, "in");
	b.build();
	b.set(z, 0);
	b.set(y, 1);
	b.set(x, 0);
	b.settle();
	Tick start = b.sim.now();
	b.set(x, 1);
	b.settle();
	Tick shortTime = b.sim.now() - start;
	b.set(x, 0);
	b.settle();
	start = b.sim.now();
	b.set(x, 15);
	b.settle();
	Tick longTime = b.sim.now() - start;
	check(b.get(s) == 0 && b.get(c) == 1, "15 + 1 = 16");
	check(longTime > shortTime, std::format("ripple carry is slower ({} vs {} ticks)", longTime, shortTime));
}

} // namespace

int main() {
	testHalfAdder();
	testFullAdder();
	testAdder4();
	testRippleDelay();
	return report("adder_test");
}
