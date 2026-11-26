#include "core/circuit.hpp"

#include <algorithm>
#include <format>
#include <set>

namespace logic {

int Circuit::add(Kind kind, int x, int y) {
	return add(kind, x, y, defaultProps(kind));
}

int Circuit::add(Kind kind, int x, int y, Props props, std::string label) {
	sanitize(kind, props);
	int id = nextId++;
	components.push_back(Component{id, kind, Point{x, y}, 0, std::move(label), std::move(props)});
	return id;
}

Component* Circuit::find(int id) {
	auto it = std::ranges::find(components, id, &Component::id);
	return it == components.end() ? nullptr : &*it;
}

const Component* Circuit::find(int id) const {
	auto it = std::ranges::find(components, id, &Component::id);
	return it == components.end() ? nullptr : &*it;
}

void Circuit::remove(int id) {
	std::erase_if(components, [id](const Component& c) { return c.id == id; });
	std::erase_if(wires, [id](const Wire& w) { return w.from.component == id || w.to.component == id; });
}

const ChipDef* Library::find(std::string_view name) const {
	auto it = chips.find(name);
	return it == chips.end() ? nullptr : &it->second;
}

bool Library::uses(std::string_view chip, std::string_view other) const {
	std::set<std::string, std::less<>> seen;
	std::vector<std::string> todo{std::string(chip)};
	while (!todo.empty()) {
		std::string name = std::move(todo.back());
		todo.pop_back();
		const ChipDef* def = find(name);
		if (def == nullptr || !seen.insert(name).second) {
			continue;
		}
		for (const Component& c : def->circuit.components) {
			if (c.kind == Kind::Chip) {
				if (c.props.chip == other) {
					return true;
				}
				todo.push_back(c.props.chip);
			}
		}
	}
	return false;
}

ChipPorts chipPorts(const Circuit& circuit) {
	ChipPorts ports;
	for (size_t i = 0; i < circuit.components.size(); i++) {
		Kind kind = circuit.components[i].kind;
		if (kind == Kind::InputPin) {
			ports.inputs.push_back(static_cast<int>(i));
		} else if (kind == Kind::OutputPin) {
			ports.outputs.push_back(static_cast<int>(i));
		}
	}
	auto byPosition = [&](int a, int b) {
		Point pa = circuit.components[static_cast<size_t>(a)].pos;
		Point pb = circuit.components[static_cast<size_t>(b)].pos;
		return pa.y != pb.y ? pa.y < pb.y : pa.x < pb.x;
	};
	std::ranges::stable_sort(ports.inputs, byPosition);
	std::ranges::stable_sort(ports.outputs, byPosition);
	return ports;
}

ComponentSpec chipSpec(const ChipDef& chip, std::string_view name) {
	ChipPorts ports = chipPorts(chip.circuit);
	int rows = static_cast<int>(std::max(ports.inputs.size(), ports.outputs.size()));
	// Wide enough for the name and the longest pin names on both sides
	size_t longestIn = 0, longestOut = 0;
	for (int i : ports.inputs) {
		longestIn = std::max(longestIn, chip.circuit.components[static_cast<size_t>(i)].label.size());
	}
	for (int i : ports.outputs) {
		longestOut = std::max(longestOut, chip.circuit.components[static_cast<size_t>(i)].label.size());
	}
	int textWidth = static_cast<int>(std::max(name.size(), longestIn + longestOut + 2));
	ComponentSpec spec{std::clamp((textWidth + 1) / 2 + 1, 3, 16), std::max(rows + 1, 2), {}};

	std::set<std::string> used;
	auto addPin = [&](int index, bool output, int row, int number) {
		const Component& c = chip.circuit.components[static_cast<size_t>(index)];
		std::string pinName = c.label.empty() ? std::format("{}{}", output ? "out" : "in", number) : c.label;
		while (!used.insert(pinName).second) {
			pinName += "'";
		}
		spec.pins.push_back(PinSpec{pinName, output, c.props.width, output ? spec.w : 0, row, std::nullopt});
	};
	for (size_t i = 0; i < ports.inputs.size(); i++) {
		addPin(ports.inputs[i], false, static_cast<int>(i) + 1, static_cast<int>(i));
	}
	for (size_t i = 0; i < ports.outputs.size(); i++) {
		addPin(ports.outputs[i], true, static_cast<int>(i) + 1, static_cast<int>(i));
	}
	return spec;
}

ComponentSpec specOf(const Component& component, const Library& library) {
	if (component.kind != Kind::Chip) {
		return builtinSpec(component.kind, component.props);
	}
	const ChipDef* chip = library.find(component.props.chip);
	if (chip == nullptr) {
		return ComponentSpec{4, 2, {}};
	}
	return chipSpec(*chip, component.props.chip);
}

bool connect(Circuit& circuit, const Library& library, int fromId, std::string_view fromPin, int toId,
             std::string_view toPin) {
	const Component* from = circuit.find(fromId);
	const Component* to = circuit.find(toId);
	if (from == nullptr || to == nullptr) {
		return false;
	}
	int a = specOf(*from, library).pinIndex(fromPin);
	int b = specOf(*to, library).pinIndex(toPin);
	if (a < 0 || b < 0) {
		return false;
	}
	circuit.wires.push_back(Wire{PinRef{fromId, a}, PinRef{toId, b}, {}});
	return true;
}

} // namespace logic
