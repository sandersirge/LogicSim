#pragma once

// Where components, pins and wires are on the grid, and the camera that maps grid units to pixels.
#include "core/circuit.hpp"

#include <imgui.h>

#include <vector>

namespace gui {

using logic::Circuit;
using logic::Component;
using logic::ComponentSpec;
using logic::Library;
using logic::PinRef;
using logic::Point;
using logic::Wire;

// Pixels per grid unit at zoom 1
inline constexpr float GRID = 20.f;

struct Camera {
	ImVec2 offset{0.f, 0.f}; // screen position of grid point (0, 0)
	float zoom = 1.f;

	float scale() const { return GRID * zoom; }
	ImVec2 toScreen(ImVec2 world) const {
		return {offset.x + world.x * scale(), offset.y + world.y * scale()};
	}
	ImVec2 toScreen(Point p) const {
		return toScreen(ImVec2{static_cast<float>(p.x), static_cast<float>(p.y)});
	}
	ImVec2 toWorld(ImVec2 screen) const {
		return {(screen.x - offset.x) / scale(), (screen.y - offset.y) / scale()};
	}
	// Zoom around a screen point, which stays where it is
	void zoomAt(ImVec2 screen, float factor);
};

// A point in the component's own layout (facing east) -> grid position, with rotation applied
ImVec2 localToWorld(const Component& c, const ComponentSpec& spec, float x, float y);
// Size on the grid after rotation
ImVec2 rotatedSize(const Component& c, const ComponentSpec& spec);
Point pinPosition(const Component& c, const ComponentSpec& spec, int pin);

// The corner points of a wire from pin to pin: its bends, with an L-shaped (or Z-shaped, without
// bends) route so every segment is horizontal or vertical
std::vector<Point> wirePath(Point from, Point to, const std::vector<Point>& bends);
std::vector<Point> wirePath(const Circuit& circuit, const Library& library, const Wire& wire);

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b);

struct Segment {
	Point a, b;
};

// Stretches where wires of different signals run on top of each other along a grid line: they
// look connected but are not. paths[i] is the route of wire i (see wirePath), nets[i] its signal.
std::vector<Segment> wireOverlaps(const std::vector<std::vector<Point>>& paths, const std::vector<int>& nets);

// The bends for a new wire from pin `from` to pin `to` that was drawn without any: empty when the
// usual route is fine, else a route through another column that stays off the wires of other
// signals (`others`): it does not run along them, has no corner on them and they have none on it.
// It does not pass through the `avoid` points either (pins of other signals). Empty as well when
// no column works.
std::vector<Point> routeAround(Point from, Point to, const std::vector<std::vector<Point>>& others,
                               const std::vector<Point>& avoid = {});

} // namespace gui
