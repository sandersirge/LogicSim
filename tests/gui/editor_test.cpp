// The editor driven like a user would: place parts, wire them, flip a switch, move, undo/redo,
// delete, rotate, copy and paste. No window is opened.
#include "check.hpp"
#include "gui/editor.hpp"

#include <filesystem>
#include <format>

using namespace logic;
using test::check;

namespace {

using Button = sf::Mouse::Button;

// The editor starts with grid point (0, 0) at screen (40, 40) and 20 pixels per grid unit
ImVec2 screen(float x, float y) {
	return {40.f + x * gui::GRID, 40.f + y * gui::GRID};
}

void click(gui::Editor& e, float x, float y, bool shift = false) {
	e.mouseMoved(screen(x, y));
	e.mousePressed(Button::Left, screen(x, y), shift);
	e.mouseReleased(Button::Left, screen(x, y));
}

void drag(gui::Editor& e, float x1, float y1, float x2, float y2) {
	e.mouseMoved(screen(x1, y1));
	e.mousePressed(Button::Left, screen(x1, y1), false);
	e.mouseMoved(screen((x1 + x2) / 2, (y1 + y2) / 2));
	e.mouseMoved(screen(x2, y2));
	e.mouseReleased(Button::Left, screen(x2, y2));
}

sf::Event::KeyPressed key(sf::Keyboard::Key code, bool control = false) {
	sf::Event::KeyPressed k{};
	k.code = code;
	k.control = control;
	return k;
}

const Component* find(const gui::Editor& e, Kind kind) {
	for (const Component& c : e.document().main.components) {
		if (c.kind == kind) {
			return &c;
		}
	}
	return nullptr;
}

} // namespace

