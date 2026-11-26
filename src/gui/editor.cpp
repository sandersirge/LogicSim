#include "gui/editor.hpp"

#include "core/json_io.hpp"
#include "gui/draw.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <sstream>

namespace gui {

using logic::Kind;
using logic::Value;

namespace {

constexpr std::size_t EVENT_BUDGET = 300'000; // per frame / per step
constexpr std::size_t MAX_UNDO = 200;
constexpr float DRAG_PIXELS = 4.f;

bool isOutput(const ComponentSpec& spec, int pin) {
	return pin >= 0 && pin < static_cast<int>(spec.pins.size()) && spec.pins[static_cast<size_t>(pin)].output;
}

std::string hexColor(const float rgb[3]) {
	auto c = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
	return std::format("#{:02x}{:02x}{:02x}", c(rgb[0]), c(rgb[1]), c(rgb[2]));
}

} // namespace

Editor::Editor() {
	newDocument();
}

// ----------------------------------------------------------------------------------- Document

void Editor::newDocument() {
	doc_ = {};
	doc_.main.name = "Untitled";
	path_.clear();
	modified_ = false;
	undo_.clear();
	redo_.clear();
	selected_.clear();
	selectedWire_ = -1;
	mode_ = Mode::Idle;
	loadWarnings.clear();
	camera_ = {};
	camera_.offset = {canvasMin_.x + 2 * GRID, canvasMin_.y + 2 * GRID};
	rebuild();
}

bool Editor::open(const std::filesystem::path& path, std::string& error) {
	logic::LoadResult result = logic::loadFile(path);
	if (!result.ok) {
		error = result.error;
		return false;
	}
	doc_ = std::move(result.document);
	if (doc_.main.name.empty()) {
		doc_.main.name = path.stem().string();
	}
	loadWarnings = std::move(result.warnings);
	path_ = path;
	modified_ = false;
	undo_.clear();
	redo_.clear();
	selected_.clear();
	selectedWire_ = -1;
	mode_ = Mode::Idle;
	rebuild();
	fitView();
	return true;
}

bool Editor::save(const std::filesystem::path& path, std::string& error) {
	if (!logic::saveFile(path, logic::saveDocument(doc_), error)) {
		return false;
	}
	path_ = path;
	modified_ = false;
	return true;
}

// ------------------------------------------------------------------------------------ Changes

void Editor::beginChange() {
	undo_.push_back(doc_);
	if (undo_.size() > MAX_UNDO) {
		undo_.erase(undo_.begin());
	}
	redo_.clear();
	modified_ = true;
}

void Editor::changed() {
	rebuild();
}

void Editor::rebuild(bool settle) {
	compiled_ = logic::compile(doc_.main, doc_.library);
	sim_ = logic::Simulator(compiled_.netlist);
	errorComponents_.clear();
	for (const logic::Diagnostic& d : compiled_.diagnostics) {
		if (d.error) {
			errorComponents_.insert(d.component);
		}
	}
	tickDebt_ = 0.0;
	oscillating_ = settle && sim_.settle(EVENT_BUDGET) == logic::SettleResult::Oscillating;
}

void Editor::undo() {
	if (undo_.empty()) {
		return;
	}
	redo_.push_back(std::move(doc_));
	doc_ = std::move(undo_.back());
	undo_.pop_back();
	std::erase_if(selected_, [&](int id) { return doc_.main.find(id) == nullptr; });
	selectedWire_ = -1;
	modified_ = true;
	rebuild();
}

void Editor::redo() {
	if (redo_.empty()) {
		return;
	}
	undo_.push_back(std::move(doc_));
	doc_ = std::move(redo_.back());
	redo_.pop_back();
	std::erase_if(selected_, [&](int id) { return doc_.main.find(id) == nullptr; });
	selectedWire_ = -1;
	modified_ = true;
	rebuild();
}

void Editor::remapPins(int id, const ComponentSpec& before) {
	const Component* c = doc_.main.find(id);
	if (c == nullptr) {
		return;
	}
	ComponentSpec after = specOf(*c);
	auto remap = [&](PinRef& ref) {
		if (ref.component != id) {
			return true;
		}
		if (ref.pin < 0 || ref.pin >= static_cast<int>(before.pins.size())) {
			return false;
		}
		ref.pin = after.pinIndex(before.pins[static_cast<size_t>(ref.pin)].name);
		return ref.pin >= 0;
	};
	std::erase_if(doc_.main.wires, [&](Wire& w) { return !(remap(w.from) && remap(w.to)); });
}

// ---------------------------------------------------------------------------------- Selection

void Editor::cancel() {
	mode_ = Mode::Idle;
	selected_.clear();
	selectedWire_ = -1;
	wireBends_.clear();
}

void Editor::startPlacing(Kind kind) {
	mode_ = Mode::Placing;
	placeKind_ = kind;
	placeProps_ = logic::defaultProps(kind);
	placeRotation_ = 0;
	selected_.clear();
	selectedWire_ = -1;
}

void Editor::deleteSelection() {
	if (selected_.empty() && selectedWire_ < 0) {
		return;
	}
	beginChange();
	if (selectedWire_ >= 0 && selectedWire_ < static_cast<int>(doc_.main.wires.size())) {
		doc_.main.wires.erase(doc_.main.wires.begin() + selectedWire_);
	}
	for (int id : selected_) {
		doc_.main.remove(id);
	}
	selected_.clear();
	selectedWire_ = -1;
	changed();
}

void Editor::rotateSelection() {
	if (mode_ == Mode::Placing) {
		placeRotation_ = (placeRotation_ + 1) & 3;
		return;
	}
	if (selected_.empty()) {
		return;
	}
	beginChange();
	for (int id : selected_) {
		if (Component* c = doc_.main.find(id)) {
			c->rotation = (c->rotation + 1) & 3;
		}
	}
	changed();
}

void Editor::selectAll() {
	selected_.clear();
	for (const Component& c : doc_.main.components) {
		selected_.insert(c.id);
	}
	selectedWire_ = -1;
}

void Editor::copySelection() {
	if (selected_.empty()) {
		return;
	}
	clipboard_ = {};
	for (const Component& c : doc_.main.components) {
		if (selected_.contains(c.id)) {
			clipboard_.components.push_back(c);
		}
	}
	for (const Wire& w : doc_.main.wires) {
		if (selected_.contains(w.from.component) && selected_.contains(w.to.component)) {
			clipboard_.wires.push_back(w);
		}
	}
	pasteCount_ = 0;
}

void Editor::cut() {
	copySelection();
	deleteSelection();
}

void Editor::paste() {
	if (clipboard_.components.empty()) {
		return;
	}
	pasteCount_++;
	pasteCircuit(clipboard_, Point{2 * pasteCount_, 2 * pasteCount_});
}

void Editor::duplicate() {
	copySelection();
	paste();
}

void Editor::pasteCircuit(const Circuit& source, Point offset) {
	beginChange();
	std::map<int, int> ids;
	selected_.clear();
	selectedWire_ = -1;
	for (Component c : source.components) {
		int id = doc_.main.nextId++;
		ids[c.id] = id;
		c.id = id;
		c.pos.x += offset.x;
		c.pos.y += offset.y;
		doc_.main.components.push_back(c);
		selected_.insert(id);
	}
	for (Wire w : source.wires) {
		w.from.component = ids[w.from.component];
		w.to.component = ids[w.to.component];
		for (Point& b : w.bends) {
			b.x += offset.x;
			b.y += offset.y;
		}
		doc_.main.wires.push_back(w);
	}
	changed();
}

// -------------------------------------------------------------------------------- Hit testing

Point Editor::snapped(ImVec2 world) const {
	return Point{static_cast<int>(std::lround(world.x)), static_cast<int>(std::lround(world.y))};
}

std::optional<PinRef> Editor::pinAt(ImVec2 world) const {
	std::optional<PinRef> best;
	float bestDistance = 0.45f;
	for (const Component& c : doc_.main.components) {
		ComponentSpec spec = specOf(c);
		for (size_t i = 0; i < spec.pins.size(); i++) {
			Point p = pinPosition(c, spec, static_cast<int>(i));
			float d = std::hypot(world.x - static_cast<float>(p.x), world.y - static_cast<float>(p.y));
			if (d < bestDistance) {
				bestDistance = d;
				best = PinRef{c.id, static_cast<int>(i)};
			}
		}
	}
	return best;
}

bool Editor::insideBox(const Component& c, ImVec2 world, float margin) const {
	ComponentSpec spec = specOf(c);
	ImVec2 size = rotatedSize(c, spec);
	float x = static_cast<float>(c.pos.x), y = static_cast<float>(c.pos.y);
	return world.x >= x - margin && world.x <= x + size.x + margin && world.y >= y - margin &&
	       world.y <= y + size.y + margin;
}

int Editor::componentAt(ImVec2 world) const {
	for (auto it = doc_.main.components.rbegin(); it != doc_.main.components.rend(); ++it) {
		if (insideBox(*it, world, 0.05f)) {
			return it->id;
		}
	}
	return -1;
}

int Editor::wireAt(ImVec2 world) const {
	for (size_t i = 0; i < doc_.main.wires.size(); i++) {
		std::vector<Point> path = wirePath(doc_.main, doc_.library, doc_.main.wires[i]);
		for (size_t k = 1; k < path.size(); k++) {
			ImVec2 a{static_cast<float>(path[k - 1].x), static_cast<float>(path[k - 1].y)};
			ImVec2 b{static_cast<float>(path[k].x), static_cast<float>(path[k].y)};
			if (distanceToSegment(world, a, b) < 0.25f) {
				return static_cast<int>(i);
			}
		}
	}
	return -1;
}

Value Editor::pinValue(int component, int pin) const {
	int net = compiled_.net(component, pin);
	if (net < 0) {
		return Value::x(1);
	}
	return sim_.value(net);
}

bool Editor::isInteractive(const Component& c) const {
	return c.kind == Kind::Switch || c.kind == Kind::Button || c.kind == Kind::InputPin;
}

bool Editor::interact(Component& c, ImVec2 world, bool press) {
	int prim = compiled_.prim(c.id);
	if (c.kind == Kind::Button) {
		if (prim >= 0) {
			sim_.setSource(prim, Value::of(press ? 1 : 0, 1));
		}
		return true;
	}
	if (press) {
		return false; // switches and pins change on release (a drag moves them instead)
	}
	if (c.kind == Kind::Switch) {
		c.props.value ^= 1;
	} else if (c.kind == Kind::InputPin) {
		ComponentSpec spec = specOf(c);
		// Which bit cell was clicked: undo the rotation
		float dx = world.x - static_cast<float>(c.pos.x), dy = world.y - static_cast<float>(c.pos.y);
		float w = static_cast<float>(spec.w), h = static_cast<float>(spec.h);
		float x = dx;
		switch (c.rotation & 3) {
		case 1:
			x = dy;
			break;
		case 2:
			x = w - dx;
			break;
		case 3:
			x = w - dy;
			break;
		default:
			break;
		}
		(void)h;
		int width = c.props.width;
		int cell = std::clamp(static_cast<int>(x / (w / static_cast<float>(width))), 0, width - 1);
		c.props.value ^= std::uint32_t{1} << (width - 1 - cell);
	} else {
		return false;
	}
	modified_ = true;
	if (prim >= 0) {
		sim_.setSource(prim, Value::of(c.props.value, c.props.width));
	}
	return true;
}

// --------------------------------------------------------------------------------- Mouse input

void Editor::mousePressed(sf::Mouse::Button button, ImVec2 screen, bool shift) {
	mouse_ = screen;
	ImVec2 world = mouseWorld();
	pressScreen_ = screen;
	pressWorld_ = world;
	dragged_ = false;

	if (button == sf::Mouse::Button::Right || button == sf::Mouse::Button::Middle) {
		beforePan_ = mode_;
		mode_ = Mode::Panning;
		return;
	}
	if (button != sf::Mouse::Button::Left) {
		return;
	}

	if (mode_ == Mode::Placing) {
		logic::Props props = placeProps_;
		ComponentSpec spec = logic::builtinSpec(placeKind_, props);
		Component probe{0, placeKind_, {}, placeRotation_, {}, props};
		ImVec2 size = rotatedSize(probe, spec);
		Point at = snapped({world.x - size.x / 2, world.y - size.y / 2});
		beginChange();
		int id = doc_.main.add(placeKind_, at.x, at.y, props);
		doc_.main.find(id)->rotation = placeRotation_;
		changed();
		if (!shift) {
			mode_ = Mode::Idle;
			selected_ = {id};
		}
		return;
	}

	if (mode_ == Mode::Wiring) {
		if (std::optional<PinRef> pin = pinAt(world)) {
			finishWire(*pin);
		} else {
			wireBends_.push_back(snapped(world));
		}
		return;
	}

	if (std::optional<PinRef> pin = pinAt(world)) {
		mode_ = Mode::Wiring;
		wireStart_ = *pin;
		wireBends_.clear();
		selected_.clear();
		selectedWire_ = -1;
		return;
	}

	int id = componentAt(world);
	if (id >= 0) {
		Component* c = doc_.main.find(id);
		selectedWire_ = -1;
		pressedComponent_ = id;
		if (isInteractive(*c) && !shift) {
			mode_ = Mode::Pressing;
			interact(*c, world, true);
			return;
		}
		if (shift) {
			if (!selected_.erase(id)) {
				selected_.insert(id);
			}
		} else if (!selected_.contains(id)) {
			selected_ = {id};
		}
		mode_ = Mode::Moving;
		beforeMove_ = doc_;
		moved_ = {0, 0};
		return;
	}

	int wire = wireAt(world);
	if (wire >= 0) {
		// A click selects the wire, a drag starts a branch from it
		selected_.clear();
		selectedWire_ = wire;
		pressedWire_ = wire;
		mode_ = Mode::WirePressed;
		return;
	}

	if (!shift) {
		selected_.clear();
	}
	selectedWire_ = -1;
	mode_ = Mode::Selecting;
}

void Editor::mouseMoved(ImVec2 screen) {
	ImVec2 previous = mouse_;
	mouse_ = screen;
	if (std::hypot(screen.x - pressScreen_.x, screen.y - pressScreen_.y) > DRAG_PIXELS) {
		dragged_ = true;
	}
	switch (mode_) {
	case Mode::WirePressed:
		if (dragged_) {
			startBranch(pressedWire_, pressWorld_);
		}
		break;
	case Mode::Panning:
		camera_.offset.x += screen.x - previous.x;
		camera_.offset.y += screen.y - previous.y;
		break;
	case Mode::Pressing:
		if (dragged_) {
			// Dragging a switch moves it
			Component* c = doc_.main.find(pressedComponent_);
			if (c && c->kind == Kind::Button) {
				interact(*c, mouseWorld(), false);
			}
			if (!selected_.contains(pressedComponent_)) {
				selected_ = {pressedComponent_};
			}
			mode_ = Mode::Moving;
			beforeMove_ = doc_;
			moved_ = {0, 0};
		}
		[[fallthrough]];
	case Mode::Moving: {
		ImVec2 world = mouseWorld();
		Point total = snapped({world.x - pressWorld_.x, world.y - pressWorld_.y});
		int dx = total.x - moved_.x, dy = total.y - moved_.y;
		if (dx == 0 && dy == 0) {
			break;
		}
		for (int id : selected_) {
			if (Component* c = doc_.main.find(id)) {
				c->pos.x += dx;
				c->pos.y += dy;
			}
		}
		for (Wire& w : doc_.main.wires) {
			if (selected_.contains(w.from.component) && selected_.contains(w.to.component)) {
				for (Point& b : w.bends) {
					b.x += dx;
					b.y += dy;
				}
			}
		}
		moved_ = total;
		break;
	}
	default:
		break;
	}
}

void Editor::mouseReleased(sf::Mouse::Button button, ImVec2 screen) {
	mouse_ = screen;
	ImVec2 world = mouseWorld();
	if (mode_ == Mode::Panning &&
	    (button == sf::Mouse::Button::Right || button == sf::Mouse::Button::Middle)) {
		mode_ = beforePan_;
		if (!dragged_ && button == sf::Mouse::Button::Right) {
			// A right click (no drag) cancels wiring / placing
			if (mode_ == Mode::Wiring || mode_ == Mode::Placing) {
				mode_ = Mode::Idle;
				wireBends_.clear();
			}
		}
		return;
	}
	if (button != sf::Mouse::Button::Left) {
		return;
	}
	switch (mode_) {
	case Mode::Moving:
		if (moved_.x != 0 || moved_.y != 0) {
			undo_.push_back(std::move(beforeMove_));
			if (undo_.size() > MAX_UNDO) {
				undo_.erase(undo_.begin());
			}
			redo_.clear();
			modified_ = true;
			// Positions matter inside chips only, but rebuild anyway so pin nets stay in step
			changed();
		}
		mode_ = Mode::Idle;
		break;
	case Mode::WirePressed:
		mode_ = Mode::Idle;
		break;
	case Mode::Pressing:
		if (Component* c = doc_.main.find(pressedComponent_)) {
			interact(*c, world, false);
			selected_ = {pressedComponent_};
		}
		mode_ = Mode::Idle;
		break;
	case Mode::Selecting: {
		ImVec2 lo{std::min(world.x, pressWorld_.x), std::min(world.y, pressWorld_.y)};
		ImVec2 hi{std::max(world.x, pressWorld_.x), std::max(world.y, pressWorld_.y)};
		for (const Component& c : doc_.main.components) {
			ComponentSpec spec = specOf(c);
			ImVec2 size = rotatedSize(c, spec);
			float x = static_cast<float>(c.pos.x), y = static_cast<float>(c.pos.y);
			if (x < hi.x && x + size.x > lo.x && y < hi.y && y + size.y > lo.y) {
				selected_.insert(c.id);
			}
		}
		mode_ = Mode::Idle;
		break;
	}
	case Mode::Wiring:
		if (dragged_) {
			std::optional<PinRef> pin = pinAt(world);
			if (pin && !(*pin == wireStart_)) {
				finishWire(*pin);
			} else if (!pin) {
				// Released on empty grid: keep drawing, the release point is a bend
				wireBends_.push_back(snapped(world));
			}
		}
		break;
	default:
		break;
	}
}

void Editor::finishWire(PinRef end) {
	if (end == wireStart_) {
		return;
	}
	const Component* a = doc_.main.find(wireStart_.component);
	const Component* b = doc_.main.find(end.component);
	if (a == nullptr || b == nullptr) {
		mode_ = Mode::Idle;
		return;
	}
	Wire wire{wireStart_, end, wireBends_};
	// Wires go from an output to an input where possible
	if (!isOutput(specOf(*a), wireStart_.pin) && isOutput(specOf(*b), end.pin)) {
		std::swap(wire.from, wire.to);
		std::ranges::reverse(wire.bends);
	}
	beginChange();
	doc_.main.wires.push_back(std::move(wire));
	wireBends_.clear();
	mode_ = Mode::Idle;
	changed();
}

void Editor::startBranch(int index, ImVec2 world) {
	if (index < 0 || index >= static_cast<int>(doc_.main.wires.size())) {
		mode_ = Mode::Idle;
		return;
	}
	const Wire& wire = doc_.main.wires[static_cast<size_t>(index)];
	std::vector<Point> path = wirePath(doc_.main, doc_.library, wire);
	if (path.size() < 2) {
		mode_ = Mode::Idle;
		return;
	}
	// The segment that was grabbed, and the grid point on it nearest to the mouse
	size_t segment = 1;
	float best = 1e9f;
	for (size_t k = 1; k < path.size(); k++) {
		ImVec2 a{static_cast<float>(path[k - 1].x), static_cast<float>(path[k - 1].y)};
		ImVec2 b{static_cast<float>(path[k].x), static_cast<float>(path[k].y)};
		float d = distanceToSegment(world, a, b);
		if (d < best) {
			best = d;
			segment = k;
		}
	}
	Point a = path[segment - 1], b = path[segment];
	Point grab = snapped(world);
	if (a.y == b.y) {
		grab = {std::clamp(grab.x, std::min(a.x, b.x), std::max(a.x, b.x)), a.y};
	} else {
		grab = {a.x, std::clamp(grab.y, std::min(a.y, b.y), std::max(a.y, b.y))};
	}
	// The branch follows the wire's corners up to the grab point, then goes its own way
	wireBends_.assign(path.begin() + 1, path.begin() + static_cast<long>(segment));
	if (wireBends_.empty() || !(wireBends_.back() == grab)) {
		wireBends_.push_back(grab);
	}
	wireStart_ = wire.from;
	selectedWire_ = -1;
	mode_ = Mode::Wiring;
}

void Editor::wheel(float delta, ImVec2 screen) {
	camera_.zoomAt(screen, std::pow(1.15f, delta));
}

bool Editor::keyPressed(const sf::Event::KeyPressed& key) {
	using Key = sf::Keyboard::Key;
	if (key.control) {
		switch (key.code) {
		case Key::Z:
			key.shift ? redo() : undo();
			return true;
		case Key::Y:
			redo();
			return true;
		case Key::A:
			selectAll();
			return true;
		case Key::C:
			copySelection();
			return true;
		case Key::X:
			cut();
			return true;
		case Key::V:
			paste();
			return true;
		case Key::D:
			duplicate();
			return true;
		default:
			return false;
		}
	}
	switch (key.code) {
	case Key::Delete:
	case Key::Backspace:
		deleteSelection();
		return true;
	case Key::R:
		rotateSelection();
		return true;
	case Key::Escape:
		cancel();
		return true;
	case Key::Space:
		running_ = !running_;
		return true;
	case Key::Period:
		stepTick();
		return true;
	case Key::F:
		fitView();
		return true;
	case Key::Add:
	case Key::Equal:
		camera_.zoomAt({(canvasMin_.x + canvasMax_.x) / 2, (canvasMin_.y + canvasMax_.y) / 2}, 1.25f);
		return true;
	case Key::Subtract:
	case Key::Hyphen:
		camera_.zoomAt({(canvasMin_.x + canvasMax_.x) / 2, (canvasMin_.y + canvasMax_.y) / 2}, 0.8f);
		return true;
	default:
		return false;
	}
}

void Editor::fitView() {
	if (doc_.main.components.empty()) {
		camera_.zoom = 1.f;
		camera_.offset = {canvasMin_.x + 2 * GRID, canvasMin_.y + 2 * GRID};
		return;
	}
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	for (const Component& c : doc_.main.components) {
		ImVec2 size = rotatedSize(c, specOf(c));
		x0 = std::min(x0, static_cast<float>(c.pos.x));
		y0 = std::min(y0, static_cast<float>(c.pos.y) - 1.f); // room for labels
		x1 = std::max(x1, static_cast<float>(c.pos.x) + size.x);
		y1 = std::max(y1, static_cast<float>(c.pos.y) + size.y);
	}
	for (const Wire& w : doc_.main.wires) {
		for (Point b : w.bends) {
			x0 = std::min(x0, static_cast<float>(b.x));
			y0 = std::min(y0, static_cast<float>(b.y));
			x1 = std::max(x1, static_cast<float>(b.x));
			y1 = std::max(y1, static_cast<float>(b.y));
		}
	}
	float margin = 2.f;
	float width = canvasMax_.x - canvasMin_.x, height = canvasMax_.y - canvasMin_.y;
	float zoom = std::min(width / ((x1 - x0 + 2 * margin) * GRID), height / ((y1 - y0 + 2 * margin) * GRID));
	camera_.zoom = std::clamp(zoom, 0.2f, 2.5f);
	float s = camera_.scale();
	camera_.offset = {canvasMin_.x + width / 2 - (x0 + x1) / 2 * s,
	                  canvasMin_.y + height / 2 - (y0 + y1) / 2 * s};
}

// --------------------------------------------------------------------------------- Simulation

void Editor::update(float seconds) {
	eventTimer_ += seconds;
	if (eventTimer_ >= 1.f) {
		eventsPerSecond_ = eventCounter_;
		eventCounter_ = 0;
		eventTimer_ = 0.f;
	}
	if (!running_) {
		return;
	}
	tickDebt_ += static_cast<double>(ticksPerSecond) * seconds;
	auto ticks = static_cast<logic::Tick>(std::min(tickDebt_, 1e7));
	if (ticks == 0) {
		return;
	}
	tickDebt_ -= static_cast<double>(ticks);
	logic::AdvanceResult result = sim_.advance(ticks, EVENT_BUDGET);
	eventCounter_ += result.events;
	oscillating_ = result.budgetExhausted;
	if (result.budgetExhausted) {
		tickDebt_ = 0.0;
	}
}

void Editor::stepTick() {
	running_ = false;
	logic::AdvanceResult result = sim_.advance(1, EVENT_BUDGET);
	oscillating_ = result.budgetExhausted;
}

void Editor::stepEvent() {
	running_ = false;
	sim_.stepEvent();
}

void Editor::resetSimulation() {
	rebuild(false);
}

void Editor::runTicks(logic::Tick ticks) {
	sim_.advance(ticks, EVENT_BUDGET * 10);
}

// ------------------------------------------------------------------------------------ Drawing

void Editor::draw(ImDrawList* list) {
	list->PushClipRect(canvasMin_, canvasMax_, true);
	drawGrid(list, camera_, canvasMin_, canvasMax_);
	const Circuit& circuit = doc_.main;

	for (size_t i = 0; i < circuit.wires.size(); i++) {
		const Wire& w = circuit.wires[i];
		std::vector<Point> path = wirePath(circuit, doc_.library, w);
		Value v = pinValue(w.from.component, w.from.pin);
		float thickness = v.width > 1 ? 0.2f : 0.11f;
		if (static_cast<int>(i) == selectedWire_) {
			drawWire(list, camera_, path, colors::SELECTED, thickness + 0.14f);
		}
		drawWire(list, camera_, path, valueColor(v), thickness);
	}

	drawJunctions(list);

	for (const Component& c : circuit.components) {
		Look look;
		look.selected = selected_.contains(c.id);
		look.error = errorComponents_.contains(c.id);
		int id = c.id;
		look.pinValue = [this, id](int pin) { return pinValue(id, pin); };
		drawComponent(list, camera_, c, specOf(c), look, doc_.library);
	}

	ImVec2 world = mouseWorld();
	if (mode_ == Mode::Wiring) {
		const Component* start = circuit.find(wireStart_.component);
		if (start != nullptr) {
			Point from = pinPosition(*start, specOf(*start), wireStart_.pin);
			Point to = snapped(world);
			if (std::optional<PinRef> pin = pinAt(world)) {
				const Component* end = circuit.find(pin->component);
				to = pinPosition(*end, specOf(*end), pin->pin);
			}
			drawWire(list, camera_, wirePath(from, to, wireBends_), IM_COL32(200, 205, 220, 200), 0.09f);
		}
	}
	if (mode_ == Mode::Placing) {
		Component ghost{0, placeKind_, {}, placeRotation_, {}, placeProps_};
		ComponentSpec spec = logic::builtinSpec(placeKind_, placeProps_);
		ImVec2 size = rotatedSize(ghost, spec);
		ghost.pos = snapped({world.x - size.x / 2, world.y - size.y / 2});
		Look look;
		look.ghost = true;
		drawComponent(list, camera_, ghost, spec, look, doc_.library);
	}
	if (mode_ == Mode::Selecting) {
		ImVec2 a = camera_.toScreen(pressWorld_), b = mouse_;
		list->AddRectFilled(a, b, IM_COL32(120, 165, 255, 30));
		list->AddRect(a, b, IM_COL32(120, 165, 255, 160));
	}
	if (mode_ == Mode::Idle || mode_ == Mode::Wiring) {
		if (std::optional<PinRef> pin = pinAt(world)) {
			const Component* c = circuit.find(pin->component);
			Point p = pinPosition(*c, specOf(*c), pin->pin);
			list->AddCircle(camera_.toScreen(p), camera_.scale() * 0.3f, colors::SELECTED, 0, 2.f);
		}
	}
	list->PopClipRect();
}

namespace {

// Every grid point a wire passes, in order
std::vector<Point> gridPoints(const std::vector<Point>& path) {
	std::vector<Point> points;
	for (size_t i = 0; i + 1 < path.size(); i++) {
		Point a = path[i], b = path[i + 1];
		int steps = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
		for (int k = 0; k < steps; k++) {
			points.push_back({a.x + (b.x - a.x) * k / steps, a.y + (b.y - a.y) * k / steps});
		}
	}
	if (!path.empty()) {
		points.push_back(path.back());
	}
	return points;
}

} // namespace

void Editor::drawJunctions(ImDrawList* list) const {
	// Wires from the same output that run together and then split get a dot where they split
	const Circuit& circuit = doc_.main;
	std::map<std::pair<int, int>, std::vector<std::vector<Point>>> bySource;
	for (const Wire& w : circuit.wires) {
		bySource[{w.from.component, w.from.pin}].push_back(gridPoints(wirePath(circuit, doc_.library, w)));
	}
	for (const auto& [source, paths] : bySource) {
		for (size_t i = 0; i < paths.size(); i++) {
			for (size_t j = i + 1; j < paths.size(); j++) {
				const std::vector<Point>& a = paths[i];
				const std::vector<Point>& b = paths[j];
				size_t same = 0;
				while (same < a.size() && same < b.size() && a[same] == b[same]) {
					same++;
				}
				if (same > 1 && same < a.size() && same < b.size()) {
					Value v = pinValue(source.first, source.second);
					float r = camera_.scale() * (v.width > 1 ? 0.22f : 0.17f);
					list->AddCircleFilled(camera_.toScreen(a[same - 1]), r, valueColor(v));
				}
			}
		}
	}
}

std::string Editor::hoverText() const {
	ImVec2 world = mouseWorld();
	if (mouse_.x < canvasMin_.x || mouse_.y < canvasMin_.y || mouse_.x > canvasMax_.x ||
	    mouse_.y > canvasMax_.y) {
		return {};
	}
	if (std::optional<PinRef> pin = pinAt(world)) {
		const Component* c = doc_.main.find(pin->component);
		ComponentSpec spec = specOf(*c);
		const logic::PinSpec& p = spec.pins[static_cast<size_t>(pin->pin)];
		Value v = pinValue(pin->component, pin->pin);
		std::string text =
		    std::format("{} pin {} ({}, {} bit{}): {}", logic::info(c->kind).name, p.name,
			            p.output ? "output" : "input", p.width, p.width > 1 ? "s" : "", v.binary());
		if (v.width > 1) {
			text += std::format("  = {}  = {}", v.hex(), v.decimal());
		}
		return text;
	}
	int id = componentAt(world);
	if (id >= 0) {
		const Component* c = doc_.main.find(id);
		for (const logic::Diagnostic& d : compiled_.diagnostics) {
			if (d.component == id) {
				return std::format("{}: {}", logic::info(c->kind).name, d.message);
			}
		}
		std::string name(c->kind == Kind::Chip ? c->props.chip : std::string(logic::info(c->kind).name));
		return std::format("{} - {}", name, logic::info(c->kind).tooltip);
	}
	return {};
}

// ------------------------------------------------------------------------------- Properties

void Editor::propertiesPanel() {
	if (!ImGui::IsAnyItemActive()) {
		editSnapshot_ = false;
	}
	if (selected_.size() != 1) {
		if (selected_.size() > 1) {
			ImGui::Text("%zu components selected", selected_.size());
			if (ImGui::Button("Rotate  (R)")) {
				rotateSelection();
			}
			ImGui::SameLine();
			if (ImGui::Button("Delete  (Del)")) {
				deleteSelection();
			}
		} else if (selectedWire_ >= 0 && selectedWire_ < static_cast<int>(doc_.main.wires.size())) {
			const Wire& w = doc_.main.wires[static_cast<size_t>(selectedWire_)];
			Value v = pinValue(w.from.component, w.from.pin);
			ImGui::Text("Wire");
			ImGui::TextDisabled("Value: %s", v.binary().c_str());
			if (v.width > 1) {
				ImGui::TextDisabled("= %s = %s", v.hex().c_str(), v.decimal().c_str());
			}
			if (ImGui::Button("Delete  (Del)")) {
				deleteSelection();
			}
		} else {
			ImGui::TextDisabled("Nothing selected");
			ImGui::Spacing();
			ImGui::TextWrapped(
			    "Click a part in the toolbox, then click on the grid to place it. Drag from a pin "
			    "to another pin to wire them. Click switches to flip them.");
		}
		return;
	}

	Component* c = doc_.main.find(*selected_.begin());
	if (c == nullptr) {
		return;
	}
	const logic::KindInfo& kind = logic::info(c->kind);
	ImGui::Text("%s", c->kind == Kind::Chip ? c->props.chip.c_str() : std::string(kind.name).c_str());
	ImGui::TextDisabled("%s", std::string(kind.tooltip).c_str());
	ImGui::Spacing();

	Component edited = *c;
	logic::Props& p = edited.props;
	unsigned flags = kind.props;
	bool activated = false;
	auto track = [&]() { activated = activated || ImGui::IsItemActivated(); };
	ImGui::PushItemWidth(-90.f);

	ImGui::InputText("Label", &edited.label);
	track();
	if (flags & logic::P_INPUTS) {
		ImGui::SliderInt("Inputs", &p.inputs, 2, 8);
		track();
	}
	if (flags & logic::P_WIDTH) {
		ImGui::SliderInt("Bits", &p.width, c->kind == Kind::Splitter || c->kind == Kind::Merger ? 2 : 1, 32);
		track();
	}
	if (flags & logic::P_SELECT) {
		ImGui::SliderInt("Select bits", &p.selectBits, 1, 5);
		track();
	}
	if (flags & logic::P_ADDRESS) {
		ImGui::SliderInt("Address bits", &p.addressBits, 1, 16);
		track();
	}
	if (flags & logic::P_DELAY) {
		ImGui::InputInt("Delay", &p.delay);
		track();
		ImGui::SetItemTooltip("Propagation delay in ticks");
	}
	if (flags & logic::P_PERIOD) {
		ImGui::InputInt("Period", &p.period, 2, 10);
		track();
		ImGui::SetItemTooltip("Ticks per full clock cycle");
	}
	if (flags & logic::P_VALUE) {
		ImGui::InputScalar("Value (hex)", ImGuiDataType_U32, &p.value, nullptr, nullptr, "%X",
		                   ImGuiInputTextFlags_CharsHexadecimal);
		track();
	}
	if (flags & logic::P_TEXT) {
		ImGui::InputText(c->kind == Kind::Tunnel ? "Name" : "Text", &p.text);
		track();
	}
	if (flags & logic::P_COLOR) {
		ImU32 current = parseColor(p.color, IM_COL32(255, 70, 60, 255));
		float rgb[3] = {static_cast<float>(current & 255) / 255.f,
		                static_cast<float>((current >> 8) & 255) / 255.f,
		                static_cast<float>((current >> 16) & 255) / 255.f};
		if (ImGui::ColorEdit3("Colour", rgb, ImGuiColorEditFlags_NoInputs)) {
			p.color = hexColor(rgb);
		}
		track();
	}
	if (flags & logic::P_PARTS) {
		std::string parts;
		for (int w : logic::partWidths(p)) {
			parts += (parts.empty() ? "" : ",") + std::to_string(w);
		}
		if (ImGui::InputText("Parts", &parts, ImGuiInputTextFlags_EnterReturnsTrue)) {
			p.parts.clear();
			std::stringstream in(parts);
			std::string item;
			while (std::getline(in, item, ',')) {
				try {
					p.parts.push_back(std::stoi(item));
				} catch (...) {
				}
			}
		}
		track();
		ImGui::SetItemTooltip("Bit widths of the parts, low bits first, e.g. 4,4 (press Enter)");
	}
	ImGui::PopItemWidth();

	if (flags & logic::P_DATA) {
		if (romTextFor_ != c->id) {
			romTextFor_ = c->id;
			romText_.clear();
			for (size_t i = 0; i < p.data.size(); i++) {
				romText_ += std::format("{:X}{}", p.data[i], i % 8 == 7 ? "\n" : " ");
			}
		}
		ImGui::TextDisabled("Contents (hex words, address 0 first)");
		ImGui::InputTextMultiline("##rom", &romText_, ImVec2{-1.f, 140.f});
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			p.data.clear();
			std::stringstream in(romText_);
			std::string word;
			while (in >> word) {
				try {
					p.data.push_back(static_cast<std::uint32_t>(std::stoul(word, nullptr, 16)));
				} catch (...) {
				}
			}
			activated = true;
			romTextFor_ = -1;
		}
	}

