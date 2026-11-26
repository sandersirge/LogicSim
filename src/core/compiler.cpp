#include "core/compiler.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <numeric>
#include <optional>
#include <set>

namespace logic {

bool Compiled::hasErrors() const {
	return std::ranges::any_of(diagnostics, &Diagnostic::error);
}

int Compiled::net(int component, int pin) const {
	auto it = pinNets.find(component);
	if (it == pinNets.end() || pin < 0 || pin >= static_cast<int>(it->second.size())) {
		return -1;
	}
	return it->second[static_cast<size_t>(pin)];
}

int Compiled::prim(int component) const {
	auto it = prims.find(component);
	return it == prims.end() ? -1 : it->second;
}

namespace {

constexpr int MAX_DEPTH = 32;

// Every pin of every instantiated component is a node; wires, tunnels and chip boundaries join
// nodes into nets (union-find).
struct Node {
	int width = 1;
	int owner = -1; // top-level component id for messages
	std::optional<std::uint32_t> floating;
};

struct PendingPrim {
	Prim prim;
	std::vector<int> inNodes;
	std::vector<int> outNodes;
};

bool isSink(Kind kind) {
	return kind == Kind::Led || kind == Kind::SevenSegment || kind == Kind::HexDisplay ||
	       kind == Kind::Probe || kind == Kind::Text;
}

class Builder {
public:
	Builder(const Library& library, Compiled& result) : library_(library), result_(result) {}

	void build(const Circuit& top) {
		std::vector<std::vector<int>> nodes = instantiate(top, nullptr, -1);
		resolve();
		for (size_t i = 0; i < top.components.size(); i++) {
			std::vector<int>& nets = result_.pinNets[top.components[i].id];
			for (int node : nodes[i]) {
				nets.push_back(nodeNet_[static_cast<size_t>(node)]);
			}
		}
	}

private:
	int newNode(int width, int owner, std::optional<std::uint32_t> floating = std::nullopt) {
		nodes_.push_back(Node{width, owner, floating});
		parent_.push_back(static_cast<int>(parent_.size()));
		return static_cast<int>(nodes_.size()) - 1;
	}

	int find(int node) {
		while (parent_[static_cast<size_t>(node)] != node) {
			int& p = parent_[static_cast<size_t>(node)];
			p = parent_[static_cast<size_t>(p)];
			node = p;
		}
		return node;
	}

	void join(int a, int b) {
		a = find(a);
		b = find(b);
		if (a != b) {
			parent_[static_cast<size_t>(b)] = a;
		}
	}

	void error(int owner, std::string message) {
		for (const Diagnostic& d : result_.diagnostics) {
			if (d.component == owner && d.message == message) {
				return;
			}
		}
		result_.diagnostics.push_back(Diagnostic{owner, std::move(message), true});
	}

	// Creates the nodes and primitives of a circuit. ports: the nodes of the chip instance's pins
	// (inputs, then outputs), or null for the top level. Returns the nodes of each component's pins.
	std::vector<std::vector<int>> instantiate(const Circuit& circuit, const std::vector<int>* ports,
	                                          int owner) {
		std::vector<std::vector<int>> pinNodes(circuit.components.size());
		std::map<std::string, int, std::less<>> tunnels;
		ChipPorts chipPortList = chipPorts(circuit);
		bool top = ports == nullptr;

		for (size_t i = 0; i < circuit.components.size(); i++) {
			const Component& c = circuit.components[i];
			int who = top ? c.id : owner;
			ComponentSpec spec = specOf(c, library_);
			for (const PinSpec& pin : spec.pins) {
				pinNodes[i].push_back(newNode(pin.width, who, pin.output ? std::nullopt : pin.floating));
			}
			std::vector<int>& nodes = pinNodes[i];
			int index = static_cast<int>(i);

			switch (c.kind) {
			case Kind::Text:
				break;
			case Kind::Tunnel: {
				auto [it, added] = tunnels.try_emplace(c.props.text, nodes[0]);
				if (!added) {
					join(it->second, nodes[0]);
				}
				break;
			}
			case Kind::InputPin:
				if (top) {
					addPrim(c, who, {}, nodes);
				} else {
					auto pos = std::ranges::find(chipPortList.inputs, index) - chipPortList.inputs.begin();
					join(nodes[0], (*ports)[static_cast<size_t>(pos)]);
				}
				break;
			case Kind::OutputPin:
				if (!top) {
					auto pos = std::ranges::find(chipPortList.outputs, index) - chipPortList.outputs.begin();
					join(nodes[0], (*ports)[chipPortList.inputs.size() + static_cast<size_t>(pos)]);
				}
				break;
			case Kind::Chip:
				instantiateChip(c, nodes, who);
				break;
			default: {
				if (isSink(c.kind)) {
					break;
				}
				std::vector<int> ins, outs;
				for (size_t p = 0; p < spec.pins.size(); p++) {
					(spec.pins[p].output ? outs : ins).push_back(nodes[p]);
				}
				addPrim(c, who, ins, outs);
				break;
			}
			}
		}

		for (const Wire& w : circuit.wires) {
			int a = nodeOf(circuit, pinNodes, w.from);
			int b = nodeOf(circuit, pinNodes, w.to);
			if (a < 0 || b < 0) {
				error(top ? (a < 0 ? w.from.component : w.to.component) : owner,
				      top ? "A wire ends at a pin that does not exist" : "A chip contains a broken wire");
				continue;
			}
			join(a, b);
		}
		return pinNodes;
	}

