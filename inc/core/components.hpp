#pragma once

// The built-in component kinds, their settings (Props) and their pin layout. The layout is in grid
// units with the component facing east: inputs on the left edge (x = 0), outputs on the right edge
// (x = w), control pins on the top (y = 0) or bottom (y = h) edge. The GUI rotates it; the netlist
// compiler only needs the pins' order, direction and width.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace logic {

enum class Kind {
	// Gates
	And,
	Or,
	Not,
	Nand,
	Nor,
	Xor,
	Xnor,
	Buffer,
	// Inputs and outputs
	Switch,
	Button,
	Constant,
	Clock,
	Led,
	SevenSegment,
	HexDisplay,
	Probe,
	InputPin,
	OutputPin,
	// Wiring
	Splitter,
	Merger,
	Tunnel,
	Text,
	// Plexers
	Mux,
	Demux,
	Decoder,
	PriorityEncoder,
	// Flip-flops and latches
	SrLatch,
	DLatch,
	DFlipFlop,
	JkFlipFlop,
	TFlipFlop,
	// Memory
	Register,
	Counter,
	Ram,
	Rom,
	// An instance of a custom chip (Props::chip names it)
	Chip,
};

enum class Category { Gates, InputOutput, Wiring, Plexers, FlipFlops, Memory, Chips };

// Settings of a component. Each kind uses some of them (see KindInfo); the others keep their
// defaults and are not saved.
struct Props {
	int inputs = 2;                  // gates: number of inputs (2..8)
	int width = 1;                   // data width in bits (1..32)
	int delay = 1;                   // propagation delay in ticks
	int period = 8;                  // clock: ticks per full cycle (even, >= 2)
	int selectBits = 1;              // mux, demux, decoder, priority encoder (1..5)
	int addressBits = 4;             // RAM, ROM (1..16)
	std::uint32_t value = 0;         // constant value; switch and input pin state
	std::string color;               // LED and chip colour, "#RRGGBB" (empty = default)
	std::string text;                // tunnel name, text label
	std::string chip;                // chip kind name
	std::vector<int> parts;          // splitter / merger part widths, low bits first (empty = all 1)
	std::vector<std::uint32_t> data; // ROM contents

	friend bool operator==(const Props&, const Props&) = default;
};

// Which settings a kind has (for the properties panel and for saving)
enum PropFlag : unsigned {
	P_INPUTS = 1 << 0,
	P_WIDTH = 1 << 1,
	P_DELAY = 1 << 2,
	P_PERIOD = 1 << 3,
	P_SELECT = 1 << 4,
	P_ADDRESS = 1 << 5,
	P_VALUE = 1 << 6,
	P_COLOR = 1 << 7,
	P_TEXT = 1 << 8,
	P_PARTS = 1 << 9,
	P_DATA = 1 << 10,
};

struct KindInfo {
	Kind kind;
	std::string_view id;   // name in files: "and", "d_flip_flop"
	std::string_view name; // shown in the toolbox: "AND", "D flip-flop"
	Category category;
	unsigned props;           // PropFlag bits
	std::string_view tooltip; // one-line description
};

const std::vector<KindInfo>& allKinds();
const KindInfo& info(Kind kind);
std::optional<Kind> kindFromId(std::string_view id);
std::string_view categoryName(Category category);

// Default settings for a newly placed component of this kind
Props defaultProps(Kind kind);
// Clamp settings into their valid ranges (after loading or editing)
void sanitize(Kind kind, Props& props);
// Splitter/merger part widths (Props::parts or, when empty or not adding up, all 1)
std::vector<int> partWidths(const Props& props);

struct PinSpec {
	std::string name;
	bool output = false;
	int width = 1;
	int x = 0;
	int y = 0;
	// Value used when nothing is connected (optional control pins); nullopt = the input reads X
	std::optional<std::uint32_t> floating;
};

struct ComponentSpec {
	int w = 2;
	int h = 2;
	std::vector<PinSpec> pins;

	int pinIndex(std::string_view name) const;
};

// Layout of a built-in kind (not Kind::Chip, which needs its chip definition: see circuit.hpp)
ComponentSpec builtinSpec(Kind kind, const Props& props);

// Input rows of a gate-shaped component with n inputs: 1, 2, ... with an empty middle row when n
// is even, so the output (at height / 2) lines up with a grid row.
std::vector<int> gateRows(int n);
int gateHeight(int n);

} // namespace logic
