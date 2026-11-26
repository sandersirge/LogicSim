// Three-valued logic on buses: 0, 1 and X per bit.
#include "check.hpp"
#include "core/value.hpp"

#include <format>

using namespace logic;
using test::check;

namespace {

// A 1-bit value from 0, 1 or -1 (X)
Value bit(int b) {
	return b < 0 ? Value::x(1) : Value::of(static_cast<std::uint32_t>(b), 1);
}

int toInt(Value v) {
	return v.bit(0);
}

void testSingleBits() {
	const int values[] = {0, 1, -1};
	for (int a : values) {
		for (int b : values) {
			std::string name = std::format("a={} b={}", a, b);
			int expectAnd = (a == 0 || b == 0) ? 0 : (a == 1 && b == 1) ? 1 : -1;
			int expectOr = (a == 1 || b == 1) ? 1 : (a == 0 && b == 0) ? 0 : -1;
			int expectXor = (a < 0 || b < 0) ? -1 : a ^ b;
			check(toInt(logicAnd(bit(a), bit(b))) == expectAnd, "AND " + name);
			check(toInt(logicOr(bit(a), bit(b))) == expectOr, "OR " + name);
			check(toInt(logicXor(bit(a), bit(b))) == expectXor, "XOR " + name);
			int expectMerge = a == b ? a : -1;
			check(toInt(merge(bit(a), bit(b))) == expectMerge, "merge " + name);
		}
		check(toInt(logicNot(bit(a))) == (a < 0 ? -1 : 1 - a), std::format("NOT {}", a));
	}
}

void testBuses() {
	Value a = Value::of(0b1100, 4), b = Value::of(0b1010, 4);
	check(logicAnd(a, b) == Value::of(0b1000, 4), "4-bit AND");
	check(logicOr(a, b) == Value::of(0b1110, 4), "4-bit OR");
	check(logicXor(a, b) == Value::of(0b0110, 4), "4-bit XOR");
	check(logicNot(a) == Value::of(0b0011, 4), "4-bit NOT stays 4 bits");

	// Partly known: bit 0 unknown
	Value p = Value::make(0b0110, 0b1110, 4);
	check(p.binary() == "011X", "binary text " + p.binary());
	check(logicAnd(p, Value::of(0b1110, 4)) == Value::of(0b0110, 4), "AND with 0 hides the X");
	check(logicOr(p, Value::of(0b0001, 4)) == Value::of(0b0111, 4), "OR with 1 hides the X");
	check(!logicXor(p, Value::of(0, 4)).fullyKnown(), "XOR keeps the X");

	check(Value::of(42, 8).hex() == "0x2A", "hex " + Value::of(42, 8).hex());
	check(Value::of(5, 12).hex() == "0x005", "hex keeps width");
	check(p.hex() == "0x?", "hex of unknown");
	check(Value::of(42, 8).decimal() == "42" && p.decimal() == "X", "decimal");

	Value full = Value::of(0xFFFFFFFFu, 32);
	check(full.fullyKnown() && logicNot(full) == Value::of(0, 32), "32-bit values");
	check(Value::of(0x1F, 4) == Value::of(0xF, 4), "bits above the width are dropped");
}

} // namespace

int main() {
	testSingleBits();
	testBuses();
	return test::report("value_test");
}
