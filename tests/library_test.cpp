// The editor's chip commands on the document: making a chip from a selection, wires to instances
// following a chip's pins when they change, renaming, counting instances and importing chips.
#include "bench.hpp"
#include "chips.hpp"
#include "core/json_io.hpp"

using namespace test;

namespace {

int chipInstance(Bench& b, const std::string& name) {
	Props p = defaultProps(Kind::Chip);
	p.chip = name;
	return b.add(Kind::Chip, p);
}

void testChipFromSelection() {
	// A half adder built in the main circuit, with a switch that is not selected
	Bench b;
	Circuit& c = b.circuit();
	int a = pin(c, Kind::InputPin, 4, "A"), bb = pin(c, Kind::InputPin, 6, "B");
	int s = pin(c, Kind::OutputPin, 4, "S"), carry = pin(c, Kind::OutputPin, 6, "C");
	int x = c.add(Kind::Xor, 10, 4), n = c.add(Kind::And, 10, 8);
	int other = c.add(Kind::Switch, 40, 40);
	b.wire(a, "out", x, "in0");
	b.wire(bb, "out", x, "in1");
	b.wire(a, "out", n, "in0");
	b.wire(bb, "out", n, "in1");
	b.wire(x, "out", s, "in");
	b.wire(n, "out", carry, "in");
	b.wire(other, "out", n, "in1"); // a wire leaving the selection is not taken along

	ChipDef chip = chipFromSelection(c, {a, bb, s, carry, x, n});
	check(chip.circuit.components.size() == 6 && chip.circuit.wires.size() == 6,
	      "the selected parts and the wires between them");
	check(chip.circuit.find(a) && chip.circuit.find(a)->pos == Point{2, 2}, "moved to start near (0, 0)");
	check(chip.circuit.nextId > n, "new ids inside the chip do not clash");
	b.doc.library.chips["HA"] = std::move(chip);
	ComponentSpec spec = chipSpec(*b.doc.library.find("HA"), "HA");
	check(spec.pins.size() == 4 && spec.pins[0].name == "A" && spec.pins[3].name == "C", "pins A, B, S, C");

	Bench t;
	t.doc.library = b.doc.library;
	int h = chipInstance(t, "HA");
	int ia = t.input(), ib = t.input(), os = t.output(), oc = t.output();
	t.wire(ia, "out", h, "A");
	t.wire(ib, "out", h, "B");
	t.wire(h, "S", os, "in");
	t.wire(h, "C", oc, "in");
	t.build();
	t.set(ia, 1);
	t.set(ib, 1);
	t.settle();
	check(t.get(os) == 0 && t.get(oc) == 1, "the new chip adds: 1 + 1 = 10");
	check(countInstances(t.doc, "HA") == 1 && countInstances(t.doc, "nope") == 0, "instances counted");
}

void testPinsFollow() {
	Bench b;
	addHalfAdder(b.doc.library);
	int h = chipInstance(b, "Half adder");
	int ia = b.input(), ib = b.input(), os = b.output(), oc = b.output();
	b.wire(ia, "out", h, "A");
	b.wire(ib, "out", h, "B");
	b.wire(h, "S", os, "in");
	b.wire(h, "C", oc, "in");
	Circuit& inside = b.doc.library.chips.at("Half adder").circuit;

	// Renaming pin A keeps its wire
	std::vector<int> before = portIds(inside);
	int pinA = before[0], pinB = before[1];
	inside.find(pinA)->label = "X";
	remapChipPins(b.doc, "Half adder", before);
	check(b.doc.main.wires.size() == 4, "renaming a pin keeps the wires");

	// Moving B above A swaps the pins: the wires swap with them
	before = portIds(inside);
	inside.find(pinB)->pos.y = -2;
	remapChipPins(b.doc, "Half adder", before);
	ComponentSpec spec = specOf(*b.doc.main.find(h), b.doc.library);
	check(spec.pins[0].name == "B" && spec.pins[1].name == "X", "B is now the first pin");
	const Wire& fromA = b.doc.main.wires[0];
	check(fromA.from.component == ia && fromA.to.pin == 1, "the wire from input A went along to pin X");
	b.build();
	b.set(ia, 1);
	b.set(ib, 0);
	b.settle();
	check(b.get(os) == 1 && b.get(oc) == 0, "and the chip still adds 1 + 0");

	// Removing output C removes its wire, the others stay
	before = portIds(inside);
	inside.remove(before[3]);
	remapChipPins(b.doc, "Half adder", before);
	check(b.doc.main.wires.size() == 3, "a removed pin takes its wire along");
}

void testRename() {
	Document doc;
	addAdders(doc.library);
	Props p = defaultProps(Kind::Chip);
	p.chip = "Full adder";
	doc.main.add(Kind::Chip, 0, 0, p);
	check(!renameChip(doc, "Full adder", "Half adder"), "a taken name is refused");
	check(!renameChip(doc, "Full adder", ""), "an empty name is refused");
	check(renameChip(doc, "Full adder", "FA"), "renamed");
	check(doc.library.find("FA") && !doc.library.find("Full adder"), "the library has the new name");
	check(doc.main.components[0].props.chip == "FA", "the instance in the main circuit follows");
	check(countInstances(doc, "FA") == 5, "and the four in the 4-bit adder (5 in all)");
	Bench b;
	b.doc = doc;
	b.build();
}

void testMerge() {
	Library mine;
	addAdders(mine);
	// The other library: the same half adder, a different full adder, and a new chip using it
	Library theirs;
	addHalfAdder(theirs);
	ChipDef full;
	full.circuit.add(Kind::InputPin, 0, 0, defaultProps(Kind::InputPin), "only");
	theirs.chips["Full adder"] = full;
	ChipDef user;
	Props p = defaultProps(Kind::Chip);
	p.chip = "Full adder";
	user.circuit.add(Kind::Chip, 0, 0, p);
	theirs.chips["User"] = user;

	std::vector<std::string> messages = mergeLibrary(mine, theirs);
	check(mine.chips.size() == 5, std::format("two chips added ({})", mine.chips.size()));
	check(messages.size() == 1, "one chip had to be renamed");
	check(mine.find("Full adder (2)") && mine.find("Full adder (2)")->circuit.components.size() == 1,
	      "the different full adder came in under a new name");
	check(mine.find("User") && mine.find("User")->circuit.components[0].props.chip == "Full adder (2)",
	      "and the chip using it follows the new name");
	check(mine.find("Full adder")->circuit.components.size() > 1, "our full adder is untouched");

	// A library file (no main circuit) loads like any other file
	LoadResult loaded = loadDocument(saveLibrary(mine));
	check(loaded.ok && loaded.document.library.chips.size() == 5 && loaded.document.main.components.empty(),
	      "a saved library loads");
	check(mergeLibrary(mine, loaded.document.library).empty() && mine.chips.size() == 5,
	      "importing the same chips again changes nothing");
}

} // namespace

int main() {
	testChipFromSelection();
	testPinsFollow();
	testRename();
	testMerge();
	return report("library_test");
}
