#include "gui/geometry.hpp"

#include <algorithm>
#include <cmath>

namespace gui {

void Camera::zoomAt(ImVec2 screen, float factor) {
	ImVec2 world = toWorld(screen);
	zoom = std::clamp(zoom * factor, 0.2f, 5.f);
	offset = {screen.x - world.x * scale(), screen.y - world.y * scale()};
}

ImVec2 localToWorld(const Component& c, const ComponentSpec& spec, float x, float y) {
	float w = static_cast<float>(spec.w), h = static_cast<float>(spec.h);
	float rx = x, ry = y;
	switch (c.rotation & 3) {
	case 1:
		rx = h - y;
		ry = x;
		break;
	case 2:
		rx = w - x;
		ry = h - y;
		break;
	case 3:
		rx = y;
		ry = w - x;
		break;
	default:
		break;
	}
	return {static_cast<float>(c.pos.x) + rx, static_cast<float>(c.pos.y) + ry};
}

ImVec2 rotatedSize(const Component& c, const ComponentSpec& spec) {
	bool turned = (c.rotation & 1) != 0;
	return {static_cast<float>(turned ? spec.h : spec.w), static_cast<float>(turned ? spec.w : spec.h)};
}

Point pinPosition(const Component& c, const ComponentSpec& spec, int pin) {
	const logic::PinSpec& p = spec.pins[static_cast<size_t>(pin)];
	ImVec2 world = localToWorld(c, spec, static_cast<float>(p.x), static_cast<float>(p.y));
	return Point{static_cast<int>(std::lround(world.x)), static_cast<int>(std::lround(world.y))};
}

std::vector<Point> wirePath(Point from, Point to, const std::vector<Point>& bends) {
	std::vector<Point> corners{from};
	if (bends.empty()) {
		if (from.x != to.x && from.y != to.y) {
			int middle = (from.x + to.x) / 2;
			corners.push_back({middle, from.y});
			corners.push_back({middle, to.y});
		}
	} else {
		for (Point b : bends) {
			Point last = corners.back();
			if (last.x != b.x && last.y != b.y) {
				corners.push_back({b.x, last.y});
			}
			corners.push_back(b);
		}
		Point last = corners.back();
		if (last.x != to.x && last.y != to.y) {
			corners.push_back({last.x, to.y});
		}
	}
	corners.push_back(to);
	return corners;
}

std::vector<Point> wirePath(const Circuit& circuit, const Library& library, const Wire& wire) {
	const Component* a = circuit.find(wire.from.component);
	const Component* b = circuit.find(wire.to.component);
	if (a == nullptr || b == nullptr) {
		return {};
	}
	ComponentSpec sa = logic::specOf(*a, library), sb = logic::specOf(*b, library);
	if (wire.from.pin >= static_cast<int>(sa.pins.size()) ||
	    wire.to.pin >= static_cast<int>(sb.pins.size())) {
		return {};
	}
	return wirePath(pinPosition(*a, sa, wire.from.pin), pinPosition(*b, sb, wire.to.pin), wire.bends);
}

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
	float dx = b.x - a.x, dy = b.y - a.y;
	float length = dx * dx + dy * dy;
	float t = length > 0.f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0.f, 1.f) : 0.f;
	float cx = a.x + t * dx - p.x, cy = a.y + t * dy - p.y;
	return std::sqrt(cx * cx + cy * cy);
}

} // namespace gui
