#include "gui/draw.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numbers>
#include <string>

namespace gui {

using logic::Kind;
using logic::Value;

ImU32 valueColor(Value v) {
	if (v.isX()) {
		return colors::UNKNOWN;
	}
	if (!v.fullyKnown()) {
		return colors::PARTLY;
	}
	return v.bits != 0 ? colors::ONE : colors::ZERO;
}

ImU32 parseColor(const std::string& hex, ImU32 fallback) {
	if (hex.size() != 7 || hex[0] != '#') {
		return fallback;
	}
	try {
		unsigned long rgb = std::stoul(hex.substr(1), nullptr, 16);
		return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
	} catch (...) {
		return fallback;
	}
}

namespace {

ImFont* canvasFont = nullptr;

ImU32 withAlpha(ImU32 color, float alpha) {
	ImU32 a = static_cast<ImU32>(static_cast<float>((color >> IM_COL32_A_SHIFT) & 255) * alpha);
	return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 dim(ImU32 color, float factor) {
	auto channel = [&](int shift) {
		return static_cast<ImU32>(static_cast<float>((color >> shift) & 255) * factor) << shift;
	};
	return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) |
	       (color & IM_COL32_A_MASK);
}

// Draws one component in its own layout coordinates; every point goes through the rotation
struct Painter {
	ImDrawList* list;
	const Camera& camera;
	const Component& c;
	const ComponentSpec& spec;
	const Look& look;
	float alpha;

	float w() const { return static_cast<float>(spec.w); }
	float h() const { return static_cast<float>(spec.h); }
	float px(float units) const { return units * camera.scale(); }
	ImVec2 at(float x, float y) const { return camera.toScreen(localToWorld(c, spec, x, y)); }
	ImU32 col(ImU32 color) const { return withAlpha(color, alpha); }
	ImU32 outline() const {
		return col(look.error ? colors::ERROR : look.selected ? colors::SELECTED : colors::OUTLINE);
	}
	float lineWidth() const { return std::max(1.f, px(look.selected ? 0.11f : 0.08f)); }

	Value pin(int index) const {
		return look.pinValue && index >= 0 ? look.pinValue(index)
		                                   : Value::x(spec.pins[static_cast<size_t>(index)].width);
	}
	Value pin(std::string_view name) const { return pin(spec.pinIndex(name)); }

	void polygon(const std::vector<ImVec2>& local, ImU32 fill) const {
		std::vector<ImVec2> points;
		points.reserve(local.size());
		for (ImVec2 p : local) {
			points.push_back(at(p.x, p.y));
		}
		list->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), col(fill));
		list->AddPolyline(points.data(), static_cast<int>(points.size()), outline(), ImDrawFlags_Closed,
		                  lineWidth());
	}

	void line(float x1, float y1, float x2, float y2, ImU32 color, float thickness) const {
		list->AddLine(at(x1, y1), at(x2, y2), col(color), std::max(1.f, px(thickness)));
	}

	// Axis-aligned on screen whatever the rotation
	void box(float x1, float y1, float x2, float y2, ImU32 fill, float rounding = 0.25f,
	         bool border = true) const {
		ImVec2 a = at(x1, y1), b = at(x2, y2);
		ImVec2 lo{std::min(a.x, b.x), std::min(a.y, b.y)}, hi{std::max(a.x, b.x), std::max(a.y, b.y)};
		list->AddRectFilled(lo, hi, col(fill), px(rounding));
		if (border) {
			list->AddRect(lo, hi, outline(), px(rounding), 0, lineWidth());
		}
	}

	void circle(float x, float y, float r, ImU32 fill, bool border = true) const {
		list->AddCircleFilled(at(x, y), px(r), col(fill));
		if (border) {
			list->AddCircle(at(x, y), px(r), outline(), 0, lineWidth());
		}
	}

	// Text centred on a layout point (never rotated); size in grid units
	void text(float x, float y, std::string_view s, float size, ImU32 color) const {
		textAt(at(x, y), s, size, color);
	}
	void textAt(ImVec2 center, std::string_view s, float size, ImU32 color) const {
		float pixels = px(size);
		if (pixels < 6.f || s.empty()) {
			return;
		}
		ImFont* font = canvasFont != nullptr ? canvasFont : ImGui::GetFont();
		ImVec2 extent = font->CalcTextSizeA(pixels, FLT_MAX, 0.f, s.data(), s.data() + s.size());
		list->AddText(font, pixels, ImVec2{center.x - extent.x / 2, center.y - extent.y / 2}, col(color),
		              s.data(), s.data() + s.size());
	}

