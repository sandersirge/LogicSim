#pragma once

// Turns a circuit (with its library of chips) into a flat netlist for the simulator. Chip instances
// are expanded into their parts, wires and tunnels become nets, and problems are reported with the
// id of the top-level component they belong to. A circuit with errors still compiles as far as
// possible, so the rest of it can be simulated.
#include "core/circuit.hpp"
#include "core/netlist.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace logic {

struct Diagnostic {
	int component = -1; // top-level component id, -1 if none
	std::string message;
	bool error = true; // false: a warning
};

struct Compiled {
	Netlist netlist;
	std::vector<Diagnostic> diagnostics;
	// For each top-level component id: the net of each of its pins (index as in its spec), so the
	// canvas can colour wires and pins and read displays
	std::unordered_map<int, std::vector<int>> pinNets;
	// Top-level component id -> its primitive (sources, flip-flops, memories...)
	std::unordered_map<int, int> prims;

	bool hasErrors() const;
	int net(int component, int pin) const;
	int prim(int component) const;
};

Compiled compile(const Circuit& circuit, const Library& library);

} // namespace logic
