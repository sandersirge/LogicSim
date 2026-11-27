#include "gui/logo.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace gui {

namespace {

struct Rgba {
	float r, g, b, a;
};

// The colours of the canvas (see gui/draw.hpp)
constexpr Rgba CANVAS{22, 24, 30, 255};
constexpr Rgba RIM{62, 68, 86, 255};
constexpr Rgba BODY{40, 45, 58, 255};
constexpr Rgba OUTLINE{150, 160, 182, 255};
constexpr Rgba ONE{90, 235, 115, 255};

// The drawing in a unit square, (0, 0) top left
constexpr float CORNER = 0.21f;     // corner radius of the background
constexpr float RIM_WIDTH = 0.025f; // its lighter edge
// Line thicknesses: in the unit square, but at least about 2 pixels so small icons stay crisp
struct Lines {
	float wire; // wire thickness
	float pin;  // radius of the dots at the wire ends
	float line; // gate outline thickness
};
// The AND gate: a flat back and a half-disc front
constexpr float BACK = 0.30f, TOP = 0.26f, BOTTOM = 0.74f;
constexpr float RADIUS = (BOTTOM - TOP) / 2, CENTER_X = 0.46f, CENTER_Y = 0.5f;
constexpr float IN1 = 0.38f, IN2 = 0.62f, WIRE_START = 0.12f, WIRE_END = 0.88f;

bool insideRoundedSquare(float x, float y, float inset) {
	float lo = inset, hi = 1.f - inset, r = std::max(CORNER - inset, 0.f);
	if (x < lo || x > hi || y < lo || y > hi) {
		return false;
	}
	float cx = std::clamp(x, lo + r, hi - r), cy = std::clamp(y, lo + r, hi - r);
	return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

// The gate grown by d (negative: shrunk)
bool insideGate(float x, float y, float d) {
	if (y < TOP - d || y > BOTTOM + d || x < BACK - d) {
		return false;
	}
	if (x <= CENTER_X) {
		return true;
	}
	float dx = x - CENTER_X, dy = y - CENTER_Y;
	return dx * dx + dy * dy <= (RADIUS + d) * (RADIUS + d);
}

bool onWire(float x, float y, float wireY, float from, float to, const Lines& l) {
	return x >= from && x <= to && std::abs(y - wireY) <= l.wire / 2;
}

bool inDot(float x, float y, float cx, float cy, const Lines& l) {
	return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= l.pin * l.pin;
}

// The colour at one point (the topmost layer that covers it)
Rgba colorAt(float x, float y, const Lines& l) {
	if (insideGate(x, y, -l.line / 2)) {
		return BODY;
	}
	if (insideGate(x, y, l.line / 2)) {
		return OUTLINE;
	}
	if (onWire(x, y, IN1, WIRE_START, BACK, l) || onWire(x, y, IN2, WIRE_START, BACK, l) ||
	    onWire(x, y, CENTER_Y, CENTER_X + RADIUS, WIRE_END, l) || inDot(x, y, WIRE_START, IN1, l) ||
	    inDot(x, y, WIRE_START, IN2, l) || inDot(x, y, WIRE_END, CENTER_Y, l)) {
		return ONE;
	}
	if (insideRoundedSquare(x, y, RIM_WIDTH)) {
		return CANVAS;
	}
	if (insideRoundedSquare(x, y, 0.f)) {
		return RIM;
	}
	return {0, 0, 0, 0};
}

} // namespace

std::vector<std::uint8_t> drawLogo(unsigned size) {
	std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4, 0);
	if (size == 0) {
		return pixels;
	}
	// Each pixel is the average of n x n samples (antialiasing); small icons get more of them
	const int n = size <= 64 ? 8 : 4;
	const float step = 1.f / static_cast<float>(size);
	const Lines lines{std::max(0.06f, 2.f * step), std::max(0.05f, 1.2f * step),
	                  std::max(0.045f, 1.4f * step)};
	for (unsigned py = 0; py < size; py++) {
		for (unsigned px = 0; px < size; px++) {
			// Premultiplied sums, so transparent samples do not darken the edges
			std::array<float, 4> sum{};
			for (int sy = 0; sy < n; sy++) {
				for (int sx = 0; sx < n; sx++) {
					float x =
					    (static_cast<float>(px) + (static_cast<float>(sx) + 0.5f) / static_cast<float>(n)) *
					    step;
					float y =
					    (static_cast<float>(py) + (static_cast<float>(sy) + 0.5f) / static_cast<float>(n)) *
					    step;
					Rgba c = colorAt(x, y, lines);
					float a = c.a / 255.f;
					sum[0] += c.r * a;
					sum[1] += c.g * a;
					sum[2] += c.b * a;
					sum[3] += a;
				}
			}
			std::uint8_t* out = &pixels[(static_cast<std::size_t>(py) * size + px) * 4];
			float coverage = sum[3] / static_cast<float>(n * n);
			if (sum[3] > 0.f) {
				for (int k = 0; k < 3; k++) {
					out[k] = static_cast<std::uint8_t>(std::lround(std::clamp(sum[k] / sum[3], 0.f, 255.f)));
				}
			}
			out[3] = static_cast<std::uint8_t>(std::lround(coverage * 255.f));
		}
	}
	return pixels;
}

} // namespace gui