	ImGui::Spacing();
	if (ImGui::Button("Rotate  (R)")) {
		rotateSelection();
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Delete  (Del)")) {
		deleteSelection();
		return;
	}

	// Live state of the simulation
	int prim = compiled_.prim(c->id);
	if (prim >= 0 && (c->kind == Kind::Ram || c->kind == Kind::Rom)) {
		const logic::Prim& state = sim_.prim(prim);
		ImGui::Spacing();
		ImGui::TextDisabled("Memory now");
		ImGui::BeginChild("memory", ImVec2{-1.f, 140.f}, ImGuiChildFlags_Borders);
		for (size_t i = 0; i < state.memory.size(); i += 4) {
			std::string row = std::format("{:04X}:", i);
			for (size_t k = i; k < std::min(i + 4, state.memory.size()); k++) {
				row += " " + state.memory[k].hex().substr(2);
			}
			ImGui::TextUnformatted(row.c_str());
		}
		ImGui::EndChild();
	}
	for (const logic::Diagnostic& d : compiled_.diagnostics) {
		if (d.component == c->id) {
			ImGui::TextColored(ImVec4{0.95f, 0.4f, 0.4f, 1.f}, "%s", d.message.c_str());
		}
	}

	// Apply the edit: one undo step per widget interaction
	logic::sanitize(edited.kind, edited.props);
	bool differs = edited.label != c->label || !(edited.props == c->props);
	if (differs || activated) {
		if (differs && !editSnapshot_) {
			beginChange();
			editSnapshot_ = true;
			c = doc_.main.find(edited.id);
		}
		if (differs) {
			ComponentSpec before = specOf(*c);
			bool structural = !(edited.props == c->props);
			*c = edited;
			if (structural) {
				remapPins(c->id, before);
				changed();
			}
			modified_ = true;
		}
	}
}

} // namespace gui
