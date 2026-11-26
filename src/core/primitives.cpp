// What every primitive computes. Inputs are in the order of the component's input pins (see
// builtinSpec), outputs in the order of its output pins; out[] arrives filled with X of the right
// widths.
#include "core/netlist.hpp"

#include <algorithm>

namespace logic {

namespace {

using enum Kind;

// Control pins (clock, enable, set, reset, write enable) act only on a known 1: an X on them, e.g.
// at power-on before the switches drive them, leaves the stored value alone. Data X is stored.
bool risingEdge(Prim& p, Value clock) {
	Value previous = p.previousClock;
	p.previousClock = clock;
	return previous.isZero() && clock.isOne();
}

// Asynchronous set / reset of the state; returns true when one of them is active
bool asyncSetReset(Prim& p, Value set, Value reset) {
	int w = p.state.width;
	if (set.isOne() && reset.isOne()) {
		p.state = Value::x(w);
		return true;
	}
	if (reset.isOne()) {
		p.state = Value::of(0, w);
		return true;
	}
	if (set.isOne()) {
		p.state = Value::of(widthMask(w), w);
		return true;
	}
	return false;
}

Value gateFold(Kind kind, std::span<const Value> in) {
	Value acc = in[0];
	for (size_t i = 1; i < in.size(); i++) {
		switch (kind) {
		case And:
		case Nand:
			acc = logicAnd(acc, in[i]);
			break;
		case Or:
		case Nor:
			acc = logicOr(acc, in[i]);
			break;
		default:
			acc = logicXor(acc, in[i]);
			break;
		}
	}
	bool invert = kind == Nand || kind == Nor || kind == Xnor || kind == Not;
	return invert ? logicNot(acc) : acc;
}

void outputQ(const Prim& p, std::span<Value> out) {
	out[0] = p.state;
	out[1] = logicNot(p.state);
}

} // namespace

int Netlist::addPrim(Prim prim) {
	int index = static_cast<int>(prims.size());
	for (int net : prim.in) {
		std::vector<int>& readers = nets[static_cast<size_t>(net)].readers;
		if (std::ranges::find(readers, index) == readers.end()) {
			readers.push_back(index);
		}
	}
	for (int net : prim.out) {
		nets[static_cast<size_t>(net)].driver = index;
	}
	prims.push_back(std::move(prim));
	return index;
}

bool isSource(Kind kind) {
	return kind == Switch || kind == Button || kind == Constant || kind == InputPin || kind == Clock;
}

void resetState(Prim& p) {
	int width = p.out.empty() ? 1 : p.props.width;
	p.previousClock = Value::x(1);
	p.state = Value::of(0, width);
	p.memory.clear();
	switch (p.kind) {
	case Switch:
	case Constant:
	case InputPin:
		p.source = Value::of(p.props.value, p.props.width);
		break;
	case Button:
	case Clock:
		p.source = Value::of(0, 1);
		break;
	case SrLatch:
	case DLatch:
	case DFlipFlop:
	case JkFlipFlop:
	case TFlipFlop:
		p.state = Value::of(0, 1);
		break;
	case Ram:
		p.memory.assign(size_t{1} << p.props.addressBits, Value::of(0, p.props.width));
		break;
	case Rom:
		p.memory.resize(size_t{1} << p.props.addressBits, Value::of(0, p.props.width));
		for (size_t i = 0; i < p.memory.size(); i++) {
			p.memory[i] = Value::of(i < p.props.data.size() ? p.props.data[i] : 0, p.props.width);
		}
		break;
	default:
		break;
	}
}

void evaluate(Prim& p, std::span<const Value> in, std::span<Value> out) {
	switch (p.kind) {
	case And:
	case Or:
	case Not:
	case Nand:
	case Nor:
	case Xor:
	case Xnor:
		out[0] = gateFold(p.kind, in);
		break;
	case Buffer:
		out[0] = in[0];
		break;

	case Switch:
	case Button:
	case Constant:
	case InputPin:
	case Clock:
		out[0] = p.source;
		break;

	case Splitter: {
		int low = 0;
		for (Value& part : out) {
			part = Value::make(in[0].bits >> low, in[0].known >> low, part.width);
			low += part.width;
		}
		break;
	}
	case Merger: {
		std::uint32_t bits = 0, known = 0;
		int low = 0;
		for (const Value& part : in) {
			bits |= part.bits << low;
			known |= part.known << low;
			low += part.width;
		}
		out[0] = Value::make(bits, known, out[0].width);
		break;
	}

	case Mux: {
		std::span<const Value> data = in.first(in.size() - 1);
		Value sel = in.back();
		if (sel.fullyKnown()) {
			out[0] = data[sel.bits];
		} else {
			// Only the inputs the select could still pick matter
			bool first = true;
			for (std::uint32_t i = 0; i < data.size(); i++) {
				if ((i & sel.known) == sel.bits) {
					out[0] = first ? data[i] : merge(out[0], data[i]);
					first = false;
				}
			}
		}
		break;
	}
	case Demux: {
		Value sel = in[1];
		for (std::uint32_t i = 0; i < out.size(); i++) {
			bool possible = (i & sel.known) == sel.bits;
			Value zero = Value::of(0, out[i].width);
			if (sel.fullyKnown()) {
				out[i] = i == sel.bits ? in[0] : zero;
			} else {
				out[i] = possible ? merge(in[0], zero) : zero;
			}
		}
		break;
	}
	case Decoder: {
		Value sel = in[0];
		for (std::uint32_t i = 0; i < out.size(); i++) {
			bool possible = (i & sel.known) == sel.bits;
			if (sel.fullyKnown()) {
				out[i] = Value::of(i == sel.bits, 1);
			} else {
				out[i] = possible ? Value::x(1) : Value::of(0, 1);
			}
		}
		break;
	}
	case PriorityEncoder: {
		int selectBits = out[0].width;
		out[0] = Value::of(0, selectBits);
		out[1] = Value::of(0, 1);
		for (int i = static_cast<int>(in.size()) - 1; i >= 0; i--) {
			if (in[i].isOne()) {
				out[0] = Value::of(static_cast<std::uint32_t>(i), selectBits);
				out[1] = Value::of(1, 1);
				break;
			}
			if (!in[i].isZero()) {
				out[0] = Value::x(selectBits);
				out[1] = Value::x(1);
				break;
			}
		}
		break;
	}

	case SrLatch: {
		Value s = in[0], r = in[1];
		if (s.isOne() && r.isOne()) {
			// Not allowed: like a NOR latch both outputs are 0, and what remains afterwards is unknown
			p.state = Value::x(1);
			out[0] = out[1] = Value::of(0, 1);
			return;
		}
		asyncSetReset(p, s, r);
		outputQ(p, out);
		break;
	}
	case DLatch:
		if (in[1].isOne()) {
			p.state = in[0];
		}
		outputQ(p, out);
		break;
	case DFlipFlop: {
		bool edge = risingEdge(p, in[1]);
		if (!asyncSetReset(p, in[2], in[3]) && edge) {
			p.state = in[0];
		}
		outputQ(p, out);
		break;
	}
	case JkFlipFlop: {
		bool edge = risingEdge(p, in[1]);
		if (!asyncSetReset(p, in[3], in[4]) && edge) {
			Value j = in[0], k = in[2];
			// Q' = J·Q̄ + K̄·Q
			p.state = logicOr(logicAnd(j, logicNot(p.state)), logicAnd(logicNot(k), p.state));
		}
		outputQ(p, out);
		break;
	}
	case TFlipFlop: {
		bool edge = risingEdge(p, in[1]);
		if (!asyncSetReset(p, in[2], in[3]) && edge) {
			p.state = logicXor(p.state, in[0]);
		}
		outputQ(p, out);
		break;
	}

	case Register: {
		bool edge = risingEdge(p, in[2]);
		if (in[3].isOne()) {
			p.state = Value::of(0, p.state.width);
		} else if (edge && in[1].isOne()) {
			p.state = in[0];
		}
		out[0] = p.state;
		break;
	}
	case Counter: {
		// Inputs: D, LD, EN, CLK, RST
		bool edge = risingEdge(p, in[3]);
		int w = p.state.width;
		if (in[4].isOne()) {
			p.state = Value::of(0, w);
		} else if (edge && in[1].isOne()) {
			p.state = in[0];
		} else if (edge && in[2].isOne()) {
			p.state = p.state.fullyKnown() ? Value::of(p.state.bits + 1, w) : Value::x(w);
		}
		out[0] = p.state;
		// Carry out: all ones while counting is enabled
		bool allOnes = p.state.fullyKnown() && p.state.bits == widthMask(w);
		out[1] = Value::of(allOnes && in[2].isOne(), 1);
		break;
	}
	case Ram: {
		// Inputs: A, D, WE, CLK
		bool edge = risingEdge(p, in[3]);
		Value address = in[0];
		if (edge && in[2].isOne()) {
			if (address.fullyKnown()) {
				p.memory[address.bits] = in[1];
			} else {
				// Unknown address: any word the address could mean may have been written
				for (std::uint32_t i = 0; i < p.memory.size(); i++) {
					if ((i & address.known) == address.bits) {
						p.memory[i] = merge(p.memory[i], in[1]);
					}
				}
			}
		}
		out[0] = address.fullyKnown() ? p.memory[address.bits] : Value::x(out[0].width);
		break;
	}
	case Rom: {
		Value address = in[0];
		out[0] = address.fullyKnown() ? p.memory[address.bits] : Value::x(out[0].width);
		break;
	}

	default:
		// Displays, probes, output pins, tunnels and text have no outputs
		break;
	}
}

} // namespace logic
