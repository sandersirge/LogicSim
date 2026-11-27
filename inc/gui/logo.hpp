#pragma once

// The LogicSim logo, drawn in code: an AND gate with lit (green) input and output wires on a dark
// rounded square, the colours of the canvas. Used for the window icon and the About dialog; the
// icons tool (tools/make_icons.cpp) writes the icon files in res/ from it.
#include <cstdint>
#include <vector>

namespace gui {

// size x size RGBA pixels, rows from the top, transparent outside the rounded square
std::vector<std::uint8_t> drawLogo(unsigned size);

} // namespace gui
