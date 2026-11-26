#pragma once

// A flat netlist: primitives (gates, flip-flops, memories, sources) connected by nets. The netlist
// compiler (compiler.hpp) builds it from a circuit with its chips flattened; the simulator runs it.
#include "core/components.hpp"
#include "core/value.hpp"

#include <span>
#include <vector>

namespace logic {

struct Net {
	int width = 1;
	int driver = -1;          // the primitive driving it (-1: undriven, stays X)
	std::vector<int> readers; // primitives with an input on it (each listed once)
};

struct Prim {
	Kind kind = Kind::Buffer;
	Props props;
	std::vector<int> in;  // net of each input pin, in the order of the component's pins
	std::vector<int> out; // net of each output pin
	int delay = 1;
	int owner = -1; // id of the top-level component it came from (a chip instance for inner parts)
	// The component ids from the top level down through the chips to the component it came from
	// (empty for the constants the compiler adds on unconnected optional pins). It identifies the
	// primitive across recompiles, so an edited circuit can keep its state.
	std::vector<int> path;

	// State (reset by resetState)
	Value source;              // switch, button, input pin, constant: the value it drives
	Value state;               // flip-flops, registers, counters: the stored value
	Value previousClock;       // the clock input at the last evaluation, for edge detection
	std::vector<Value> memory; // RAM and ROM words
};

struct Netlist {
	std::vector<Net> nets;
	std::vector<Prim> prims;

	int addNet(int width) {
		nets.push_back(Net{width, -1, {}});
		return static_cast<int>(nets.size()) - 1;
	}
	// Adds a primitive and registers it as driver and reader of its nets
	int addPrim(Prim prim);
};

// Is it a source whose value comes from outside (GUI clicks, Simulator::setSource)?
bool isSource(Kind kind);
// Power-on state: stored values 0, ROM loaded from its props, sources from Props::value
void resetState(Prim& prim);
// Computes the outputs from the inputs (and updates the stored state on clock edges)
void evaluate(Prim& prim, std::span<const Value> in, std::span<Value> out);

} // namespace logic
