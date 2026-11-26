// Every example circuit loads, compiles without errors and settles; the shift register example
// loads in parallel and shifts serially.
#include "bench.hpp"
#include "core/json_io.hpp"

#include <filesystem>

using namespace test;

namespace {

int byLabel(const Circuit& c, std::string_view label) {
	for (const Component& component : c.components) {
		if (component.label == label) {
			return component.id;
		}
	}
	check(false, std::format("a component labelled {}", label));
	return -1;
}

void testAllExamples() {
	int count = 0;
	for (const auto& entry : std::filesystem::directory_iterator(LOGICSIM_EXAMPLES_DIR)) {
		if (entry.path().extension() != ".json") {
			continue;
		}
		std::string name = entry.path().filename().string();
		LoadResult loaded = loadFile(entry.path());
		check(loaded.ok, name + " loads: " + loaded.error);
		check(loaded.warnings.empty(), name + " loads without warnings");
		Bench b;
		b.doc = loaded.document;
		b.build();
		b.sim.advance(200, 1'000'000);
		count++;
	}
	check(count >= 3, std::format("found the examples ({})", count));
}

void testShiftRegister() {
	LoadResult loaded = loadFile(std::filesystem::path(LOGICSIM_EXAMPLES_DIR) / "shift-register.json");
	if (!check(loaded.ok, "shift-register.json loads")) {
		return;
	}
	Bench b;
	b.doc = loaded.document;
	b.build();
	const Circuit& c = b.doc.main;
	int d = byLabel(c, "D"), si = byLabel(c, "SI"), load = byLabel(c, "LOAD"), clk = byLabel(c, "CLK");
	int q = byLabel(c, "Q");
	// Inputs settle before the clock edge (changing them in the same tick as the edge would be
	// a setup-time violation: the flip-flops would still see the multiplexers' old outputs)
	auto pulse = [&] {
		b.settle();
		b.set(clk, 1);
		b.settle();
		b.set(clk, 0);
		b.settle();
	};
	b.settle();
	check(b.get(q) == 0, "starts at 0");

	b.set(d, 0b1011);
	b.set(load, 1);
	b.settle();
	check(b.get(q) == 0, "nothing happens before the clock");
	pulse();
	check(b.get(q) == 0b1011, std::format("parallel load 1011 (got {})", b.read(q).binary()));

	b.set(load, 0);
	b.set(si, 0);
	pulse();
	check(b.get(q) == 0b0110, std::format("shift in 0: 0110 (got {})", b.read(q).binary()));
	b.set(si, 1);
	pulse();
	check(b.get(q) == 0b1101, std::format("shift in 1: 1101 (got {})", b.read(q).binary()));
	for (int i = 0; i < 4; i++) {
		pulse();
	}
	check(b.get(q) == 0b1111, "four more 1s fill it");
}

} // namespace

int main() {
	testAllExamples();
	testShiftRegister();
	return report("examples_test");
}