	// Pin names just inside the edge they sit on
	void pinLabels(float size = 0.5f) const {
		for (const logic::PinSpec& p : spec.pins) {
			float x = static_cast<float>(p.x), y = static_cast<float>(p.y);
			float dx = p.x == 0 ? 1.f : p.x == spec.w ? -1.f : 0.f;
			float dy = dx != 0.f ? 0.f : p.y == 0 ? 1.f : -1.f;
			float inset = dx != 0.f ? 0.2f + size * 0.32f * static_cast<float>(p.name.size()) : 0.45f;
			text(x + dx * inset, y + dy * inset, p.name, size, colors::MUTED);
		}
	}

	void pins() const {
		for (size_t i = 0; i < spec.pins.size(); i++) {
			const logic::PinSpec& p = spec.pins[i];
			ImU32 color = look.pinValue ? valueColor(pin(static_cast<int>(i))) : colors::OUTLINE;
			circle(static_cast<float>(p.x), static_cast<float>(p.y), p.width > 1 ? 0.16f : 0.12f, color,
			       false);
		}
	}

	void clockMarks() const {
		for (const logic::PinSpec& p : spec.pins) {
			if (p.name == "CLK" && p.x == 0) {
				float y = static_cast<float>(p.y);
				std::vector<ImVec2> tri{at(0.f, y - 0.3f), at(0.4f, y), at(0.f, y + 0.3f)};
				list->AddPolyline(tri.data(), 3, outline(), 0, lineWidth());
			}
		}
	}
};

// Quadratic Bezier points from a to b with control point q (without a)
void quadratic(std::vector<ImVec2>& out, ImVec2 a, ImVec2 q, ImVec2 b, int steps = 12) {
	for (int i = 1; i <= steps; i++) {
		float t = static_cast<float>(i) / static_cast<float>(steps), u = 1.f - t;
		out.push_back(
		    {u * u * a.x + 2 * u * t * q.x + t * t * b.x, u * u * a.y + 2 * u * t * q.y + t * t * b.y});
	}
}

void drawGate(const Painter& p) {
	Kind kind = p.c.kind;
	bool inverted = kind == Kind::Nand || kind == Kind::Nor || kind == Kind::Xnor;
	float top = 0.3f, bottom = p.h() - 0.3f, mid = p.h() / 2;
	float back = (kind == Kind::Xor || kind == Kind::Xnor) ? 0.65f : 0.3f;
	float front = inverted ? p.w() - 0.5f : p.w();
	std::vector<ImVec2> shape;
	bool curvedBack = kind != Kind::And && kind != Kind::Nand;
	if (!curvedBack) {
		// Flat back, half-ellipse front
		float ry = (bottom - top) / 2, rx = std::min(ry, (front - back) * 0.6f);
		shape.push_back({back, top});
		for (int i = 0; i <= 16; i++) {
			float angle =
			    -std::numbers::pi_v<float> / 2 + std::numbers::pi_v<float> * static_cast<float>(i) / 16.f;
			shape.push_back({front - rx + rx * std::cos(angle), mid + ry * std::sin(angle)});
		}
		shape.push_back({back, bottom});
	} else {
		float span = front - back;
		shape.push_back({back, top});
		quadratic(shape, {back, top}, {back + span * 0.6f, top}, {front, mid});
		quadratic(shape, {front, mid}, {back + span * 0.6f, bottom}, {back, bottom});
		quadratic(shape, {back, bottom}, {back + 0.8f, mid}, {back, top});
		shape.pop_back();
	}
	// Input stubs from the pins to the body
	for (const logic::PinSpec& pin : p.spec.pins) {
		if (pin.output) {
			continue;
		}
		float y = static_cast<float>(pin.y);
		float t = (y - top) / (bottom - top);
		float x = curvedBack ? back + 1.6f * t * (1 - t) : back;
		p.line(0.f, y, x, y, valueColor(p.pin(pin.name)), 0.08f);
	}
	p.polygon(shape, colors::BODY);
	if (kind == Kind::Xor || kind == Kind::Xnor) {
		std::vector<ImVec2> extra{p.at(0.3f, top)};
		std::vector<ImVec2> local;
		quadratic(local, {0.3f, top}, {1.1f, mid}, {0.3f, bottom});
		for (ImVec2 v : local) {
			extra.push_back(p.at(v.x, v.y));
		}
		p.list->AddPolyline(extra.data(), static_cast<int>(extra.size()), p.outline(), 0, p.lineWidth());
	}
	if (inverted) {
		p.circle(p.w() - 0.25f, mid, 0.22f, colors::BODY);
	}
}

