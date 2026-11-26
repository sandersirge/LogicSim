#pragma once

// Event-driven simulation of a netlist. Time is counted in ticks; a primitive whose input changes
// is evaluated at once and its changed outputs take effect after its delay. Feedback loops work:
// a latch holds its value, a ring oscillator keeps oscillating, and a loop that never settles is
// reported instead of hanging (each call has an event budget).
#include "core/netlist.hpp"

#include <cstdint>
#include <limits>
#include <queue>
#include <vector>

namespace logic {

using Tick = std::uint64_t;

enum class SettleResult { Stable, Oscillating };

struct AdvanceResult {
	std::size_t events = 0; // net events processed
	bool budgetExhausted = false;
};

class Simulator {
public:
	Simulator() = default;
	explicit Simulator(Netlist netlist);

	// Power-on: every net X, stored values 0, sources drive their values at tick 0
	void reset();

	// Sets a switch, button or input pin (takes effect at now())
	void setSource(int prim, Value value);
	Value source(int prim) const { return netlist_.prims[static_cast<size_t>(prim)].source; }

	// Processes the next pending event only (time moves to it); false when nothing is pending
	bool stepEvent();
	// Processes everything before now() + ticks, then now() moves there. With a budget it stops
	// early (time stays at the last event) when more events would be needed.
	AdvanceResult advance(Tick ticks, std::size_t maxEvents = std::numeric_limits<std::size_t>::max());
	// Runs until no change is pending apart from clock ticks. Oscillating when maxEvents were not enough.
	SettleResult settle(std::size_t maxEvents = 1'000'000);

	// The first tick that has not been processed yet
	Tick now() const { return now_; }
	Value value(int net) const { return values_[static_cast<size_t>(net)]; }
	// The tick the net last changed at (for highlighting recent activity)
	Tick lastChange(int net) const { return changed_[static_cast<size_t>(net)]; }
	bool everChanged(int net) const { return changed_[static_cast<size_t>(net)] != NEVER; }
	std::size_t pendingEvents() const { return netEvents_; }
	std::size_t totalEvents() const { return totalEvents_; }

	const Netlist& netlist() const { return netlist_; }
	const Prim& prim(int index) const { return netlist_.prims[static_cast<size_t>(index)]; }

	static constexpr Tick NEVER = std::numeric_limits<Tick>::max();

private:
	struct Event {
		Tick time;
		std::uint64_t seq; // same-tick events run in the order they were scheduled
		int net;           // >= 0: the net takes value; < 0: clock primitive -net-1 toggles
		Value value;
		bool operator>(const Event& other) const {
			return time != other.time ? time > other.time : seq > other.seq;
		}
	};

	void schedule(int net, Value value, Tick time);
	void process(const Event& event);
	void evaluatePrim(int index);

	Netlist netlist_;
	std::vector<Value> values_;
	std::vector<Value> scheduled_;            // the value each net will have after its pending events
	std::vector<Tick> scheduledTime_;         // time of the newest pending event of each net
	std::vector<std::uint64_t> scheduledSeq_; // its seq (an older event at the same time is dropped)
	std::vector<Tick> changed_;
	std::priority_queue<Event, std::vector<Event>, std::greater<>> queue_;
	std::uint64_t seq_ = 0;
	Tick now_ = 0;
	std::size_t netEvents_ = 0; // pending events that are not clock toggles
	std::size_t totalEvents_ = 0;
	std::vector<Value> inScratch_, outScratch_;
};

} // namespace logic
