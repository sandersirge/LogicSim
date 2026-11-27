// LogicSim: place logic gates on a grid, wire them and watch the signals propagate.
//
// logicsim [circuit.json]                                    opens the app (and the circuit)
// logicsim circuit.json --screenshot out.png [--ticks N]     saves a picture after N ticks and quits
// logicsim --version | --help
#include "core/version.hpp"
#include "gui/app.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>

#include <cstdint>
#include <vector>
#endif

namespace {

// The Windows build is a GUI program without a console of its own: show --help, --version and
// option errors in the console it was started from (output redirected to a file works as it is)
void useParentConsole() {
#if defined(_WIN32)
	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
	if ((output == nullptr || output == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
		(void)std::freopen("CONOUT$", "w", stdout);
		(void)std::freopen("CONOUT$", "w", stderr);
		std::ios::sync_with_stdio();
	}
#endif
}

// The folder of the running program (the examples are next to it), from the operating system
// rather than argv[0], which is a link in an AppImage and may be a bare name on the PATH
std::filesystem::path programDir(const char* argv0) {
	std::error_code error;
#if defined(_WIN32)
	wchar_t buffer[32768];
	DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
	if (length > 0 && length < std::size(buffer)) {
		return std::filesystem::path(std::wstring(buffer, length)).parent_path();
	}
#elif defined(__APPLE__)
	std::uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::vector<char> buffer(size + 1, '\0');
	if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
		std::filesystem::path path = std::filesystem::canonical(buffer.data(), error);
		if (!error) {
			return path.parent_path();
		}
	}
#else
	std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
	if (!error) {
		return self.parent_path();
	}
#endif
	return std::filesystem::absolute(argv0, error).parent_path();
}

constexpr std::string_view USAGE = R"(Usage: logicsim [circuit.json] [options]

  circuit.json            open this circuit
  --version               print the version and exit
  --help                  print this text and exit

Screenshots (the window opens, renders a few frames, saves the picture and closes):
  --screenshot FILE       save a PNG of the window with the circuit to FILE
  --ticks N               run the simulation for N ticks first
  --about                 show the About dialog
)";

} // namespace

int main(int argc, char* argv[]) {
	gui::App::Options options;
	for (int i = 1; i < argc; i++) {
		std::string_view arg = argv[i];
		if (arg == "--version") {
			useParentConsole();
			std::cout << "LogicSim " << version::NUMBER << '\n';
			return 0;
		}
		if (arg == "--help" || arg == "-h") {
			useParentConsole();
			std::cout << USAGE;
			return 0;
		}
		if (arg == "--about") {
			options.about = true;
		} else if (arg == "--screenshot" || arg == "--ticks") {
			if (i + 1 >= argc) {
				useParentConsole();
				std::cerr << "logicsim: " << arg << " needs a value\n\n" << USAGE;
				return 2;
			}
			if (arg == "--screenshot") {
				options.screenshot = argv[++i];
			} else {
				options.ticks = static_cast<logic::Tick>(std::atoll(argv[++i]));
			}
		} else if (arg.starts_with("--")) {
			useParentConsole();
			std::cerr << "logicsim: unknown option: " << arg << "\n\n" << USAGE;
			return 2;
		} else {
			options.file = argv[i];
		}
	}
	gui::App app(programDir(argv[0]));
	return app.run(options);
}
