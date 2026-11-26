#include "core/simulator.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace logic {

namespace {

// Same kind and pins of the same widths: the stored state and the output values still fit
bool sameShape(const Prim& a, const Netlist& na, const Prim& b, const Netlist& nb) {
	if (a.kind != b.kind || a.in.size() != b.in.size() || a.out.size() != b.out.size()) {
		return false;
	}
	for (size_t i = 0; i < a.in.size(); i++) {
		if (na.nets[static_cast<size_t>(a.in[i])].width != nb.nets[static_cast<size_t>(b.in[i])].width) {
			return false;
		}
	}
	for (size_t i = 0; i < a.out.size(); i++) {
		if (na.nets[static_cast<size_t>(a.out[i])].width != nb.nets[static_cast<size_t>(b.out[i])].width) {
			return false;
		}
	}
	return true;
}

} // namespace

Simulator::Simulator(Netlist netlist) : netlist_(std::move(netlist)) {
	reset();
}

void Simulator::reset() {
	size_t n = netlist_.nets.size();
	values_.assign(n, Value{});
	for (size_t i = 0; i < n; i++) {
		values_[i] = Value::x(netlist_.nets[i].width);
	}
	scheduled_ = values_;
	scheduledTime_.assign(n, NEVER);
	scheduledSeq_.assign(n, 0);
	changed_.assign(n, NEVER);
	queue_ = {};
	cancelled_.clear();
	seq_ = 0;
	now_ = 0;
	netEvents_ = 0;
	totalEvents_ = 0;

	for (size_t i = 0; i < netlist_.prims.size(); i++) {
		Prim& p = netlist_.prims[i];
		resetState(p);
		int index = static_cast<int>(i);
		if (isSource(p.kind)) {
			schedule(p.out[0], p.source, 0);
			if (p.kind == Kind::Clock) {
				queue_.push(Event{static_cast<Tick>(p.props.period / 2), seq_++, -index - 1, {}});
			}
		}
	}
	// Stored values (flip-flops, registers, counters, memories) show up on their outputs at once,
	// without waiting for their inputs to change.
	for (size_t i = 0; i < netlist_.prims.size(); i++) {
		const Prim& p = netlist_.prims[i];
		switch (p.kind) {
		case Kind::SrLatch:
		case Kind::DLatch:
		case Kind::DFlipFlop:
		case Kind::JkFlipFlop:
		case Kind::TFlipFlop:
			schedule(p.out[0], p.state, 0);
			schedule(p.out[1], logicNot(p.state), 0);
			break;
		case Kind::Register:
		case Kind::Counter:
			schedule(p.out[0], p.state, 0);
			break;
		default:
			break;
		}
	}
}

void Simulator::carryOver(const Simulator& previous) {
	reset();
	// The power-on events are replaced by what follows
	queue_ = {};
	cancelled_.clear();
	netEvents_ = 0;
	scheduledTime_.assign(scheduledTime_.size(), NEVER);
	now_ = previous.now_;
	totalEvents_ = previous.totalEvents_;

	std::map<std::vector<int>, int> before;
	for (size_t i = 0; i < previous.netlist_.prims.size(); i++) {
		const Prim& p = previous.netlist_.prims[i];
		if (!p.path.empty()) {
			before.emplace(p.path, static_cast<int>(i));
		}
	}
	// When each clock of the previous simulation was going to toggle next
	std::unordered_map<int, Tick> toggles;
	for (auto pending = previous.queue_; !pending.empty(); pending.pop()) {
		if (pending.top().net < 0) {
			toggles.emplace(-pending.top().net - 1, pending.top().time);
		}
	}

	for (size_t i = 0; i < netlist_.prims.size(); i++) {
		Prim& p = netlist_.prims[i];
		int index = static_cast<int>(i);
		auto match = p.path.empty() ? before.end() : before.find(p.path);
		const Prim* old = nullptr;
		if (match != before.end()) {
			const Prim& candidate = previous.netlist_.prims[static_cast<size_t>(match->second)];
			if (sameShape(p, netlist_, candidate, previous.netlist_)) {
				old = &candidate;
			}
		}
		if (old != nullptr) {
			if (old->state.width == p.state.width) {
				p.state = old->state;
			}
			p.previousClock = old->previousClock;
			if (p.kind == Kind::Ram && old->memory.size() == p.memory.size()) {
				p.memory = old->memory;
			}
			// Switches and input pins take their value from the circuit, which has the latest one
			if (p.kind == Kind::Clock || p.kind == Kind::Button) {
				p.source = old->source;
			}
			for (size_t k = 0; k < p.out.size(); k++) {
				size_t net = static_cast<size_t>(p.out[k]), was = static_cast<size_t>(old->out[k]);
				values_[net] = previous.values_[was];
				changed_[net] = previous.changed_[was];
			}
		}
		if (p.kind == Kind::Clock) {
			Tick next = now_ + static_cast<Tick>(p.props.period / 2);
			if (old != nullptr) {
				if (auto toggle = toggles.find(match->second); toggle != toggles.end()) {
					next = std::min(next, toggle->second);
				}
			}
			queue_.push(Event{next, seq_++, -index - 1, {}});
		}
	}
	scheduled_ = values_;
	for (size_t i = 0; i < netlist_.prims.size(); i++) {
		evaluatePrim(static_cast<int>(i));
	}
}