void drawTriangle(const Painter& p) {
	bool inverted = p.c.kind == Kind::Not;
	float tip = inverted ? 1.5f : 1.95f;
	p.polygon({{0.15f, 0.3f}, {tip, 1.f}, {0.15f, 1.7f}}, colors::BODY);
	if (inverted) {
		p.circle(1.75f, 1.f, 0.22f, colors::BODY);
	}
}

void drawSevenSegment(const Painter& p) {
	p.box(0.f, 0.f, p.w(), p.h(), IM_COL32(18, 20, 24, 255));
	ImU32 on = parseColor(p.c.props.color, IM_COL32(255, 70, 60, 255));
	ImU32 off = IM_COL32(48, 40, 44, 255);
	float l = 1.4f, r = 4.1f, t = 1.f, m = 4.5f, b = 8.f;
	struct Segment {
		float x1, y1, x2, y2;
	};
	const Segment segments[7] = {{l, t, r, t}, {r, t, r, m}, {r, m, r, b}, {l, b, r, b},
	                             {l, m, l, b}, {l, t, l, m}, {l, m, r, m}};
	for (int i = 0; i < 7; i++) {
		const Segment& s = segments[i];
		bool lit = p.pin(i).isOne();
		float inset = 0.25f;
		float dx = s.x1 == s.x2 ? 0.f : inset, dy = s.y1 == s.y2 ? 0.f : inset;
		p.line(s.x1 + dx, s.y1 + dy, s.x2 - dx, s.y2 - dy, lit ? on : off, 0.32f);
	}
	p.circle(r + 0.5f, b, 0.2f, p.pin(7).isOne() ? on : off, false);
	p.pinLabels(0.4f);
}

std::string bitsText(Value v) {
	return v.width == 1 ? v.binary() : v.hex();
}

