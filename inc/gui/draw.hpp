#pragma once

// Drawing the circuit onto an ImGui draw list: the grid, gate shapes, boxes, displays and wires,
// coloured by the live signal values.
#include "core/value.hpp"
#include "gui/geometry.hpp"

#include <functional>

namespace gui {

namespace colors {
inline constexpr ImU32 CANVAS = IM_COL32(22, 24, 30, 255);
inline constexpr ImU32 GRID_DOT = IM_COL32(52, 57, 70, 255);
inline constexpr ImU32 BODY = IM_COL32(40, 45, 58, 255);
inline constexpr ImU32 OUTLINE = IM_COL32(150, 160, 182, 255);
inline constexpr ImU32 TEXT = IM_COL32(220, 224, 235, 255);
inline constexpr ImU32 MUTED = IM_COL32(140, 148, 168, 255);
inline constexpr ImU32 SELECTED = IM_COL32(120, 165, 255, 255);
inline constexpr ImU32 ERROR = IM_COL32(240, 80, 80, 255);
inline constexpr ImU32 ZERO = IM_COL32(36, 110, 52, 255);
inline constexpr ImU32 ONE = IM_COL32(90, 235, 115, 255);
inline constexpr ImU32 UNKNOWN = IM_COL32(85, 125, 235, 255);
inline constexpr ImU32 PARTLY = IM_COL32(200, 170, 80, 255);
} // namespace colors

// Colour of a wire or pin carrying this value
ImU32 valueColor(logic::Value value);
// A colour from "#RRGGBB" (or the fallback)
ImU32 parseColor(const std::string& hex, ImU32 fallback);

struct Look {
	bool selected = false;
	bool error = false;
	bool ghost = false; // the component being placed, half transparent
	// The value on each pin (nullptr while there is no simulation)
	std::function<logic::Value(int pin)> pinValue;
};

// Font for text on the canvas (nullptr: ImGui's current font)
void setCanvasFont(ImFont* font);

void drawGrid(ImDrawList* list, const Camera& camera, ImVec2 min, ImVec2 max);
void drawComponent(ImDrawList* list, const Camera& camera, const Component& c, const ComponentSpec& spec,
                   const Look& look, const Library& library);
void drawWire(ImDrawList* list, const Camera& camera, const std::vector<Point>& path, ImU32 color,
              float thickness);

} // namespace gui