void Simulator::setSource(int index, Value value) {
	Prim& p = netlist_.prims[static_cast<size_t>(index)];
	value = Value::make(value.bits, value.known, p.source.width);
	p.source = value;
	schedule(p.out[0], value, now_);
}

void Simulator::schedule(int net, Value value, Tick time) {
	size_t n = static_cast<size_t>(net);
	if (value == scheduled_[n]) {
		return;
	}
	// A newer event at the same tick replaces the pending one (no zero-width glitches)
	if (scheduledTime_[n] == time) {
		cancelled_.insert(scheduledSeq_[n]);
		netEvents_--;
	}
	scheduled_[n] = value;
	scheduledTime_[n] = time;
	scheduledSeq_[n] = seq_;
	queue_.push(Event{time, seq_++, net, value});
	netEvents_++;
}

void Simulator::evaluatePrim(int index) {
	Prim& p = netlist_.prims[static_cast<size_t>(index)];
	inScratch_.clear();
	for (int net : p.in) {
		inScratch_.push_back(values_[static_cast<size_t>(net)]);
	}
	outScratch_.clear();
	for (int net : p.out) {
		outScratch_.push_back(Value::x(netlist_.nets[static_cast<size_t>(net)].width));
	}
	evaluate(p, inScratch_, outScratch_);
	for (size_t i = 0; i < p.out.size(); i++) {
		schedule(p.out[i], outScratch_[i], now_ + static_cast<Tick>(p.delay));
	}
}

void Simulator::process(const Event& event) {
	now_ = event.time;
	if (event.net < 0) {
		// A clock toggles and schedules its next toggle
		int index = -event.net - 1;
		Prim& p = netlist_.prims[static_cast<size_t>(index)];
		p.source = logicNot(p.source);
		schedule(p.out[0], p.source, now_);
		queue_.push(Event{now_ + static_cast<Tick>(p.props.period / 2), seq_++, event.net, {}});
		return;
	}
	size_t n = static_cast<size_t>(event.net);
	if (cancelled_.erase(event.seq) > 0) {
		return; // replaced by a newer event at the same tick (already uncounted)
	}
	if (scheduledSeq_[n] == event.seq) {
		scheduledTime_[n] = NEVER;
	}
	netEvents_--;
	totalEvents_++;
	if (event.value == values_[n]) {
		return;
	}
	values_[n] = event.value;
	changed_[n] = now_;
	for (int reader : netlist_.nets[n].readers) {
		evaluatePrim(reader);
	}
}

bool Simulator::stepEvent() {
	if (queue_.empty()) {
		return false;
	}
	Event event = queue_.top();
	queue_.pop();
	process(event);
	return true;
}

AdvanceResult Simulator::advance(Tick ticks, std::size_t maxEvents) {
	AdvanceResult result;
	Tick end = now_ + ticks;
	while (!queue_.empty() && queue_.top().time < end) {
		if (result.events >= maxEvents) {
			result.budgetExhausted = true;
			return result;
		}
		Event event = queue_.top();
		queue_.pop();
		std::size_t before = totalEvents_;
		process(event);
		result.events += totalEvents_ - before;
	}
	now_ = end;
	return result;
}

SettleResult Simulator::settle(std::size_t maxEvents) {
	std::size_t events = 0;
	while (netEvents_ > 0) {
		if (events >= maxEvents) {
			return SettleResult::Oscillating;
		}
		Event event = queue_.top();
		queue_.pop();
		std::size_t before = totalEvents_;
		process(event);
		events += totalEvents_ - before;
	}
	return SettleResult::Stable;
}

} // namespace logic
