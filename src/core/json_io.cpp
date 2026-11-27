#include "core/json_io.hpp"

#include <nlohmann/json.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace logic {

namespace {

using nlohmann::json;

constexpr int FORMAT_VERSION = 1;

json propsToJson(Kind kind, const Props& p) {
	const Props d = defaultProps(kind);
	unsigned flags = info(kind).props;
	json j = json::object();
	if ((flags & P_INPUTS) && p.inputs != d.inputs) {
		j["inputs"] = p.inputs;
	}
	if ((flags & P_WIDTH) && p.width != d.width) {
		j["width"] = p.width;
	}
	if ((flags & P_DELAY) && p.delay != d.delay) {
		j["delay"] = p.delay;
	}
	if ((flags & P_PERIOD) && p.period != d.period) {
		j["period"] = p.period;
	}
	if ((flags & P_SELECT) && p.selectBits != d.selectBits) {
		j["selectBits"] = p.selectBits;
	}
	if ((flags & P_ADDRESS) && p.addressBits != d.addressBits) {
		j["addressBits"] = p.addressBits;
	}
	if ((flags & P_VALUE) && p.value != d.value) {
		j["value"] = p.value;
	}
	if ((flags & P_COLOR) && !p.color.empty()) {
		j["color"] = p.color;
	}
	if ((flags & P_TEXT) && p.text != d.text) {
		j["text"] = p.text;
	}
	if ((flags & P_PARTS) && !p.parts.empty()) {
		j["parts"] = p.parts;
	}
	if (flags & P_DATA) {
		// Trailing zero words are left out
		size_t used = p.data.size();
		while (used > 0 && p.data[used - 1] == 0) {
			used--;
		}
		if (used > 0) {
			j["data"] = std::vector<std::uint32_t>(p.data.begin(), p.data.begin() + static_cast<long>(used));
		}
	}
	return j;
}

Props propsFromJson(Kind kind, const json& j) {
	Props p = defaultProps(kind);
	if (!j.is_object()) {
		return p;
	}
	p.inputs = j.value("inputs", p.inputs);
	p.width = j.value("width", p.width);
	p.delay = j.value("delay", p.delay);
	p.period = j.value("period", p.period);
	p.selectBits = j.value("selectBits", p.selectBits);
	p.addressBits = j.value("addressBits", p.addressBits);
	p.value = j.value("value", p.value);
	p.color = j.value("color", p.color);
	p.text = j.value("text", p.text);
	if (j.contains("parts")) {
		p.parts = j["parts"].get<std::vector<int>>();
	}
	if (j.contains("data")) {
		p.data = j["data"].get<std::vector<std::uint32_t>>();
	}
	return p;
}

json circuitToJson(const Circuit& circuit, const Library& library) {
	json components = json::array();
	for (const Component& c : circuit.components) {
		json jc = {{"id", c.id}, {"type", info(c.kind).id}, {"x", c.pos.x}, {"y", c.pos.y}};
		if (c.rotation != 0) {
			jc["rotation"] = c.rotation;
		}
		if (!c.label.empty()) {
			jc["label"] = c.label;
		}
		if (c.kind == Kind::Chip) {
			jc["chip"] = c.props.chip;
		}
		json props = propsToJson(c.kind, c.props);
		if (!props.empty()) {
			jc["props"] = props;
		}
		components.push_back(jc);
	}

	json wires = json::array();
	auto pinName = [&](PinRef ref) -> std::string {
		const Component* c = circuit.find(ref.component);
		if (c == nullptr) {
			return {};
		}
		ComponentSpec spec = specOf(*c, library);
		return ref.pin >= 0 && ref.pin < static_cast<int>(spec.pins.size())
		           ? spec.pins[static_cast<size_t>(ref.pin)].name
		           : std::string{};
	};
	for (const Wire& w : circuit.wires) {
		std::string from = pinName(w.from), to = pinName(w.to);
		if (from.empty() || to.empty()) {
			continue;
		}
		json jw = {{"from", {w.from.component, from}}, {"to", {w.to.component, to}}};
		if (!w.bends.empty()) {
			json bends = json::array();
			for (Point p : w.bends) {
				bends.push_back({p.x, p.y});
			}
			jw["bends"] = bends;
		}
		wires.push_back(jw);
	}

	json j = {{"components", components}, {"wires", wires}};
	if (!circuit.name.empty()) {
		j["name"] = circuit.name;
	}
	return j;
}

void componentsFromJson(const json& j, Circuit& circuit, std::vector<std::string>& warnings) {
	circuit.name = j.value("name", std::string{});
	if (!j.contains("components")) {
		return;
	}
	for (const json& jc : j["components"]) {
		std::string type = jc.value("type", std::string{});
		std::optional<Kind> kind = kindFromId(type);
		if (!kind) {
			warnings.push_back(std::format("Unknown component type \"{}\" skipped", type));
			continue;
		}
		int id = jc.value("id", 0);
		if (id <= 0 || circuit.find(id) != nullptr) {
			id = circuit.nextId;
		}
		Props props = propsFromJson(*kind, jc.value("props", json::object()));
		if (*kind == Kind::Chip) {
			props.chip = jc.value("chip", std::string{});
		}
		sanitize(*kind, props);
		int rotation = *kind == Kind::Text ? 0 : jc.value("rotation", 0) & 3; // notes never turn
		circuit.components.push_back(Component{id, *kind, Point{jc.value("x", 0), jc.value("y", 0)}, rotation,
		                                       jc.value("label", std::string{}), std::move(props)});
		circuit.nextId = std::max(circuit.nextId, id + 1);
	}
}

void wiresFromJson(const json& j, Circuit& circuit, const Library& library,
                   std::vector<std::string>& warnings) {
	if (!j.contains("wires")) {
		return;
	}
	auto pinRef = [&](const json& end) -> PinRef {
		if (!end.is_array() || end.size() != 2) {
			return {};
		}
		int id = end[0].get<int>();
		const Component* c = circuit.find(id);
		if (c == nullptr) {
			return {};
		}
		return PinRef{id, specOf(*c, library).pinIndex(end[1].get<std::string>())};
	};
	for (const json& jw : j["wires"]) {
		PinRef from = pinRef(jw.value("from", json{})), to = pinRef(jw.value("to", json{}));
		if (from.pin < 0 || to.pin < 0) {
			warnings.push_back(std::format("A wire to a missing pin was skipped ({})", jw.dump()));
			continue;
		}
		Wire wire{from, to, {}};
		for (const json& b : jw.value("bends", json::array())) {
			wire.bends.push_back(Point{b.at(0).get<int>(), b.at(1).get<int>()});
		}
		circuit.wires.push_back(std::move(wire));
	}
}

json libraryToJson(const Library& library) {
	json chips = json::object();
	for (const auto& [name, chip] : library.chips) {
		json jc = {{"circuit", circuitToJson(chip.circuit, library)}};
		if (!chip.color.empty()) {
			jc["color"] = chip.color;
		}
		if (!chip.description.empty()) {
			jc["description"] = chip.description;
		}
		chips[name] = jc;
	}
	return chips;
}

} // namespace

