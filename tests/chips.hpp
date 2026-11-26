#pragma once

// Adder chips built from gates, as a user would: half adder -> full adder -> 4-bit ripple adder.
#include "check.hpp"
#include "core/circuit.hpp"

#include <format>
#include <string>

namespace test {

using namespace logic;

inline int pin(Circuit& c, Kind kind, int y, std::string label, int width = 1) {
	Props p = defaultProps(kind);
	p.width = width;
	return c.add(kind, kind == Kind::InputPin ? 0 : 30, y, p, std::move(label));
}

inline void link(Circuit& c, const Library& lib, int a, std::string_view pa, int b, std::string_view pb) {
	check(connect(c, lib, a, pa, b, pb), std::format("chip wire {}.{} -> {}.{}", a, pa, b, pb));
}

inline int chip(Circuit& c, const std::string& name, int x = 10, int y = 0) {
	Props p = defaultProps(Kind::Chip);
	p.chip = name;
	return c.add(Kind::Chip, x, y, p);
}

// Half adder: S = A xor B, C = A and B
inline void addHalfAdder(Library& lib) {
	ChipDef def;
	def.color = "#3a6ea5";
	Circuit& c = def.circuit;
	int a = pin(c, Kind::InputPin, 0, "A"), b = pin(c, Kind::InputPin, 2, "B");
	int s = pin(c, Kind::OutputPin, 0, "S"), carry = pin(c, Kind::OutputPin, 2, "C");
	int x = c.add(Kind::Xor, 10, 0), n = c.add(Kind::And, 10, 4);
	link(c, lib, a, "out", x, "in0");
	link(c, lib, b, "out", x, "in1");
	link(c, lib, a, "out", n, "in0");
	link(c, lib, b, "out", n, "in1");
	link(c, lib, x, "out", s, "in");
	link(c, lib, n, "out", carry, "in");
	lib.chips["Half adder"] = std::move(def);
}

// Full adder from two half adders and an OR
inline void addFullAdder(Library& lib) {
	ChipDef def;
	Circuit& c = def.circuit;
	int a = pin(c, Kind::InputPin, 0, "A"), b = pin(c, Kind::InputPin, 2, "B"),
	    cin = pin(c, Kind::InputPin, 4, "Cin");
	int s = pin(c, Kind::OutputPin, 0, "S"), cout = pin(c, Kind::OutputPin, 2, "Cout");
	int h1 = chip(c, "Half adder", 6, 0), h2 = chip(c, "Half adder", 14, 2);
	int o = c.add(Kind::Or, 22, 4);
	link(c, lib, a, "out", h1, "A");
	link(c, lib, b, "out", h1, "B");
	link(c, lib, h1, "S", h2, "A");
	link(c, lib, cin, "out", h2, "B");
	link(c, lib, h2, "S", s, "in");
	link(c, lib, h1, "C", o, "in0");
	link(c, lib, h2, "C", o, "in1");
	link(c, lib, o, "out", cout, "in");
	lib.chips["Full adder"] = std::move(def);
}

// 4-bit ripple-carry adder: buses split into bits, four full adders, merged back
inline void addAdder4(Library& lib) {
	ChipDef def;
	Circuit& c = def.circuit;
	int a = pin(c, Kind::InputPin, 0, "A", 4), b = pin(c, Kind::InputPin, 2, "B", 4);
	int cin = pin(c, Kind::InputPin, 4, "Cin");
	int s = pin(c, Kind::OutputPin, 0, "S", 4), cout = pin(c, Kind::OutputPin, 2, "Cout");
	Props split = defaultProps(Kind::Splitter);
	split.width = 4;
	int sa = c.add(Kind::Splitter, 4, 0, split), sb = c.add(Kind::Splitter, 4, 6, split);
	int m = c.add(Kind::Merger, 26, 0, split);
	int carry = cin;
	std::string carryPin = "out";
	for (int i = 0; i < 4; i++) {
		int fa = chip(c, "Full adder", 12, i * 5);
		std::string bit = std::to_string(i);
		link(c, lib, sa, bit, fa, "A");
		link(c, lib, sb, bit, fa, "B");
		link(c, lib, carry, carryPin, fa, "Cin");
		link(c, lib, fa, "S", m, bit);
		carry = fa;
		carryPin = "Cout";
	}
	link(c, lib, a, "out", sa, "in");
	link(c, lib, b, "out", sb, "in");
	link(c, lib, m, "out", s, "in");
	link(c, lib, carry, carryPin, cout, "in");
	lib.chips["4-bit adder"] = std::move(def);
}

inline void addAdders(Library& lib) {
	addHalfAdder(lib);
	addFullAdder(lib);
	addAdder4(lib);
}

} // namespace test
