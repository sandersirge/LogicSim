#pragma once

// The circuit document the editor works on and the files store: placed components, wires between
// their pins, and a library of custom chips. A chip is a circuit whose input and output pins become
// the pins of the chip; chips can contain other chips.
#include "core/components.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace logic {

struct Point {
	int x = 0;
	int y = 0;
	friend bool operator==(const Point&, const Point&) = default;
};

struct Component {
	int id = 0;
	Kind kind = Kind::And;
	Point pos;        // top-left corner of the body in grid units (before rotation)
	int rotation = 0; // quarter turns clockwise, 0..3
	std::string label;
	Props props;
};

struct PinRef {
	int component = -1; // component id
	int pin = -1;       // index into the component's pins
	friend bool operator==(const PinRef&, const PinRef&) = default;
};

struct Wire {
	PinRef from;
	PinRef to;
	std::vector<Point> bends; // corner points between the two pins
};

struct Circuit {
	std::string name;
	std::vector<Component> components;
	std::vector<Wire> wires;
	int nextId = 1;

	// Places a component with default settings (or the given ones); returns its id
	int add(Kind kind, int x = 0, int y = 0);
	int add(Kind kind, int x, int y, Props props, std::string label = {});
	Component* find(int id);
	const Component* find(int id) const;
	// Removes the component and every wire attached to it
	void remove(int id);
};

struct ChipDef {
	Circuit circuit;
	std::string color; // "#RRGGBB", empty = default
	std::string description;
};

struct Library {
	std::map<std::string, ChipDef, std::less<>> chips;

	const ChipDef* find(std::string_view name) const;
	// Chips that the named chip contains, directly or deeper (to reject a chip containing itself)
	bool uses(std::string_view chip, std::string_view other) const;
};

struct Document {
	Circuit main;
	Library library;
};

// A chip's pins: its InputPin and OutputPin components (indices into components), each list sorted
// top to bottom, then left to right.
struct ChipPorts {
	std::vector<int> inputs;
	std::vector<int> outputs;
};
ChipPorts chipPorts(const Circuit& circuit);

// Layout of a chip instance: inputs on the left, outputs on the right, named after the pin labels
ComponentSpec chipSpec(const ChipDef& chip, std::string_view name);
// Layout of any component (chips are looked up in the library; an unknown chip has no pins)
ComponentSpec specOf(const Component& component, const Library& library);

// Adds a wire between two pins given by name; false (and nothing added) if a pin does not exist
bool connect(Circuit& circuit, const Library& library, int fromId, std::string_view fromPin, int toId,
             std::string_view toPin);

} // namespace logic
