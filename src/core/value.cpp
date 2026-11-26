#include "core/value.hpp"

#include <format>

namespace logic {

std::string Value::binary() const {
	std::string text;
	for (int i = width - 1; i >= 0; i--) {
		int b = bit(i);
		text += b < 0 ? 'X' : static_cast<char>('0' + b);
	}
	return text;
}

std::string Value::hex() const {
	if (!fullyKnown()) {
		return "0x?";
	}
	return std::format("0x{:0{}X}", bits, (width + 3) / 4);
}

std::string Value::decimal() const {
	return fullyKnown() ? std::to_string(bits) : "X";
}

Value logicAnd(Value a, Value b) {
	std::uint32_t one = a.ones() & b.ones();
	std::uint32_t zero = a.zeros() | b.zeros();
	return Value::make(one, one | zero, a.width);
}

Value logicOr(Value a, Value b) {
	std::uint32_t one = a.ones() | b.ones();
	std::uint32_t zero = a.zeros() & b.zeros();
	return Value::make(one, one | zero, a.width);
}

Value logicXor(Value a, Value b) {
	return Value::make(a.bits ^ b.bits, a.known & b.known, a.width);
}

Value logicNot(Value a) {
	return Value::make(~a.bits, a.known, a.width);
}

Value merge(Value a, Value b) {
	std::uint32_t same = a.known & b.known & ~(a.bits ^ b.bits);
	return Value::make(a.bits, same, a.width);
}

} // namespace logic
