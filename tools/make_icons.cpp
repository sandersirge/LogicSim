// Writes the icon files in res/ from the logo drawn in code (gui/logo.cpp):
//   logicsim.png      512 x 512, for the README, the Linux .desktop entry and the AppImage
//   logicsim.ico      16-256 px, the Windows program icon (compiled in through logicsim.rc)
//   logicsim.icns     16-1024 px, the macOS app bundle icon
// Run after changing the drawing: make icons (or build the logicsim_icons target and run it with res/)
#include "gui/logo.hpp"

#include <SFML/Graphics/Image.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes png(unsigned size) {
	std::vector<std::uint8_t> pixels = gui::drawLogo(size);
	sf::Image image(sf::Vector2u{size, size}, pixels.data());
	std::optional<std::vector<std::uint8_t>> encoded = image.saveToMemory("png");
	if (!encoded) {
		std::cerr << "logicsim_icons: PNG encoding failed\n";
		std::exit(1);
	}
	return *encoded;
}

void put16le(Bytes& out, unsigned v) {
	out.push_back(static_cast<std::uint8_t>(v & 0xFF));
	out.push_back(static_cast<std::uint8_t>(v >> 8 & 0xFF));
}

void put32le(Bytes& out, std::uint32_t v) {
	put16le(out, v & 0xFFFF);
	put16le(out, v >> 16);
}

void put32be(Bytes& out, std::uint32_t v) {
	for (int shift = 24; shift >= 0; shift -= 8) {
		out.push_back(static_cast<std::uint8_t>(v >> shift & 0xFF));
	}
}

// An .ico with a PNG image for each size (Windows Vista and later)
Bytes ico(const std::vector<unsigned>& sizes) {
	std::vector<Bytes> images;
	for (unsigned size : sizes) {
		images.push_back(png(size));
	}
	Bytes out;
	put16le(out, 0); // reserved
	put16le(out, 1); // type: icon
	put16le(out, static_cast<unsigned>(sizes.size()));
	std::uint32_t offset = 6 + 16 * static_cast<std::uint32_t>(sizes.size());
	for (size_t i = 0; i < sizes.size(); i++) {
		auto side = static_cast<std::uint8_t>(sizes[i] >= 256 ? 0 : sizes[i]); // 0 means 256
		out.push_back(side);
		out.push_back(side);
		out.push_back(0); // no palette
		out.push_back(0);
		put16le(out, 1);  // planes
		put16le(out, 32); // bits per pixel
		put32le(out, static_cast<std::uint32_t>(images[i].size()));
		put32le(out, offset);
		offset += static_cast<std::uint32_t>(images[i].size());
	}
	for (const Bytes& image : images) {
		out.insert(out.end(), image.begin(), image.end());
	}
	return out;
}

// An .icns with PNG images (macOS 10.7 and later)
Bytes icns() {
	const std::pair<const char*, unsigned> types[] = {{"icp4", 16},  {"icp5", 32},  {"icp6", 64},
	                                                  {"ic07", 128}, {"ic08", 256}, {"ic09", 512},
	                                                  {"ic10", 1024}};
	Bytes body;
	for (const auto& [type, size] : types) {
		Bytes image = png(size);
		body.insert(body.end(), type, type + 4);
		put32be(body, static_cast<std::uint32_t>(image.size() + 8));
		body.insert(body.end(), image.begin(), image.end());
	}
	Bytes out{'i', 'c', 'n', 's'};
	put32be(out, static_cast<std::uint32_t>(body.size() + 8));
	out.insert(out.end(), body.begin(), body.end());
	return out;
}

void write(const std::filesystem::path& path, const Bytes& bytes) {
	std::ofstream file(path, std::ios::binary);
	file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!file) {
		std::cerr << "logicsim_icons: could not write " << path.string() << '\n';
		std::exit(1);
	}
	std::cout << path.string() << " (" << bytes.size() << " bytes)\n";
}

} // namespace

int main(int argc, char* argv[]) {
	if (argc != 2) {
		std::cerr << "Usage: logicsim_icons <res folder>\n";
		return 2;
	}
	std::filesystem::path dir = argv[1];
	std::filesystem::create_directories(dir);
	write(dir / "logicsim.png", png(512));
	write(dir / "logicsim.ico", ico({16, 24, 32, 48, 64, 128, 256}));
	write(dir / "logicsim.icns", icns());
	return 0;
}
