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

std::vector<Segment> wireOverlaps(const std::vector<std::vector<Point>>& paths,
                                  const std::vector<int>& nets) {
	// Every straight piece of every wire, grouped by the grid line it lies on
	struct Run {
		int line, lo, hi, net;
	};
	std::vector<Run> horizontal, vertical;
	for (size_t i = 0; i < paths.size() && i < nets.size(); i++) {
		const std::vector<Point>& path = paths[i];
		for (size_t k = 1; k < path.size(); k++) {
			Point a = path[k - 1], b = path[k];
			if (a.y == b.y && a.x != b.x) {
				horizontal.push_back({a.y, std::min(a.x, b.x), std::max(a.x, b.x), nets[i]});
			} else if (a.x == b.x && a.y != b.y) {
				vertical.push_back({a.x, std::min(a.y, b.y), std::max(a.y, b.y), nets[i]});
			}
		}
	}
	std::vector<Segment> overlaps;
	auto scan = [&](std::vector<Run>& runs, bool across) {
		std::ranges::sort(runs, [](const Run& r, const Run& s) {
			return r.line != s.line ? r.line < s.line : r.lo < s.lo;
		});
		for (size_t i = 0; i < runs.size(); i++) {
			const Run& r = runs[i];
			for (size_t j = i + 1; j < runs.size() && runs[j].line == r.line && runs[j].lo < r.hi; j++) {
				const Run& s = runs[j];
				if (s.net == r.net) {
					continue;
				}
				int lo = s.lo, hi = std::min(r.hi, s.hi);
				overlaps.push_back(across ? Segment{{lo, r.line}, {hi, r.line}}
				                          : Segment{{r.line, lo}, {r.line, hi}});
			}
		}
	};
	scan(horizontal, true);
	scan(vertical, false);
	return overlaps;
}

namespace {

// On the horizontal or vertical segment a-b, ends included
bool onSegment(Point p, Point a, Point b) {
	if (a.x == b.x) {
		return p.x == a.x && p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
	}
	return p.y == a.y && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x);
}

// Both on the same grid line and sharing more than a point
bool alongside(Point a, Point b, Point c, Point d) {
	if (a.y == b.y && c.y == d.y && a.y == c.y && a.x != b.x && c.x != d.x) {
		return std::min(std::max(a.x, b.x), std::max(c.x, d.x)) >
		       std::max(std::min(a.x, b.x), std::min(c.x, d.x));
	}
	if (a.x == b.x && c.x == d.x && a.x == c.x && a.y != b.y && c.y != d.y) {
		return std::min(std::max(a.y, b.y), std::max(c.y, d.y)) >
		       std::max(std::min(a.y, b.y), std::min(c.y, d.y));
	}
	return false;
}

// Corners of a path (its points apart from the two ends) that lie on the other path
bool cornerOn(const std::vector<Point>& path, const std::vector<Point>& other) {
	for (size_t i = 1; i + 1 < path.size(); i++) {
		for (size_t k = 1; k < other.size(); k++) {
			if (onSegment(path[i], other[k - 1], other[k])) {
				return true;
			}
		}
	}
	return false;
}

bool clearOf(const std::vector<Point>& path, const std::vector<std::vector<Point>>& others,
             const std::vector<Point>& avoid) {
	for (Point p : avoid) {
		for (size_t i = 1; i < path.size(); i++) {
			if (onSegment(p, path[i - 1], path[i])) {
				return false;
			}
		}
	}
	for (const std::vector<Point>& other : others) {
		for (size_t i = 1; i < path.size(); i++) {
			for (size_t k = 1; k < other.size(); k++) {
				if (alongside(path[i - 1], path[i], other[k - 1], other[k])) {
					return false;
				}
			}
		}
		if (cornerOn(path, other) || cornerOn(other, path)) {
			return false;
		}
	}
	return true;
}

} // namespace

std::vector<Point> routeAround(Point from, Point to, const std::vector<std::vector<Point>>& others,
                               const std::vector<Point>& avoid) {
	if (from.x == to.x || from.y == to.y || clearOf(wirePath(from, to, {}), others, avoid)) {
		return {};
	}
	// The other columns between the two pins, nearest to the middle first
	int middle = (from.x + to.x) / 2;
	std::vector<int> columns;
	for (int x = std::min(from.x, to.x) + 1; x < std::max(from.x, to.x); x++) {
		if (x != middle) {
			columns.push_back(x);
		}
	}
	std::ranges::stable_sort(columns, {}, [&](int x) { return std::abs(x - middle); });
	for (int x : columns) {
		std::vector<Point> bends{{x, from.y}, {x, to.y}};
		if (clearOf(wirePath(from, to, bends), others, avoid)) {
			return bends;
		}
	}
	return {};
}

} // namespace gui