void drawBody(const Painter& p, const Library& library) {
	const Component& c = p.c;
	float w = p.w(), h = p.h();
	switch (c.kind) {
	case Kind::And:
	case Kind::Or:
	case Kind::Nand:
	case Kind::Nor:
	case Kind::Xor:
	case Kind::Xnor:
		drawGate(p);
		break;
	case Kind::Not:
	case Kind::Buffer:
		drawTriangle(p);
		break;

	case Kind::Switch: {
		bool on = p.pin("out").isOne();
		p.box(0.1f, 0.1f, w - 0.1f, h - 0.1f, colors::BODY);
		p.box(0.4f, 0.65f, 1.6f, 1.35f, on ? dim(colors::ONE, 0.6f) : IM_COL32(30, 33, 40, 255), 0.35f,
		      false);
		p.circle(on ? 1.25f : 0.75f, 1.f, 0.28f, on ? colors::ONE : colors::MUTED, false);
		break;
	}
	case Kind::Button: {
		bool on = p.pin("out").isOne();
		p.box(0.1f, 0.1f, w - 0.1f, h - 0.1f, colors::BODY);
		p.circle(1.f, 1.f, 0.55f, on ? colors::ONE : IM_COL32(150, 60, 60, 255));
		break;
	}
	case Kind::Constant:
		p.box(0.2f, 0.3f, w, h - 0.3f, colors::BODY);
		p.text(1.1f, 1.f, bitsText(Value::of(c.props.value, c.props.width)), c.props.width > 4 ? 0.45f : 0.7f,
		       colors::TEXT);
		break;
	case Kind::Clock: {
		Value v = p.pin("out");
		p.box(0.1f, 0.1f, w - 0.1f, h - 0.1f, colors::BODY);
		std::vector<ImVec2> wave{p.at(0.4f, 1.3f), p.at(0.7f, 1.3f), p.at(0.7f, 0.7f), p.at(1.1f, 0.7f),
		                         p.at(1.1f, 1.3f), p.at(1.4f, 1.3f), p.at(1.4f, 0.7f), p.at(1.6f, 0.7f)};
		p.list->AddPolyline(wave.data(), static_cast<int>(wave.size()), p.col(valueColor(v)), 0,
		                    std::max(1.f, p.px(0.1f)));
		break;
	}
	case Kind::Led: {
		ImU32 on = parseColor(c.props.color, IM_COL32(255, 70, 60, 255));
		Value v = p.pin("in");
		ImU32 fill = v.isOne() ? on : v.isX() ? IM_COL32(40, 46, 70, 255) : dim(on, 0.25f);
		if (v.isOne()) {
			p.circle(1.1f, 1.f, 0.95f, withAlpha(on, 0.25f), false);
		}
		p.circle(1.1f, 1.f, 0.75f, fill);
		break;
	}
	case Kind::InputPin:
	case Kind::OutputPin: {
		bool input = c.kind == Kind::InputPin;
		Value v = p.pin(input ? "out" : "in");
		p.box(0.f, 0.1f, w, h - 0.1f, colors::BODY, input ? 0.1f : 0.6f);
		int width = c.props.width;
		float cell = w / static_cast<float>(std::max(width, 1));
		if (width == 1) {
			p.box(0.45f, 0.45f, w - 0.45f, h - 0.45f, valueColor(v), input ? 0.08f : 0.5f, false);
			p.text(w / 2, 1.f, v.binary(), 0.6f, IM_COL32(15, 20, 25, 255));
		} else {
			for (int i = 0; i < width; i++) {
				int bit = width - 1 - i;
				Value b = Value::make(v.bits >> bit, v.known >> bit, 1);
				p.text(cell * (static_cast<float>(i) + 0.5f), 1.f, b.binary(), 0.6f, valueColor(b));
			}
		}
		break;
	}
	case Kind::Probe: {
		Value v = p.pin("in");
		p.box(0.f, 0.3f, w, h - 0.3f, IM_COL32(26, 30, 38, 255), 0.2f);
		std::string s = v.width == 1 ? v.binary() : v.hex() + " " + v.decimal();
		p.text(w / 2, 1.f, s, v.width > 8 ? 0.42f : 0.55f, valueColor(v));
		break;
	}
	case Kind::SevenSegment:
		drawSevenSegment(p);
		break;
	case Kind::HexDisplay: {
		Value v = p.pin("in");
		p.box(0.f, 0.f, w, h, IM_COL32(18, 20, 24, 255));
		ImU32 on = parseColor(c.props.color, IM_COL32(255, 70, 60, 255));
		const char* digits = "0123456789ABCDEF";
		std::string s = v.fullyKnown() ? std::string(1, digits[v.bits & 15]) : "?";
		p.text(w / 2, h / 2, s, 3.6f, v.fullyKnown() ? on : colors::UNKNOWN);
		break;
	}

	case Kind::Splitter:
	case Kind::Merger: {
		bool split = c.kind == Kind::Splitter;
		p.line(1.f, 0.5f, 1.f, h - 0.5f, colors::OUTLINE, 0.22f);
		for (const logic::PinSpec& pin : p.spec.pins) {
			float y = static_cast<float>(pin.y);
			p.line(static_cast<float>(pin.x), y, 1.f, y, valueColor(p.pin(pin.name)), 0.08f);
			if (pin.width < c.props.width) {
				p.text(split ? 1.55f : 0.45f, y - 0.35f, pin.name, 0.4f, colors::MUTED);
			}
		}
		break;
	}
	case Kind::Tunnel: {
		Value v = p.pin("io");
		p.polygon({{0.f, 1.f}, {0.7f, 0.35f}, {w, 0.35f}, {w, 1.65f}, {0.7f, 1.65f}}, colors::BODY);
		p.text(1.8f, 1.f, c.props.text, 0.55f, valueColor(v));
		break;
	}
	case Kind::Text:
		if (p.look.selected || p.look.ghost) {
			p.box(0.f, 0.f, w, h, IM_COL32(0, 0, 0, 0));
		}
		p.text(w / 2, h / 2, c.props.text, 0.7f, colors::TEXT);
		break;

	case Kind::Mux:
		p.polygon({{0.f, 0.f}, {w, 1.f}, {w, h - 1.f}, {0.f, h}}, colors::BODY);
		p.text(w / 2, h / 2, "MUX", 0.6f, colors::TEXT);
		p.pinLabels(0.48f);
		break;
	case Kind::Demux:
		p.polygon({{0.f, 1.f}, {w, 0.f}, {w, h}, {0.f, h - 1.f}}, colors::BODY);
		p.text(w / 2, h / 2, "DMX", 0.6f, colors::TEXT);
		p.pinLabels(0.48f);
		break;
	case Kind::Decoder:
	case Kind::PriorityEncoder:
		p.box(0.f, 0.f, w, h, colors::BODY, 0.15f);
		p.text(w / 2, 0.6f, c.kind == Kind::Decoder ? "DEC" : "PRI", 0.6f, colors::TEXT);
		p.pinLabels(0.48f);
		break;

	case Kind::Chip: {
		const logic::ChipDef* def = library.find(c.props.chip);
		ImU32 tint = def ? parseColor(def->color, IM_COL32(52, 70, 104, 255)) : colors::ERROR;
		p.box(0.f, 0.f, w, h, dim(tint, 0.55f), 0.2f);
		p.text(w / 2, 0.55f, c.props.chip, 0.5f, colors::TEXT);
		p.pinLabels(0.5f);
		break;
	}

	default: {
		// Flip-flops and memories: a box with a title, pin names and the clock triangle
		p.box(0.f, 0.f, w, h, colors::BODY, 0.15f);
		const char* title = "";
		switch (c.kind) {
		case Kind::SrLatch:
			title = "SR";
			break;
		case Kind::DLatch:
			title = "D latch";
			break;
		case Kind::DFlipFlop:
			title = "D FF";
			break;
		case Kind::JkFlipFlop:
			title = "JK FF";
			break;
		case Kind::TFlipFlop:
			title = "T FF";
			break;
		case Kind::Register:
			title = "REG";
			break;
		case Kind::Counter:
			title = "CNT";
			break;
		case Kind::Ram:
			title = "RAM";
			break;
		case Kind::Rom:
			title = "ROM";
			break;
		default:
			break;
		}
		bool showsValue = c.kind == Kind::Register || c.kind == Kind::Counter;
		p.text(w / 2, h / 2 - (showsValue ? 0.45f : 0.f), title, 0.6f, colors::TEXT);
		if (showsValue) {
			Value q = p.pin("Q");
			p.text(w / 2, h / 2 + 0.45f, q.hex(), 0.6f, valueColor(q));
		}
		p.pinLabels(0.5f);
		p.clockMarks();
		break;
	}
	}
}

} // namespace