	int nodeOf(const Circuit& circuit, const std::vector<std::vector<int>>& pinNodes, PinRef ref) {
		auto it = std::ranges::find(circuit.components, ref.component, &Component::id);
		if (it == circuit.components.end()) {
			return -1;
		}
		const std::vector<int>& nodes = pinNodes[static_cast<size_t>(it - circuit.components.begin())];
		return ref.pin >= 0 && ref.pin < static_cast<int>(nodes.size()) ? nodes[static_cast<size_t>(ref.pin)]
		                                                                : -1;
	}

	void instantiateChip(const Component& c, const std::vector<int>& nodes, int who) {
		const ChipDef* def = library_.find(c.props.chip);
		if (def == nullptr) {
			error(who, std::format("Unknown chip \"{}\"", c.props.chip));
			return;
		}
		if (std::ranges::find(stack_, c.props.chip) != stack_.end() ||
		    static_cast<int>(stack_.size()) >= MAX_DEPTH) {
			error(who, std::format("Chip \"{}\" contains itself", c.props.chip));
			return;
		}
		stack_.push_back(c.props.chip);
		instantiate(def->circuit, &nodes, who);
		stack_.pop_back();
	}

	void addPrim(const Component& c, int who, std::vector<int> ins, std::vector<int> outs) {
		Prim prim;
		prim.kind = c.kind;
		prim.props = c.props;
		prim.delay = c.props.delay;
		prim.owner = who;
		bool top = stack_.empty();
		pending_.push_back(PendingPrim{std::move(prim), std::move(ins), std::move(outs)});
		if (top) {
			topPrims_.emplace_back(c.id, static_cast<int>(pending_.size()) - 1);
		}
	}

	// Nets from the joined nodes; then the primitives are connected to them
	void resolve() {
		Netlist& netlist = result_.netlist;
		size_t n = nodes_.size();
		std::vector<int> classNet(n, -1);
		nodeNet_.assign(n, -1);
		for (size_t i = 0; i < n; i++) {
			int root = find(static_cast<int>(i));
			int& net = classNet[static_cast<size_t>(root)];
			if (net < 0) {
				net = netlist.addNet(nodes_[static_cast<size_t>(root)].width);
			}
			if (netlist.nets[static_cast<size_t>(net)].width == nodes_[i].width) {
				nodeNet_[i] = net;
			} else {
				// A pin of a different width gets a net of its own (unconnected) and an error
				error(nodes_[i].owner,
				      std::format("Width mismatch: a {}-bit pin is wired to a {}-bit wire", nodes_[i].width,
				                  netlist.nets[static_cast<size_t>(net)].width));
				nodeNet_[i] = netlist.addNet(nodes_[i].width);
			}
		}

		std::vector<int> primIndex(pending_.size());
		for (size_t i = 0; i < pending_.size(); i++) {
			PendingPrim& pp = pending_[i];
			for (int node : pp.inNodes) {
				pp.prim.in.push_back(nodeNet_[static_cast<size_t>(node)]);
			}
			for (int node : pp.outNodes) {
				int net = nodeNet_[static_cast<size_t>(node)];
				if (netlist.nets[static_cast<size_t>(net)].driver >= 0 || driven_.contains(net)) {
					error(pp.prim.owner, "Two outputs are connected to each other");
					net = netlist.addNet(nodes_[static_cast<size_t>(node)].width);
				}
				driven_.insert(net);
				pp.prim.out.push_back(net);
			}
			primIndex[i] = netlist.addPrim(std::move(pp.prim));
		}
		for (auto [id, pending] : topPrims_) {
			result_.prims[id] = primIndex[static_cast<size_t>(pending)];
		}

		// Undriven nets read by an optional pin (enable, reset...) get that pin's default value
		for (size_t i = 0; i < n; i++) {
			const Node& node = nodes_[i];
			int net = nodeNet_[i];
			if (!node.floating || netlist.nets[static_cast<size_t>(net)].driver >= 0) {
				continue;
			}
			Prim constant;
			constant.kind = Kind::Constant;
			constant.props.width = node.width;
			constant.props.value = *node.floating;
			constant.delay = 0;
			constant.owner = node.owner;
			constant.out.push_back(net);
			netlist.addPrim(std::move(constant));
		}
	}

	const Library& library_;
	Compiled& result_;
	std::vector<Node> nodes_;
	std::vector<int> parent_;
	std::vector<int> nodeNet_;
	std::vector<PendingPrim> pending_;
	std::vector<std::pair<int, int>> topPrims_;
	std::vector<std::string> stack_;
	std::set<int> driven_;
};

} // namespace

Compiled compile(const Circuit& circuit, const Library& library) {
	Compiled result;
	Builder builder(library, result);
	builder.build(circuit);
	return result;
}

} // namespace logic
