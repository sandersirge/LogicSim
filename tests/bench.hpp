#pragma once

// A test bench: build a circuit in code, compile it, drive its switches / input pins and read its
// wires. Used by the engine tests (no window needed).
#include "check.hpp"
#include "core/compiler.hpp"
#include "core/simulator.hpp"

#include <format>
#include <string>
#include <string_view>

namespace test {

using namespace logic;

struct Bench {
	Document doc;
	Compiled compiled;
	Simulator sim;

	Circuit& circuit() { return doc.main; }

	int add(Kind kind, Props props, std::string label = {}) {
		return doc.main.add(kind, 0, 0, std::move(props), std::move(label));
	}
	int add(Kind kind) { return doc.main.add(kind); }
	// A gate with n inputs (and optional width / delay)
	int gate(Kind kind, int inputs = 2, int width = 1, int delay = 1) {
		Props p = defaultProps(kind);
		p.inputs = inputs;
		p.width = width;
		p.delay = delay;
		return add(kind, p);
	}
	// An input pin of the given width (a switch for 1 bit is the same thing to the engine)
	int input(int width = 1, std::string label = {}) {
		Props p = defaultProps(Kind::InputPin);
		p.width = width;
		return add(Kind::InputPin, p, std::move(label));
	}
	int output(int width = 1, std::string label = {}) {
		Props p = defaultProps(Kind::OutputPin);
		p.width = width;
		return add(Kind::OutputPin, p, std::move(label));
	}
	void wire(int from, std::string_view fromPin, int to, std::string_view toPin) {
		check(connect(doc.main, doc.library, from, fromPin, to, toPin),
		      std::format("connect {}.{} -> {}.{}", from, fromPin, to, toPin));
	}

	// Compiles and powers on. Returns false (and reports) if the compiler found errors.
	bool build(bool expectErrors = false) {
		compiled = compile(doc.main, doc.library);
		if (!expectErrors) {
			for (const Diagnostic& d : compiled.diagnostics) {
				if (d.error) {
					check(false, std::format("compile error at {}: {}", d.component, d.message));
				}
			}
		}
		sim = Simulator(compiled.netlist);
		return !compiled.hasErrors();
	}

	void set(int id, std::uint32_t value) {
		int prim = compiled.prim(id);
		if (!check(prim >= 0, std::format("component {} is not a source", id))) {
			return;
		}
		sim.setSource(prim, Value::of(value, sim.prim(prim).source.width));
	}

	// The value on a pin of a component
	Value at(int id, std::string_view pin) {
		const Component* c = doc.main.find(id);
		if (!check(c != nullptr, std::format("component {} exists", id))) {
			return Value::x(1);
		}
		int index = specOf(*c, doc.library).pinIndex(pin);
		int net = compiled.net(id, index);
		if (!check(net >= 0, std::format("pin {}.{} has a net", id, pin))) {
			return Value::x(1);
		}
		return sim.value(net);
	}
	// The value an output pin / LED / probe reads
	Value read(int id) { return at(id, "in"); }

	SettleResult settle() { return sim.settle(); }
	// Settles and reads a fully known value (or ~0u when not fully known)
	std::uint32_t get(int id) {
		Value v = read(id);
		return v.fullyKnown() ? v.bits : ~0u;
	}
};

} // namespace test
