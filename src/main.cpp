// LogicSim: place logic gates on a grid, wire them and watch the signals propagate.
//
// logicsim [circuit.json]                                    opens the app (and the circuit)
// logicsim circuit.json --screenshot out.png [--ticks N]     saves a picture after N ticks and quits
#include "gui/app.hpp"

#include <cstdlib>
#include <filesystem>
#include <string_view>

int main(int argc, char* argv[]) {
	gui::App::Options options;
	for (int i = 1; i < argc; i++) {
		std::string_view arg = argv[i];
		if (arg == "--screenshot" && i + 1 < argc) {
			options.screenshot = argv[++i];
		} else if (arg == "--ticks" && i + 1 < argc) {
			options.ticks = static_cast<logic::Tick>(std::atoll(argv[++i]));
		} else {
			options.file = argv[i];
		}
	}
	std::error_code error;
	std::filesystem::path program = std::filesystem::absolute(argv[0], error).parent_path();
	gui::App app(program);
	return app.run(options);
}
