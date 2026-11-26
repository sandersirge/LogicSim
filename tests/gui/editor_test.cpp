// The editor driven like a user would: place parts, wire them, flip a switch, move, undo/redo,
// delete, rotate, copy and paste. No window is opened.
#include "check.hpp"
#include "gui/editor.hpp"

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

	return test::report("editor_test");
}