void setCanvasFont(ImFont* font) {
	canvasFont = font;
}

void drawGrid(ImDrawList* list, const Camera& camera, ImVec2 min, ImVec2 max) {
	list->AddRectFilled(min, max, colors::CANVAS);
	float step = camera.scale();
	int every = step < 8.f ? 4 : 1;
	ImVec2 first = camera.toWorld(min), last = camera.toWorld(max);
	int x0 = static_cast<int>(std::floor(first.x)), x1 = static_cast<int>(std::ceil(last.x));
	int y0 = static_cast<int>(std::floor(first.y)), y1 = static_cast<int>(std::ceil(last.y));
	float r = std::max(1.f, step * 0.05f);
	for (int y = y0; y <= y1; y++) {
		if (y % every != 0) {
			continue;
		}
		for (int x = x0; x <= x1; x++) {
			if (x % every != 0) {
				continue;
			}
			ImVec2 s = camera.toScreen(Point{x, y});
			list->AddRectFilled({s.x - r / 2, s.y - r / 2}, {s.x + r / 2, s.y + r / 2}, colors::GRID_DOT);
		}
	}
}

void drawComponent(ImDrawList* list, const Camera& camera, const Component& c, const ComponentSpec& spec,
                   const Look& look, const Library& library) {
	Painter painter{list, camera, c, spec, look, look.ghost ? 0.5f : 1.f};
	drawBody(painter, library);
	painter.pins();
	if (!c.label.empty() && c.kind != Kind::Text) {
		ImVec2 a = painter.at(0.f, 0.f), b = painter.at(painter.w(), painter.h());
		ImVec2 top{(a.x + b.x) / 2, std::min(a.y, b.y) - painter.px(0.45f)};
		painter.textAt(top, c.label, 0.55f, colors::TEXT);
	}
}

void drawWire(ImDrawList* list, const Camera& camera, const std::vector<Point>& path, ImU32 color,
              float thickness) {
	if (path.size() < 2) {
		return;
	}
	std::vector<ImVec2> points;
	for (Point p : path) {
		points.push_back(camera.toScreen(p));
	}
	list->AddPolyline(points.data(), static_cast<int>(points.size()), color, 0,
	                  std::max(1.f, thickness * camera.scale()));
	// Round joints at the corners
	for (size_t i = 1; i + 1 < points.size(); i++) {
		list->AddCircleFilled(points[i], std::max(0.5f, thickness * camera.scale() / 2), color);
	}
}

} // namespace gui
