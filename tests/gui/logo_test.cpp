// The logo drawing: the right size, transparent corners, the gate in the middle and lit wires.
#include "check.hpp"
#include "gui/logo.hpp"

#include <format>

using test::check;

namespace {

struct Pixel {
	int r, g, b, a;
};

Pixel at(const std::vector<std::uint8_t>& pixels, unsigned size, unsigned x, unsigned y) {
	const std::uint8_t* p = &pixels[(static_cast<std::size_t>(y) * size + x) * 4];
	return {p[0], p[1], p[2], p[3]};
}

} // namespace

int main() {
	for (unsigned size : {16u, 32u, 64u, 256u}) {
		std::vector<std::uint8_t> logo = gui::drawLogo(size);
		check(logo.size() == static_cast<std::size_t>(size) * size * 4, std::format("{0}x{0} RGBA", size));
		check(at(logo, size, 0, 0).a == 0 && at(logo, size, size - 1, size - 1).a == 0,
		      std::format("{}: the corners are transparent", size));
		Pixel middle = at(logo, size, size / 2, size / 2);
		check(middle.a == 255 && middle.r < 60 && middle.b < 80, std::format("{}: the gate body is dark", size));
		// The output wire, between the gate and the right edge (the brighter of the two middle rows: at
		// small sizes the wire's centre lies between them)
		Pixel above = at(logo, size, size * 80 / 100, size / 2 - 1), below = at(logo, size, size * 80 / 100, size / 2);
		Pixel wire = above.g > below.g ? above : below;
		check(wire.a == 255 && wire.g > 200 && wire.r < 140, std::format("{}: the output wire is lit green", size));
		Pixel background = at(logo, size, size / 2, size * 12 / 100);
		check(background.a == 255 && background.g < 40, std::format("{}: the background is the canvas", size));
	}
	check(gui::drawLogo(0).empty(), "size 0 gives no pixels");
	return test::report("logo_test");
}