int main() {
	gui::Editor e;
	e.setRunning(false);

	// Place a switch, a NOT gate and an LED (each placed centred on the click)
	e.startPlacing(Kind::Switch);
	click(e, 3, 3);
	e.startPlacing(Kind::Not);
	click(e, 9, 3);
	e.startPlacing(Kind::Led);
	click(e, 14, 3);
	check(e.document().main.components.size() == 3, "three parts placed");
	check(!e.placing(), "placing ends after one click");
	const Component* sw = find(e, Kind::Switch);
	const Component* inv = find(e, Kind::Not);
	const Component* led = find(e, Kind::Led);
	check(sw && sw->pos == Point{2, 2} && inv && inv->pos == Point{8, 2} && led && led->pos == Point{13, 2},
	      "positions snapped to the grid");
	int swId = sw->id, notId = inv->id, ledId = led->id;

	// Wire switch -> NOT by dragging pin to pin, and LED -> NOT backwards (gets turned around)
	drag(e, 4, 3, 8, 3);
	drag(e, 13, 3, 10, 3);
	const auto& wires = e.document().main.wires;
	check(wires.size() == 2, std::format("two wires ({})", wires.size()));
	check(wires.size() == 2 && wires[1].from.component == notId && wires[1].to.component == ledId,
	      "a wire drawn from an input to an output is stored output -> input");
	check(e.pinValue(ledId, 0) == Value::of(1, 1), "NOT 0 lights the LED");

	// Flip the switch by clicking its body, then run a few ticks
	click(e, 2.6f, 2.5f);
	check(e.document().main.find(swId)->props.value == 1, "click flips the switch");
	e.runTicks(5);
	check(e.pinValue(ledId, 0) == Value::of(0, 1), "LED goes off after the NOT delay");

	// Move the NOT gate down by 4 and undo / redo it
	drag(e, 9, 2.3f, 9, 6.3f);
	check(e.document().main.find(notId)->pos == Point{8, 6}, "drag moves the gate");
	check(e.document().main.wires.size() == 2, "wires stay attached");
	e.undo();
	check(e.document().main.find(notId)->pos == Point{8, 2}, "undo moves it back");
	e.redo();
	check(e.document().main.find(notId)->pos == Point{8, 6}, "redo moves it again");

	// Delete the selected NOT gate: its wires go too; undo brings everything back
	check(e.selection().contains(notId), "the moved gate is selected");
	e.keyPressed(key(sf::Keyboard::Key::Delete));
	check(e.document().main.find(notId) == nullptr && e.document().main.wires.empty(),
	      "delete removes gate and wires");
	e.keyPressed(key(sf::Keyboard::Key::Z, true));
	check(e.document().main.find(notId) != nullptr && e.document().main.wires.size() == 2,
	      "Ctrl+Z restores them");

	// Rotate the LED
	click(e, 14.2f, 2.4f);
	check(e.selection().size() == 1 && e.selection().contains(ledId), "click selects the LED");
	e.keyPressed(key(sf::Keyboard::Key::R));
	check(e.document().main.find(ledId)->rotation == 1, "R rotates");

	// Select everything with a rubber band, copy and paste
	drag(e, 0.5f, 0.5f, 20, 12);
	check(e.selection().size() == 3, std::format("rubber band selects all three ({})", e.selection().size()));
	e.keyPressed(key(sf::Keyboard::Key::C, true));
	e.keyPressed(key(sf::Keyboard::Key::V, true));
	check(e.document().main.components.size() == 6 && e.document().main.wires.size() == 4,
	      "paste copies the parts and the wires between them");
	check(e.compiled().diagnostics.empty(), "the pasted copy compiles cleanly");

	// Wire with a bend: start at a pin, click on the grid, finish on a pin
	size_t before = e.document().main.wires.size();
	e.startPlacing(Kind::Led);
	click(e, 14, 13);
	click(e, 4, 3);   // switch output: start
	click(e, 6, 13);  // bend
	click(e, 13, 13); // LED input: finish
	check(e.document().main.wires.size() == before + 1 && !e.document().main.wires.back().bends.empty(),
	      "click-click-click wiring with a bend");

	// Branch: drag from the middle of a wire to another pin
	gui::Editor f;
	f.setRunning(false);
	f.startPlacing(Kind::Switch);
	click(f, 3, 3); // switch at (2, 2), output (4, 3)
	f.startPlacing(Kind::Led);
	click(f, 14, 3); // LED at (13, 2), input (13, 3)
	f.startPlacing(Kind::Led);
	click(f, 14, 9); // LED at (13, 8), input (13, 9)
	drag(f, 4, 3, 13, 3);
	drag(f, 8.2f, 3.1f, 13, 9);
	const auto& fw = f.document().main.wires;
	check(fw.size() == 2, std::format("branch added a second wire ({})", fw.size()));
	if (fw.size() == 2) {
		check(fw[1].from == fw[0].from, "the branch starts at the same output");
		check(fw[1].bends.size() == 1 && fw[1].bends[0] == Point{8, 3},
		      "and follows the wire to the grab point");
		int swId = fw[0].from.component;
		click(f, 2.6f, 2.5f);
		f.runTicks(3);
		check(f.pinValue(fw[0].to.component, 0) == Value::of(1, 1) &&
		          f.pinValue(fw[1].to.component, 0) == Value::of(1, 1),
		      "both LEDs follow the switch");
		check(f.document().main.find(swId)->props.value == 1, "switch is on");
	}
	// A click (no drag) on a wire only selects it
	click(f, 6.f, 3.f);
	check(f.document().main.wires.size() == 2 && f.selection().empty(), "click on a wire selects it");
	check(f.overlaps().empty(), "a branch running along its own wire is not an overlap");

	// Wires of different signals on the same grid line are found (they look connected)
	gui::Editor h;
	h.setRunning(false);
	h.startPlacing(Kind::Switch);
	click(h, 3, 3); // output (4, 3)
	h.startPlacing(Kind::Led);
	click(h, 14, 3); // LED at (13, 2), input (13, 3)
	h.startPlacing(Kind::Switch);
	click(h, 3, 7); // output (4, 7)
	h.startPlacing(Kind::Led);
	click(h, 14, 7); // input (13, 7)
	drag(h, 4, 3, 13, 3);
	click(h, 4, 7);
	click(h, 6, 3); // bends that run along the first wire from x = 6 to 10
	click(h, 10, 3);
	click(h, 10, 7);
	click(h, 13, 7);
	check(h.document().main.wires.size() == 2, "two wires");
	auto same = [](const gui::Segment& s, Point a, Point b) {
		return (s.a == a && s.b == b) || (s.a == b && s.b == a);
	};
	check(h.overlaps().size() == 1 && same(h.overlaps()[0], {6, 3}, {10, 3}),
	      std::format("the overlap from (6, 3) to (10, 3) is found ({})", h.overlaps().size()));
	drag(h, 14.2f, 2.4f, 14.2f, 3.4f); // the first LED one row down: its wire now turns at x = 8
	check(h.overlaps().size() == 1 && same(h.overlaps()[0], {6, 3}, {8, 3}),
	      "moving a part updates the overlaps");
	h.undo();
	check(h.overlaps().size() == 1 && same(h.overlaps()[0], {6, 3}, {10, 3}), "and so does undo");

	// Editing keeps the simulation going: a counter keeps its count when it is moved, rotated or
	// wired up, and only Reset (power on) clears it
	gui::Editor g;
	g.setRunning(false);
	g.startPlacing(Kind::Clock);
	click(g, 3, 3); // clock at (2, 2), output (4, 3)
	g.startPlacing(Kind::Counter);
	click(g, 10, 5.5f); // counter at (8, 3), CLK (8, 7), Q (12, 4)
	const Component* counter = find(g, Kind::Counter);
	check(counter && counter->pos == Point{8, 3}, "counter placed");
	int counterId = counter ? counter->id : -1;
	const int Q = 5;
	drag(g, 4, 3, 8, 7);
	g.runTicks(40); // rising clock edges at 4, 12, 20, 28 and 36
	check(g.pinValue(counterId, Q) == Value::of(5, 4),
	      std::format("counter counted to 5 ({})", g.pinValue(counterId, Q).binary()));
	logic::Tick time = g.time();
	drag(g, 10, 5, 10, 9);
	check(g.document().main.find(counterId)->pos == Point{8, 7}, "counter moved");
	check(g.pinValue(counterId, Q) == Value::of(5, 4) && g.time() == time,
	      "moving keeps the count and the time");
	g.keyPressed(key(sf::Keyboard::Key::R));
	g.keyPressed(key(sf::Keyboard::Key::R));
	g.keyPressed(key(sf::Keyboard::Key::R));
	g.keyPressed(key(sf::Keyboard::Key::R));
	check(g.pinValue(counterId, Q) == Value::of(5, 4), "rotating keeps the count");
	g.startPlacing(Kind::Probe);
	click(g, 20, 8); // probe at (18, 7), input (18, 8)
	drag(g, 12, 8, 18, 8);
	check(g.document().main.wires.size() == 2, "probe wired to Q");
	check(g.pinValue(counterId, Q) == Value::of(5, 4), "adding parts and wires keeps the count");
	g.runTicks(8);
	check(g.pinValue(counterId, Q) == Value::of(6, 4), "and counting goes on");
	g.undo();
	check(g.pinValue(counterId, Q) == Value::of(6, 4), "undo keeps the count too");
	g.resetSimulation();
	g.runTicks(1);
	check(g.pinValue(counterId, Q) == Value::of(0, 4), "Reset powers on again: count 0");

	// Ctrl+Z in the middle of a drag takes the drag back (and Ctrl+Y does it again); Esc cancels it
	gui::Editor k;
	k.setRunning(false);
	k.startPlacing(Kind::And);
	click(k, 4, 4); // AND at (2, 2), 4 x 4
	int andId = k.document().main.components.front().id;
	auto halfDrag = [&](float x1, float y1, float x2, float y2) {
		k.mouseMoved(screen(x1, y1));
		k.mousePressed(Button::Left, screen(x1, y1), false);
		k.mouseMoved(screen(x2, y2));
	};
	halfDrag(4, 4, 10, 4);
	check(k.document().main.find(andId)->pos == Point{8, 2}, "the gate follows the mouse");
	k.keyPressed(key(sf::Keyboard::Key::Z, true));
	k.mouseMoved(screen(12, 4));
	k.mouseReleased(Button::Left, screen(12, 4));
	check(k.document().main.find(andId)->pos == Point{2, 2}, "Ctrl+Z during a drag takes it back");
	check(k.canRedo(), "and it can be redone");
	k.redo();
	check(k.document().main.find(andId)->pos == Point{8, 2}, "Ctrl+Y moves it again");
	halfDrag(10, 4, 10, 8);
	k.keyPressed(key(sf::Keyboard::Key::Escape));
	k.mouseReleased(Button::Left, screen(10, 8));
	check(k.document().main.find(andId)->pos == Point{8, 2}, "Esc during a drag cancels it");
	k.undo();
	check(k.document().main.find(andId)->pos == Point{2, 2}, "and leaves no undo step behind");

	// Text notes do not turn
	k.startPlacing(Kind::Text);
	k.rotateSelection();
	click(k, 20, 20);
	k.keyPressed(key(sf::Keyboard::Key::R));
	check(find(k, Kind::Text) && find(k, Kind::Text)->rotation == 0, "R leaves text notes alone");

	// A new wire whose usual route would run along another signal's wire takes another column
	gui::Editor r;
	r.setRunning(false);
	r.startPlacing(Kind::Switch);
	click(r, 3, 3); // output (4, 3)
	r.startPlacing(Kind::Switch);
	click(r, 3, 5); // output (4, 5)
	r.startPlacing(Kind::Led);
	click(r, 13, 9); // input (12, 9)
	r.startPlacing(Kind::Led);
	click(r, 13, 11);      // input (12, 11)
	drag(r, 4, 3, 12, 9);  // turns at x = 8
	drag(r, 4, 5, 12, 11); // would also turn at x = 8
	const auto& rw = r.document().main.wires;
	check(rw.size() == 2 && rw[0].bends.empty(), "a free route stays automatic");
	check(rw.size() == 2 && rw[1].bends.size() == 2 && rw[1].bends[0].x != 8,
	      "the second wire turns in another column");
	check(r.overlaps().empty(), "so the wires do not overlap");

	// Chips: make one from a selection, place it, open it, change it, go back
	gui::Editor c;
	c.setRunning(false);
	c.startPlacing(Kind::InputPin);
	click(c, 3, 3); // A at (2, 2), output (4, 3)
	c.startPlacing(Kind::InputPin);
	click(c, 3, 7); // B at (2, 6), output (4, 7)
	c.startPlacing(Kind::Xor);
	click(c, 10, 4); // XOR at (8, 2): in0 (8, 3), in1 (8, 5), out (12, 4)
	c.startPlacing(Kind::OutputPin);
	click(c, 17, 4); // input (16, 4)
	drag(c, 4, 3, 8, 3);
	drag(c, 4, 7, 8, 5);
	drag(c, 12, 4, 16, 4);
	c.keyPressed(key(sf::Keyboard::Key::A, true));
	check(c.createChip("Odd", "#336699", "1 when A and B differ").empty(), "chip made from the selection");
	check(!c.createChip("Odd", "", "").empty(), "a second chip with the same name is refused");
	c.cancel();
	check(!c.createChip("Other", "", "").empty(), "a chip needs its pins in the selection");
	check(c.document().library.find("Odd") && c.document().main.components.size() == 4,
	      "the chip is in the library and the circuit stays");

	c.startPlacingChip("Odd");
	click(c, 21.5f, 11.5f); // 3 x 3 at (20, 10): in0 (20, 11), in1 (20, 12), out0 (23, 11)
	const Component* odd = find(c, Kind::Chip);
	check(odd && odd->pos == Point{20, 10}, "chip instance placed");
	int oddId = odd ? odd->id : -1;
	c.startPlacing(Kind::Switch);
	click(c, 15, 10); // output (16, 10)
	int s1 = c.document().main.components.back().id;
	c.startPlacing(Kind::Switch);
	click(c, 15, 14); // output (16, 14)
	c.startPlacing(Kind::Led);
	click(c, 27, 11); // input (26, 11)
	int ledOut = c.document().main.components.back().id;
	drag(c, 16, 10, 20, 11);
	drag(c, 16, 14, 20, 12);
	drag(c, 23, 11, 26, 11);
	check(c.document().main.wires.size() == 6, "instance wired");
	click(c, 14.6f, 9.5f); // switch 1 on
	c.runTicks(10);
	check(c.pinValue(ledOut, 0) == Value::of(1, 1), "1 xor 0 lights the LED");

	c.doubleClicked(screen(21.5f, 11.5f));
	check(c.editPath().size() == 1 && c.editPath()[0] == "Odd" && c.circuit().components.size() == 4,
	      "a double click opens the chip");
	check(!c.canPlace("Odd"), "a chip cannot be placed inside itself");
	c.startPlacingChip("Odd");
	check(!c.placing(), "so it is not placed");
	check(!c.deleteChip("Odd"), "a chip in use cannot be deleted");
	// Moving input B above A swaps the chip's pins: the wires outside go along with them (the view
	// was fitted to the chip, so grid points go through its camera)
	auto at = [&](float x, float y) { return c.camera().toScreen(ImVec2{x, y}); };
	c.mouseMoved(at(2.6f, 6.5f));
	c.mousePressed(Button::Left, at(2.6f, 6.5f), false);
	c.mouseMoved(at(2.6f, 3.5f));
	c.mouseMoved(at(2.6f, 0.5f));
	c.mouseReleased(Button::Left, at(2.6f, 0.5f));
	check(c.circuit().components[1].pos == Point{2, 0}, "input B moved up inside the chip");
	c.leaveTo(0);
	check(c.editPath().empty(), "back in the main circuit");
	const Wire* fromS1 = nullptr;
	for (const Wire& w : c.document().main.wires) {
		if (w.from.component == s1) {
			fromS1 = &w;
		}
	}
	check(fromS1 && fromS1->to.component == oddId && fromS1->to.pin == 1,
	      "the wire to input A follows it to the second pin");
	check(c.pinValue(ledOut, 0) == Value::of(1, 1), "and the circuit still works");

	c.enterChip("Odd");
	while (c.canUndo() && c.document().library.find("Odd")) {
		c.undo();
	}
	check(c.editPath().empty() && !c.document().library.find("Odd"), "undoing the chip's creation leaves it");

	// The example circuits have no wires of different signals on top of each other
	for (const auto& entry : std::filesystem::directory_iterator(LOGICSIM_EXAMPLES_DIR)) {
		if (entry.path().extension() != ".json") {
			continue;
		}
		gui::Editor example;
		std::string error;
		bool opened = example.open(entry.path(), error);
		std::string name = entry.path().filename().string();
		check(opened, std::format("{} opens: {}", name, error));
		check(example.overlaps().empty(),
		      std::format("{}: no overlapping wires ({})", name, example.overlaps().size()));
	}

	return test::report("editor_test");
}
