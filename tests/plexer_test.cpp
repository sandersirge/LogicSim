// Multiplexer, demultiplexer, decoder and priority encoder, including an unknown select.
#include "bench.hpp"

#include <vector>

using namespace test;

namespace {

Props plexer(Kind kind, int selectBits, int width = 1) {
	Props p = defaultProps(kind);
	p.selectBits = selectBits;
	p.width = width;
	return p;
}

void testMux() {
	for (int s = 1; s <= 3; s++) {
		Bench b;
		int n = 1 << s;
		int mux = b.add(Kind::Mux, plexer(Kind::Mux, s, 4));
		std::vector<int> data;
		for (int i = 0; i < n; i++) {
			data.push_back(b.input(4));
			b.wire(data.back(), "out", mux, std::format("d{}", i));
		}
		int sel = b.input(s), o = b.output(4);
		b.wire(sel, "out", mux, "sel");
		b.wire(mux, "out", o, "in");
		b.build();
		for (int i = 0; i < n; i++) {
			b.set(data[static_cast<size_t>(i)], static_cast<std::uint32_t>(i * 3 + 1) & 15);
		}
		for (int i = 0; i < n; i++) {
			b.set(sel, static_cast<std::uint32_t>(i));
			b.settle();
			check(b.get(o) == (static_cast<std::uint32_t>(i * 3 + 1) & 15),
			      std::format("{}-input mux picks {}", n, i));
		}
	}
	// Unknown select: only the bits all inputs agree on stay known
	Bench b;
	int mux = b.add(Kind::Mux, plexer(Kind::Mux, 1, 4));
	int d0 = b.input(4), d1 = b.input(4), o = b.output(4);
	b.wire(d0, "out", mux, "d0");
	b.wire(d1, "out", mux, "d1");
	b.wire(mux, "out", o, "in");
	b.build();
	b.set(d0, 0b1100);
	b.set(d1, 0b1010);
	b.settle();
	check(b.read(o).binary() == "1XX0", "X select: " + b.read(o).binary());
}

void testDemuxDecoder() {
	Bench b;
	int demux = b.add(Kind::Demux, plexer(Kind::Demux, 2, 4));
	int dec = b.add(Kind::Decoder, plexer(Kind::Decoder, 2));
	int in = b.input(4), sel = b.input(2);
	b.wire(in, "out", demux, "in");
	b.wire(sel, "out", demux, "sel");
	b.wire(sel, "out", dec, "sel");
	std::vector<int> outs, lines;
	for (int i = 0; i < 4; i++) {
		outs.push_back(b.output(4));
		b.wire(demux, std::format("o{}", i), outs.back(), "in");
		lines.push_back(b.add(Kind::Led));
		b.wire(dec, std::format("o{}", i), lines.back(), "in");
	}
	b.build();
	b.set(in, 9);
	for (std::uint32_t s = 0; s < 4; s++) {
		b.set(sel, s);
		b.settle();
		for (std::uint32_t i = 0; i < 4; i++) {
			check(b.get(outs[i]) == (i == s ? 9u : 0u), std::format("demux sel {} out {}", s, i));
			check(b.get(lines[i]) == (i == s ? 1u : 0u), std::format("decoder sel {} out {}", s, i));
		}
	}
}

void testPriorityEncoder() {
	Bench b;
	int enc = b.add(Kind::PriorityEncoder, plexer(Kind::PriorityEncoder, 3));
	std::vector<int> ins;
	for (int i = 0; i < 8; i++) {
		ins.push_back(b.input());
		b.wire(ins.back(), "out", enc, std::format("i{}", i));
	}
	int o = b.output(3), v = b.output();
	b.wire(enc, "out", o, "in");
	b.wire(enc, "V", v, "in");
	b.build();
	for (std::uint32_t bits = 0; bits < 256; bits++) {
		for (int i = 0; i < 8; i++) {
			b.set(ins[static_cast<size_t>(i)], bits >> i & 1);
		}
		b.settle();
		std::uint32_t highest = 0;
		for (std::uint32_t i = 0; i < 8; i++) {
			if (bits >> i & 1) {
				highest = i;
			}
		}
		check(b.get(v) == (bits != 0) && (bits == 0 || b.get(o) == highest),
		      std::format("priority encoder {:08b}", bits));
	}
}

} // namespace

int main() {
	testMux();
	testDemuxDecoder();
	testPriorityEncoder();
	return report("plexer_test");
}
