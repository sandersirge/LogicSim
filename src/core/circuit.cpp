#include "core/circuit.hpp"

#include <algorithm>
#include <format>
#include <limits>
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

std::vector<int> portIds(const Circuit& circuit) {
	ChipPorts ports = chipPorts(circuit);
	std::vector<int> ids;
	for (const std::vector<int>* list : {&ports.inputs, &ports.outputs}) {
		for (int index : *list) {
			ids.push_back(circuit.components[static_cast<size_t>(index)].id);
		}
	}
	return ids;
}

namespace {

// Every circuit of a document: the main one and the inside of each chip
template <typename F>
void forEachCircuit(Document& document, F f) {
	f(document.main);
	for (auto& [name, chip] : document.library.chips) {
		f(chip.circuit);
	}
}

bool sameChip(const ChipDef& a, const ChipDef& b) {
	return a.color == b.color && a.description == b.description &&
	       a.circuit.components == b.circuit.components && a.circuit.wires == b.circuit.wires;
}

} // namespace

void remapChipPins(Document& document, std::string_view name, const std::vector<int>& before) {
	const ChipDef* def = document.library.find(name);
	if (def == nullptr) {
		return;
	}
	std::vector<int> after = portIds(def->circuit);
	if (after == before) {
		return;
	}
	// Old pin index -> new pin index, -1 when the pin is gone
	std::vector<int> moved(before.size(), -1);
	for (size_t i = 0; i < before.size(); i++) {
		auto it = std::ranges::find(after, before[i]);
		if (it != after.end()) {
			moved[i] = static_cast<int>(it - after.begin());
		}
	}
	forEachCircuit(document, [&](Circuit& circuit) {
		auto follow = [&](PinRef& ref) {
			const Component* c = circuit.find(ref.component);
			if (c == nullptr || c->kind != Kind::Chip || c->props.chip != name) {
				return true;
			}
			if (ref.pin < 0 || ref.pin >= static_cast<int>(moved.size())) {
				return false;
			}
			ref.pin = moved[static_cast<size_t>(ref.pin)];
			return ref.pin >= 0;
		};
		std::erase_if(circuit.wires, [&](Wire& w) {
			bool from = follow(w.from);
			bool to = follow(w.to);
			return !(from && to);
		});
	});
}

ChipDef chipFromSelection(const Circuit& circuit, const std::set<int>& ids) {
	ChipDef chip;
	Circuit& inside = chip.circuit;
	Point min{std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
	for (const Component& c : circuit.components) {
		if (ids.contains(c.id)) {
			min = {std::min(min.x, c.pos.x), std::min(min.y, c.pos.y)};
		}
	}
	// A margin of 2 leaves room for the labels above the parts
	Point shift{2 - min.x, 2 - min.y};
	for (const Component& c : circuit.components) {
		if (ids.contains(c.id)) {
			Component copy = c;
			copy.pos = {c.pos.x + shift.x, c.pos.y + shift.y};
			inside.components.push_back(std::move(copy));
			inside.nextId = std::max(inside.nextId, c.id + 1);
		}
	}
	for (const Wire& w : circuit.wires) {
		if (ids.contains(w.from.component) && ids.contains(w.to.component)) {
			Wire copy = w;
			for (Point& b : copy.bends) {
				b = {b.x + shift.x, b.y + shift.y};
			}
			inside.wires.push_back(std::move(copy));
		}
	}
	return chip;
}

int countInstances(const Document& document, std::string_view name) {
	int count = 0;
	auto countIn = [&](const Circuit& circuit) {
		for (const Component& c : circuit.components) {
			count += c.kind == Kind::Chip && c.props.chip == name ? 1 : 0;
		}
	};
	countIn(document.main);
	for (const auto& [chipName, chip] : document.library.chips) {
		countIn(chip.circuit);
	}
	return count;
}

bool renameChip(Document& document, std::string_view from, const std::string& to) {
	auto it = document.library.chips.find(from);
	if (to.empty() || it == document.library.chips.end() || document.library.find(to) != nullptr) {
		return false;
	}
	std::string old(from); // `from` may point into the key being replaced
	auto node = document.library.chips.extract(it);
	node.key() = to;
	if (node.mapped().circuit.name == old) {
		node.mapped().circuit.name = to;
	}
	document.library.chips.insert(std::move(node));
	forEachCircuit(document, [&](Circuit& circuit) {
		for (Component& c : circuit.components) {
			if (c.kind == Kind::Chip && c.props.chip == old) {
				c.props.chip = to;
			}
		}
	});
	return true;
}

std::vector<std::string> mergeLibrary(Library& into, const Library& from) {
	std::vector<std::string> messages;
	std::map<std::string, std::string, std::less<>> renamed; // imported name -> name in `into`
	std::set<std::string, std::less<>> seen;
	// Chips are added after the chips they use, so references to renamed chips can be updated
	auto add = [&](auto& self, const std::string& name) -> void {
		if (!seen.insert(name).second) {
			return;
		}
		const ChipDef& def = from.chips.find(name)->second;
		for (const Component& c : def.circuit.components) {
			if (c.kind == Kind::Chip && from.find(c.props.chip) != nullptr) {
				self(self, c.props.chip);
			}
		}
		ChipDef copy = def;
		for (Component& c : copy.circuit.components) {
			if (auto it = renamed.find(c.props.chip); c.kind == Kind::Chip && it != renamed.end()) {
				c.props.chip = it->second;
			}
		}
		std::string target = name;
		if (const ChipDef* existing = into.find(name)) {
			if (sameChip(*existing, copy)) {
				renamed[name] = name;
				return;
			}
			for (int n = 2; into.find(target = std::format("{} ({})", name, n)) != nullptr; n++) {
			}
			messages.push_back(std::format(
			    "A different chip called \"{}\" is already here; the imported one is called \"{}\"", name,
			    target));
		}
		renamed[name] = target;
		if (copy.circuit.name == name) {
			copy.circuit.name = target;
		}
		into.chips[target] = std::move(copy);
	};
	for (const auto& [name, def] : from.chips) {
		add(add, name);
	}
	return messages;
}

} // namespace logic