std::string saveDocument(const Document& document) {
	json j = {{"format", "logicsim"},
	          {"version", FORMAT_VERSION},
	          {"circuit", circuitToJson(document.main, document.library)},
	          {"chips", libraryToJson(document.library)}};
	return j.dump(1, '\t') + "\n";
}

std::string saveLibrary(const Library& library) {
	json j = {{"format", "logicsim"}, {"version", FORMAT_VERSION}, {"chips", libraryToJson(library)}};
	return j.dump(1, '\t') + "\n";
}

LoadResult loadDocument(std::string_view text) {
	LoadResult result;
	try {
		json j = json::parse(text);
		if (!j.is_object() || j.value("format", std::string{}) != "logicsim") {
			result.error = "Not a LogicSim circuit file";
			return result;
		}
		if (j.value("version", 0) > FORMAT_VERSION) {
			result.warnings.push_back("The file was saved by a newer version; some parts may be missing");
		}
		Document& doc = result.document;
		// Components of every circuit first: a wire to a chip pin needs that chip's pins
		const json chips = j.value("chips", json::object());
		for (const auto& [name, jc] : chips.items()) {
			ChipDef chip;
			chip.color = jc.value("color", std::string{});
			chip.description = jc.value("description", std::string{});
			componentsFromJson(jc.value("circuit", json::object()), chip.circuit, result.warnings);
			doc.library.chips[name] = std::move(chip);
		}
		const json main = j.value("circuit", json::object());
		componentsFromJson(main, doc.main, result.warnings);
		for (const auto& [name, jc] : chips.items()) {
			wiresFromJson(jc.value("circuit", json::object()), doc.library.chips[name].circuit, doc.library,
			              result.warnings);
		}
		wiresFromJson(main, doc.main, doc.library, result.warnings);
		result.ok = true;
	} catch (const nlohmann::json::exception& e) {
		result.error = std::format("Could not read the file: {}", e.what());
		result.document = {};
	}
	return result;
}

bool saveFile(const std::filesystem::path& path, const std::string& text, std::string& error) {
	std::ofstream file(path, std::ios::binary);
	file << text;
	if (!file) {
		error = std::format("Could not write {}", path.string());
		return false;
	}
	return true;
}

LoadResult loadFile(const std::filesystem::path& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		LoadResult result;
		result.error = std::format("Could not open {}", path.string());
		return result;
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	return loadDocument(buffer.str());
}

} // namespace logic
