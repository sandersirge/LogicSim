#pragma once

// A signal on a wire or bus: 1 to 32 bits, each bit 0, 1 or X (unknown). X is the power-on state
// of every wire, so a feedback loop such as an SR latch made of NOR gates stays X until it is set
// or reset, and an unconnected input reads X.
#include <cstdint>
#include <string>

namespace logic {

constexpr int MAX_WIDTH = 32;

// The bits 0..width-1 set
constexpr std::uint32_t widthMask(int width) {
	return width >= 32 ? 0xFFFFFFFFu : (std::uint32_t{1} << width) - 1;
}

struct Value {
	std::uint32_t bits = 0;  // the value of the known bits (always 0 where a bit is unknown)
	std::uint32_t known = 0; // 1 where the bit is 0 or 1, 0 where it is X
	int width = 1;

	// All bits unknown
	static constexpr Value x(int width) { return Value{0, 0, width}; }
	// A fully known value (bits above the width are dropped)
	static constexpr Value of(std::uint32_t bits, int width) {
		return Value{bits & widthMask(width), widthMask(width), width};
	}
	// Bits and known-mask (bits are cleared where unknown)
	static constexpr Value make(std::uint32_t bits, std::uint32_t known, int width) {
		std::uint32_t k = known & widthMask(width);
		return Value{bits & k, k, width};
	}

	constexpr std::uint32_t mask() const { return widthMask(width); }
	constexpr bool fullyKnown() const { return known == mask(); }
	constexpr bool isX() const { return known == 0; }
	// Single-bit helpers: a known 1 / a known 0 in bit 0
	constexpr bool isOne() const { return (known & 1) && (bits & 1); }
	constexpr bool isZero() const { return (known & 1) && !(bits & 1); }
	// The bits that are a known 1 / a known 0
	constexpr std::uint32_t ones() const { return bits & known; }
	constexpr std::uint32_t zeros() const { return ~bits & known & mask(); }
	// Bit i: 0, 1 or -1 for X
	constexpr int bit(int i) const { return (known >> i & 1) ? static_cast<int>(bits >> i & 1) : -1; }

	friend constexpr bool operator==(const Value&, const Value&) = default;

	// "0101", "1X0" (most significant bit first)
	std::string binary() const;
	// "0x2A" or "0x?" when not fully known
	std::string hex() const;
	// Decimal, or "X"
	std::string decimal() const;
};

// Bitwise logic with X: a known 0 decides AND, a known 1 decides OR, XOR needs both bits known.
Value logicAnd(Value a, Value b);
Value logicOr(Value a, Value b);
Value logicXor(Value a, Value b);
Value logicNot(Value a);

// Where two values agree keep the bit, elsewhere X (used when a select or enable is X)
Value merge(Value a, Value b);

} // namespace logic
