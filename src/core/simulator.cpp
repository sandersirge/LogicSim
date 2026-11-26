#include "core/simulator.hpp"

namespace logic {

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
	bool newest = scheduledSeq_[n] == event.seq;
	bool superseded = !newest && scheduledTime_[n] == event.time;
	if (newest) {
		scheduledTime_[n] = NEVER;
	}
	if (superseded) {
		return; // replaced by a later event at the same tick (already uncounted)
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
