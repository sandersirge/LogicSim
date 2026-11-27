#include "core/components.hpp"
#include "core/value.hpp"

#include <algorithm>
#include <format>
#include <numeric>

namespace logic {

namespace {

using enum Kind;
constexpr unsigned GATE = P_INPUTS | P_WIDTH | P_DELAY;
constexpr unsigned FLIP = P_DELAY;

const std::vector<KindInfo> KINDS = {
    {And, "and", "AND", Category::Gates, GATE, "1 when every input is 1"},
    {Or, "or", "OR", Category::Gates, GATE, "1 when any input is 1"},
    {Not, "not", "NOT", Category::Gates, P_WIDTH | P_DELAY, "Inverts its input"},
    {Nand, "nand", "NAND", Category::Gates, GATE, "0 when every input is 1"},
    {Nor, "nor", "NOR", Category::Gates, GATE, "0 when any input is 1"},
    {Xor, "xor", "XOR", Category::Gates, GATE, "1 when an odd number of inputs are 1"},
    {Xnor, "xnor", "XNOR", Category::Gates, GATE, "1 when an even number of inputs are 1"},
    {Buffer, "buffer", "Buffer", Category::Gates, P_WIDTH | P_DELAY, "Passes its input on after a delay"},

    {Switch, "switch", "Switch", Category::InputOutput, P_VALUE, "Click to turn on or off"},
    {Button, "button", "Button", Category::InputOutput, 0, "1 while held down"},
    {Constant, "constant", "Constant", Category::InputOutput, P_WIDTH | P_VALUE, "A fixed value"},
    {Clock, "clock", "Clock", Category::InputOutput, P_PERIOD, "Toggles on its own"},
    {Led, "led", "LED", Category::InputOutput, P_COLOR, "Lights up on 1"},
    {SevenSegment, "seven_segment", "7-segment", Category::InputOutput, P_COLOR, "Segments a-g and the dot"},
    {HexDisplay, "hex_display", "Hex digit", Category::InputOutput, P_COLOR, "Shows a 4-bit value as 0-F"},
    {Probe, "probe", "Probe", Category::InputOutput, P_WIDTH, "Shows the value on a wire"},
    {InputPin, "input_pin", "Input pin", Category::InputOutput, P_WIDTH | P_VALUE,
     "An input of a chip; at the top level a clickable input"},
    {OutputPin, "output_pin", "Output pin", Category::InputOutput, P_WIDTH, "An output of a chip"},

    {Splitter, "splitter", "Splitter", Category::Wiring, P_WIDTH | P_PARTS, "Splits a bus into parts"},
    {Merger, "merger", "Merger", Category::Wiring, P_WIDTH | P_PARTS, "Joins parts into a bus"},
    {Tunnel, "tunnel", "Tunnel", Category::Wiring, P_WIDTH | P_TEXT,
     "Tunnels with the same name are connected"},
    {Text, "text", "Text", Category::Wiring, P_TEXT, "A note on the canvas"},

    {Mux, "mux", "Multiplexer", Category::Plexers, P_WIDTH | P_SELECT | P_DELAY,
     "Passes the selected input on"},
    {Demux, "demux", "Demultiplexer", Category::Plexers, P_WIDTH | P_SELECT | P_DELAY,
     "Sends the input to the selected output"},
    {Decoder, "decoder", "Decoder", Category::Plexers, P_SELECT | P_DELAY, "Sets the selected output to 1"},
    {PriorityEncoder, "priority_encoder", "Priority encoder", Category::Plexers, P_SELECT | P_DELAY,
     "The number of the highest input that is 1"},

    {SrLatch, "sr_latch", "SR latch", Category::FlipFlops, FLIP, "S sets, R resets"},
    {DLatch, "d_latch", "D latch", Category::FlipFlops, FLIP, "Follows D while EN is 1"},
    {DFlipFlop, "d_flip_flop", "D flip-flop", Category::FlipFlops, FLIP, "Stores D on a rising clock edge"},
    {JkFlipFlop, "jk_flip_flop", "JK flip-flop", Category::FlipFlops, FLIP,
     "J sets, K resets, both toggle (on a rising edge)"},
    {TFlipFlop, "t_flip_flop", "T flip-flop", Category::FlipFlops, FLIP,
     "Toggles on a rising edge when T is 1"},

    {Register, "register", "Register", Category::Memory, P_WIDTH | P_DELAY,
     "Stores D on a rising edge when EN is 1"},
    {Counter, "counter", "Counter", Category::Memory, P_WIDTH | P_DELAY,
     "Counts up on a rising edge; LD loads D"},
    {Ram, "ram", "RAM", Category::Memory, P_WIDTH | P_ADDRESS | P_DELAY,
     "Writes D at address A on a rising edge when WE is 1"},
    {Rom, "rom", "ROM", Category::Memory, P_WIDTH | P_ADDRESS | P_DATA | P_DELAY, "Fixed contents"},

    {Chip, "chip", "Chip", Category::Chips, 0, "A custom chip"},
};

PinSpec in(std::string name, int width, int x, int y, std::optional<std::uint32_t> floating = std::nullopt) {
	return PinSpec{std::move(name), false, width, x, y, floating};
}

PinSpec out(std::string name, int width, int x, int y) {
	return PinSpec{std::move(name), true, width, x, y, std::nullopt};
}

ComponentSpec gate(const Props& p, bool single) {
	if (single) {
		return {2, 2, {in("in", p.width, 0, 1), out("out", p.width, 2, 1)}};
	}
	ComponentSpec spec{4, gateHeight(p.inputs), {}};
	std::vector<int> rows = gateRows(p.inputs);
	for (int i = 0; i < p.inputs; i++) {
		spec.pins.push_back(in(std::format("in{}", i), p.width, 0, rows[i]));
	}
	spec.pins.push_back(out("out", p.width, 4, spec.h / 2));
	return spec;
}

// Q and Q-bar on the right, async set/reset on the top/bottom
void flipFlopOutputs(ComponentSpec& spec, bool setReset) {
	if (setReset) {
		spec.pins.push_back(in("S", 1, 2, 0, 0));
		spec.pins.push_back(in("R", 1, 2, 4, 0));
	}
	spec.pins.push_back(out("Q", 1, 4, 1));
	spec.pins.push_back(out("Qn", 1, 4, 3));
}

} // namespace

const std::vector<KindInfo>& allKinds() {
	return KINDS;
}

const KindInfo& info(Kind kind) {
	return KINDS[static_cast<size_t>(kind)];
}

std::optional<Kind> kindFromId(std::string_view id) {
	for (const KindInfo& k : KINDS) {
		if (k.id == id) {
			return k.kind;
		}
	}
	return std::nullopt;
}

std::string_view categoryName(Category category) {
	switch (category) {
	case Category::Gates:
		return "Gates";
	case Category::InputOutput:
		return "Input / output";
	case Category::Wiring:
		return "Wiring";
	case Category::Plexers:
		return "Plexers";
	case Category::FlipFlops:
		return "Flip-flops";
	case Category::Memory:
		return "Memory";
	case Category::Chips:
		return "Chips";
	}
	return "";
}

Props defaultProps(Kind kind) {
	Props p;
	switch (kind) {
	case Splitter:
	case Merger:
		p.width = 4;
		p.delay = 0;
		break;
	case Switch:
	case Button:
	case Constant:
	case Clock:
	case InputPin:
	case Led:
	case SevenSegment:
	case HexDisplay:
	case Probe:
	case OutputPin:
	case Tunnel:
	case Text:
	case Chip:
		p.delay = 0;
		break;
	case Register:
	case Counter:
	case Ram:
	case Rom:
		p.width = 4;
		break;
	default:
		break;
	}
	if (kind == Text) {
		p.text = "Text";
	}
	if (kind == Tunnel) {
		p.text = "A";
	}
	return p;
}

void sanitize(Kind kind, Props& p) {
	p.inputs = std::clamp(p.inputs, 2, 8);
	p.width = std::clamp(p.width, 1, MAX_WIDTH);
	p.selectBits = std::clamp(p.selectBits, 1, 5);
	p.addressBits = std::clamp(p.addressBits, 1, 16);
	p.period = std::clamp(p.period, 2, 1 << 20);
	p.period += p.period % 2;
	unsigned flags = info(kind).props;
	p.delay = (flags & P_DELAY) ? std::clamp(p.delay, 1, 1000) : 0;
	if (!(flags & P_WIDTH)) {
		p.width = 1;
	}
	if (kind == HexDisplay) {
		p.width = 4;
	}
	if (kind == Switch) {
		p.value &= 1;
	}
	p.value &= p.width >= 32 ? 0xFFFFFFFFu : (std::uint32_t{1} << p.width) - 1;
	if (kind == Splitter || kind == Merger) {
		p.width = std::max(p.width, 2);
		std::vector<int> parts = partWidths(p);
		p.parts = std::ranges::all_of(parts, [](int w) { return w == 1; }) ? std::vector<int>{} : parts;
	}
	if (kind == Rom) {
		p.data.resize(size_t{1} << p.addressBits, 0);
		for (std::uint32_t& word : p.data) {
			word &= p.width >= 32 ? 0xFFFFFFFFu : (std::uint32_t{1} << p.width) - 1;
		}
	} else {
		p.data.clear();
	}
}

std::vector<int> partWidths(const Props& p) {
	int width = std::max(p.width, 1);
	bool valid = !p.parts.empty() && std::ranges::all_of(p.parts, [](int w) { return w >= 1; }) &&
	             std::accumulate(p.parts.begin(), p.parts.end(), 0) == width;
	return valid ? p.parts : std::vector<int>(static_cast<size_t>(width), 1);
}

int ComponentSpec::pinIndex(std::string_view name) const {
	for (size_t i = 0; i < pins.size(); i++) {
		if (pins[i].name == name) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

std::vector<int> gateRows(int n) {
	std::vector<int> rows;
	for (int i = 0; i < n; i++) {
		int row = i + 1;
		if (n % 2 == 0 && i >= n / 2) {
			row++; // skip the middle row
		}
		rows.push_back(row);
	}
	return rows;
}

int gateHeight(int n) {
	return n % 2 == 0 ? n + 2 : n + 1;
}

ComponentSpec builtinSpec(Kind kind, const Props& p) {
	switch (kind) {
	case And:
	case Or:
	case Nand:
	case Nor:
	case Xor:
	case Xnor:
		return gate(p, false);
	case Not:
	case Buffer:
		return gate(p, true);

	case Switch:
	case Button:
	case Clock:
		return {2, 2, {out("out", 1, 2, 1)}};
	case Constant:
		return {2, 2, {out("out", p.width, 2, 1)}};
	case InputPin: {
		int w = std::max(2, p.width);
		return {w, 2, {out("out", p.width, w, 1)}};
	}
	case Led:
		return {2, 2, {in("in", 1, 0, 1)}};
	case OutputPin: {
		int w = std::max(2, p.width);
		return {w, 2, {in("in", p.width, 0, 1)}};
	}
	case Probe:
		return {4, 2, {in("in", p.width, 0, 1)}};
	case SevenSegment: {
		ComponentSpec spec{5, 9, {}};
		const char* names[] = {"a", "b", "c", "d", "e", "f", "g", "dp"};
		for (int i = 0; i < 8; i++) {
			spec.pins.push_back(in(names[i], 1, 0, i + 1));
		}
		return spec;
	}
	case HexDisplay:
		return {4, 6, {in("in", 4, 0, 3)}};

	case Splitter:
	case Merger: {
		std::vector<int> parts = partWidths(p);
		int n = static_cast<int>(parts.size());
		ComponentSpec spec{2, n + 1, {}};
		if (kind == Splitter) {
			spec.pins.push_back(in("in", p.width, 0, 1));
		}
		int low = 0;
		for (int i = 0; i < n; i++) {
			int high = low + parts[i] - 1;
			std::string name = parts[i] == 1 ? std::format("{}", low) : std::format("{}-{}", low, high);
			spec.pins.push_back(kind == Splitter ? out(name, parts[i], 2, i + 1)
			                                     : in(name, parts[i], 0, i + 1));
			low = high + 1;
		}
		if (kind == Merger) {
			spec.pins.push_back(out("out", p.width, 2, 1));
		}
		return spec;
	}
	case Tunnel:
		return {3, 2, {in("io", p.width, 0, 1)}};
	case Text: {
		// Big enough for the text (drawn 0.7 units tall, on average under 0.3 units per character)
		// so all of it can be clicked
		int lines = 1, longest = 0, length = 0;
		for (char ch : p.text) {
			if (ch == '\n') {
				lines++;
				length = 0;
			} else if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) { // UTF-8: count characters
				longest = std::max(longest, ++length);
			}
		}
		return {std::max(2, (longest * 3 + 9) / 10 + 1), std::max(2, lines + 1), {}};
	}

	case Mux:
	case Demux:
	case Decoder:
	case PriorityEncoder: {
		int n = 1 << p.selectBits;
		ComponentSpec spec{4, gateHeight(n), {}};
		std::vector<int> rows = gateRows(n);
		int mid = spec.h / 2;
		if (kind == Mux) {
			for (int i = 0; i < n; i++) {
				spec.pins.push_back(in(std::format("d{}", i), p.width, 0, rows[i]));
			}
			spec.pins.push_back(in("sel", p.selectBits, 2, spec.h));
			spec.pins.push_back(out("out", p.width, 4, mid));
		} else if (kind == Demux) {
			spec.pins.push_back(in("in", p.width, 0, mid));
			spec.pins.push_back(in("sel", p.selectBits, 2, spec.h));
			for (int i = 0; i < n; i++) {
				spec.pins.push_back(out(std::format("o{}", i), p.width, 4, rows[i]));
			}
		} else if (kind == Decoder) {
			spec.pins.push_back(in("sel", p.selectBits, 0, mid));
			for (int i = 0; i < n; i++) {
				spec.pins.push_back(out(std::format("o{}", i), 1, 4, rows[i]));
			}
		} else {
			for (int i = 0; i < n; i++) {
				spec.pins.push_back(in(std::format("i{}", i), 1, 0, rows[i]));
			}
			spec.pins.push_back(out("out", p.selectBits, 4, mid));
			spec.pins.push_back(out("V", 1, 4, mid + 1));
		}
		return spec;
	}

	case SrLatch: {
		ComponentSpec spec{4, 4, {in("S", 1, 0, 1), in("R", 1, 0, 3)}};
		flipFlopOutputs(spec, false);
		return spec;
	}
	case DLatch: {
		ComponentSpec spec{4, 4, {in("D", 1, 0, 1), in("EN", 1, 0, 3)}};
		flipFlopOutputs(spec, false);
		return spec;
	}
	case DFlipFlop: {
		ComponentSpec spec{4, 4, {in("D", 1, 0, 1), in("CLK", 1, 0, 3)}};
		flipFlopOutputs(spec, true);
		return spec;
	}
	case JkFlipFlop: {
		ComponentSpec spec{4, 4, {in("J", 1, 0, 1), in("CLK", 1, 0, 2), in("K", 1, 0, 3)}};
		flipFlopOutputs(spec, true);
		return spec;
	}
	case TFlipFlop: {
		ComponentSpec spec{4, 4, {in("T", 1, 0, 1), in("CLK", 1, 0, 3)}};
		flipFlopOutputs(spec, true);
		return spec;
	}

	case Register:
		return {4,
		        4,
		        {in("D", p.width, 0, 1), in("EN", 1, 0, 2, 1), in("CLK", 1, 0, 3), in("RST", 1, 2, 4, 0),
		         out("Q", p.width, 4, 1)}};
	case Counter:
		return {4,
		        5,
		        {in("D", p.width, 0, 1, 0), in("LD", 1, 0, 2, 0), in("EN", 1, 0, 3, 1), in("CLK", 1, 0, 4),
		         in("RST", 1, 2, 5, 0), out("Q", p.width, 4, 1), out("CO", 1, 4, 4)}};
	case Ram:
		return {5,
		        5,
		        {in("A", p.addressBits, 0, 1), in("D", p.width, 0, 2, 0), in("WE", 1, 0, 3, 0),
		         in("CLK", 1, 0, 4), out("Q", p.width, 5, 1)}};
	case Rom:
		return {4, 3, {in("A", p.addressBits, 0, 1), out("Q", p.width, 4, 1)}};

	case Chip:
		break;
	}
	return {};
}

} // namespace logic
