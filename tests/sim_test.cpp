// The event-driven engine: gate delays, hazards, no zero-width glitches, feedback loops (an SR latch
// made of NOR gates, a ring oscillator) and clocks.
#include "bench.hpp"

using namespace test;

namespace {

int buffer(Bench& b, int delay) {
	Props p = defaultProps(Kind::Buffer);
	p.delay = delay;
	return b.add(Kind::Buffer, p);
}

void testDelays() {
	Bench b;
	int s = b.input(), b1 = buffer(b, 1), b2 = buffer(b, 2), b3 = buffer(b, 3), o = b.output();
	b.wire(s, "out", b1, "in");
	b.wire(b1, "out", b2, "in");
	b.wire(b2, "out", b3, "in");
	b.wire(b3, "out", o, "in");
	b.build();
	check(b.settle() == SettleResult::Stable, "chain settles");
	check(b.get(o) == 0, "chain starts at 0");

	Tick start = b.sim.now();
	b.set(s, 1);
	for (int t = 0; t < 6; t++) {
		b.sim.advance(1);
		check(b.get(o) == 0, std::format("output still 0 after {} ticks", t + 1));
	}
	b.sim.advance(1);
	check(b.get(o) == 1, "output 1 after 1 + 2 + 3 ticks");
	int net = b.compiled.net(o, 0);
	check(b.sim.lastChange(net) == start + 6,
	      std::format("changed at start + 6 (got {})", b.sim.lastChange(net) - start));
}

void testHazard() {
	// AND(a, NOT a): when a rises, the slow NOT lets a short 1 through (a static hazard)
	Bench b;
	int a = b.input();
	Props notProps = defaultProps(Kind::Not);
	notProps.delay = 2;
	int n = b.add(Kind::Not, notProps);
	int g = b.gate(Kind::And);
	int o = b.output();
	b.wire(a, "out", n, "in");
	b.wire(a, "out", g, "in0");
	b.wire(n, "out", g, "in1");
	b.wire(g, "out", o, "in");
	b.build();
	b.settle();
	b.set(a, 1);
	std::string trace;
	for (int t = 0; t < 6; t++) {
		b.sim.advance(1);
		trace += b.read(o).binary();
	}
	// Ticks 0..5 after the change: AND sees 1,1 during ticks 0-1 and outputs it one tick later
	check(trace == "011000", "hazard pulse " + trace);
}

void testNoZeroWidthGlitch() {
	// Both XOR inputs change in the same tick: the output must not blip
	Bench b;
	int a = b.input(), b1 = buffer(b, 1), b2 = buffer(b, 1), g = b.gate(Kind::Xor), o = b.output();
	b.wire(a, "out", b1, "in");
	b.wire(a, "out", b2, "in");
	b.wire(b1, "out", g, "in0");
	b.wire(b2, "out", g, "in1");
	b.wire(g, "out", o, "in");
	b.build();
	b.settle();
	int net = b.compiled.net(o, 0);
	Tick before = b.sim.lastChange(net);
	for (int i = 0; i < 4; i++) {
		b.set(a, static_cast<std::uint32_t>(i % 2 == 0));
		b.settle();
		check(b.get(o) == 0, "XOR of equal inputs is 0");
	}
	check(b.sim.lastChange(net) == before, "the XOR output never changed");
}

void testReplacedThenLater() {
	// One switch reaches an XOR through buffers of 1, 1 and 2 ticks. When it flips, the XOR is
	// scheduled twice for the same tick (the second replaces the first), then once more for the next
	// tick before the first two come up. The replaced event must not show (no glitch), and settle()
	// must wait for the last one.
	Bench b;
	int s = b.input(), x = buffer(b, 1), y = buffer(b, 1), z = buffer(b, 2);
	int g = b.gate(Kind::Xor, 3), o = b.output();
	b.wire(s, "out", x, "in");
	b.wire(s, "out", y, "in");
	b.wire(s, "out", z, "in");
	b.wire(x, "out", g, "in0");
	b.wire(y, "out", g, "in1");
	b.wire(z, "out", g, "in2");
	b.wire(g, "out", o, "in");
	b.build();
	b.settle();
	check(b.get(o) == 0, "XOR of three 0s");
	int net = b.compiled.net(o, 0);
	b.set(s, 1);
	std::string trace;
	std::vector<Tick> changes;
	Tick start = b.sim.now(), last = b.sim.lastChange(net);
	for (int t = 0; t < 5; t++) {
		b.sim.advance(1);
		trace += b.read(o).binary();
		if (b.sim.lastChange(net) != last) {
			last = b.sim.lastChange(net);
			changes.push_back(last - start);
		}
	}
	check(trace == "00011", "XOR of 1, 1, 1 shows once all three arrive: " + trace);
	check(changes.size() == 1, std::format("the output changed once ({} times)", changes.size()));

	b.set(s, 0);
	check(b.settle() == SettleResult::Stable && b.get(o) == 0, "settle waits for the last event");
	check(b.sim.pendingEvents() == 0 && !b.sim.stepEvent(), "and nothing is left over");
}

void testNorLatch() {
	Bench b;
	int s = b.input(1, "S"), r = b.input(1, "R");
	int top = b.gate(Kind::Nor), bottom = b.gate(Kind::Nor);
	int q = b.output(1, "Q"), qn = b.output(1, "Qn");
	b.wire(r, "out", top, "in0");
	b.wire(bottom, "out", top, "in1");
	b.wire(s, "out", bottom, "in0");
	b.wire(top, "out", bottom, "in1");
	b.wire(top, "out", q, "in");
	b.wire(bottom, "out", qn, "in");
	b.build();

	check(b.settle() == SettleResult::Stable, "latch settles at power-on");
	check(b.read(q).isX() && b.read(qn).isX(), "latch starts unknown");

	b.set(s, 1);
	check(b.settle() == SettleResult::Stable, "set settles");
	check(b.get(q) == 1 && b.get(qn) == 0, "set: Q=1");
	b.set(s, 0);
	b.settle();
	check(b.get(q) == 1 && b.get(qn) == 0, "hold after set");
	b.set(r, 1);
	b.settle();
	check(b.get(q) == 0 && b.get(qn) == 1, "reset: Q=0");
	b.set(r, 0);
	b.settle();
	check(b.get(q) == 0 && b.get(qn) == 1, "hold after reset");
	b.set(s, 1);
	b.settle();
	check(b.get(q) == 1, "set again");
}

void testRingOscillator() {
	// NAND(enable, feedback) and two NOTs: three inversions of one tick each, period 6 ticks
	Bench b;
	int en = b.input();
	int nand = b.gate(Kind::Nand), n1 = b.add(Kind::Not), n2 = b.add(Kind::Not);
	int o = b.output();
	b.wire(en, "out", nand, "in0");
	b.wire(n2, "out", nand, "in1");
	b.wire(nand, "out", n1, "in");
	b.wire(n1, "out", n2, "in");
	b.wire(n2, "out", o, "in");
	b.build();
	check(b.settle() == SettleResult::Stable, "disabled oscillator is stable");
	check(b.get(o) == 1, "disabled: output 1");

	b.set(en, 1);
	check(b.sim.settle(10'000) == SettleResult::Oscillating, "enabled oscillator never settles");

	int net = b.compiled.net(o, 0);
	std::vector<Tick> changes;
	Tick last = b.sim.lastChange(net);
	for (int t = 0; t < 40; t++) {
		b.sim.advance(1);
		if (b.sim.lastChange(net) != last) {
			last = b.sim.lastChange(net);
			changes.push_back(last);
		}
	}
	bool even = changes.size() >= 10;
	for (size_t i = 1; i < changes.size(); i++) {
		even = even && changes[i] - changes[i - 1] == 3;
	}
	check(even, std::format("output toggles every 3 ticks ({} changes)", changes.size()));
	b.set(en, 0);
	check(b.settle() == SettleResult::Stable, "disabling stops it");
}

void testClock() {
	Bench b;
	Props p = defaultProps(Kind::Clock);
	p.period = 8;
	int clk = b.add(Kind::Clock, p);
	int o = b.add(Kind::Led);
	b.wire(clk, "out", o, "in");
	b.build();
	std::string trace;
	for (int t = 0; t < 16; t++) {
		b.sim.advance(1);
		trace += b.read(o).binary();
	}
	check(trace == "0000111100001111", "clock trace " + trace);
	check(b.settle() == SettleResult::Stable, "a circuit with only a clock counts as settled");
}

void testStepEvent() {
	Bench b;
	int s = b.input(), n = b.add(Kind::Not), o = b.output();
	b.wire(s, "out", n, "in");
	b.wire(n, "out", o, "in");
	b.build();
	int events = 0;
	while (b.sim.stepEvent()) {
		events++;
	}
	check(events == 2, std::format("power-on: switch event and NOT event ({})", events));
	check(b.get(o) == 1, "NOT 0 = 1");
	check(!b.sim.stepEvent(), "nothing pending");
}

} // namespace

int main() {
	testDelays();
	testHazard();
	testNoZeroWidthGlitch();
	testReplacedThenLater();
	testNorLatch();
	testRingOscillator();
	testClock();
	testStepEvent();
	return report("sim_test");
}
