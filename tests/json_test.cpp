// Circuit files: save -> load -> save gives the same text, the loaded circuit still works, and
// broken files are reported instead of crashing.
#include "bench.hpp"
#include "chips.hpp"
#include "core/json_io.hpp"

using namespace test;

namespace {

Document adderDocument(int& a, int& bIn, int& s) {
	Document doc;
	addAdders(doc.library);
	Circuit& c = doc.main;
	c.name = "Adder demo";
	Props p4 = defaultProps(Kind::InputPin);
	p4.width = 4;
	a = c.add(Kind::InputPin, 0, 0, p4, "A");
	bIn = c.add(Kind::InputPin, 0, 4, p4, "B");
	Props chipProps = defaultProps(Kind::Chip);
	chipProps.chip = "4-bit adder";
	int adder = c.add(Kind::Chip, 8, 0, chipProps);
	Props out4 = defaultProps(Kind::OutputPin);
	out4.width = 4;
	s = c.add(Kind::OutputPin, 20, 0, out4, "S");
	Props rom = defaultProps(Kind::Rom);
	rom.data = {1, 2, 3};
	int romId = c.add(Kind::Rom, 0, 10, rom, "table");
	c.find(romId)->rotation = 1;
	check(connect(c, doc.library, a, "out", adder, "A"), "wire A");
	check(connect(c, doc.library, bIn, "out", adder, "B"), "wire B");
	check(connect(c, doc.library, adder, "S", s, "in"), "wire S");
	c.wires.back().bends = {{14, 1}, {14, 3}};
	int zero = c.add(Kind::Constant, 0, 8);
	check(connect(c, doc.library, zero, "out", adder, "Cin"), "wire Cin");
	return doc;
}

void testRoundTrip() {
	int a = 0, bIn = 0, s = 0;
	Document doc = adderDocument(a, bIn, s);
	std::string text = saveDocument(doc);
	LoadResult loaded = loadDocument(text);
	check(loaded.ok, "loads: " + loaded.error);
	check(loaded.warnings.empty(), "no warnings");
	check(saveDocument(loaded.document) == text, "save -> load -> save is identical");
	check(loaded.document.main.name == "Adder demo", "circuit name kept");
	check(loaded.document.library.chips.size() == 3, "three chips");
	check(loaded.document.library.chips.at("Half adder").color == "#3a6ea5", "chip colour kept");
	const Component* rom = nullptr;
	for (const Component& c : loaded.document.main.components) {
		if (c.kind == Kind::Rom) {
			rom = &c;
		}
	}
	check(rom != nullptr && rom->rotation == 1 && rom->props.data.size() == 16 && rom->props.data[2] == 3 &&
	          rom->label == "table",
	      "ROM contents, rotation and label kept");
	check(loaded.document.main.wires.size() == 4 && loaded.document.main.wires[2].bends.size() == 2,
	      "wire bends kept");

	// The loaded adder adds
	Bench b;
	b.doc = loaded.document;
	b.build();
	b.set(a, 9);
	b.set(bIn, 5);
	b.settle();
	check(b.get(s) == 14, "loaded adder: 9 + 5 = 14");
}

void testBrokenFiles() {
	check(!loadDocument("not json").ok, "garbage is rejected");
	check(!loadDocument(R"({"format": "other"})").ok, "other JSON is rejected");
	LoadResult partial = loadDocument(R"({
		"format": "logicsim", "version": 1,
		"circuit": {
			"components": [
				{"id": 1, "type": "and", "x": 0, "y": 0},
				{"id": 2, "type": "flux_capacitor", "x": 0, "y": 0},
				{"id": 3, "type": "led", "x": 5, "y": 0, "props": {"color": "#ff0000"}}
			],
			"wires": [
				{"from": [1, "out"], "to": [3, "in"]},
				{"from": [1, "nope"], "to": [3, "in"]},
				{"from": [2, "out"], "to": [3, "in"]}
			]
		}
	})");
	check(partial.ok, "a file with unknown parts still loads");
	check(partial.warnings.size() == 3, std::format("three warnings ({})", partial.warnings.size()));
	check(partial.document.main.components.size() == 2 && partial.document.main.wires.size() == 1,
	      "the good parts are kept");
	check(partial.document.main.components[1].props.color == "#ff0000", "props read");
	check(partial.document.main.nextId == 4, "new ids continue after the largest");
}

} // namespace

int main() {
	testRoundTrip();
	testBrokenFiles();
	return report("json_test");
}
