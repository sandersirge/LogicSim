# Shortcuts around CMake (the real build). Needs CMake 3.28+, a C++23 compiler and, for the app,
# SFML 3.1 (downloaded automatically when it is not installed). Dear ImGui and nlohmann/json are
# downloaded when needed. More in docs/building.md.
#   make               configure (first time) and build into build/
#   make run           build and start the app
#   make test          build and run the tests
#   make format        clang-format every source file (use the CI's version: pip install clang-format==21.1.8)
#   make format-check  report formatting differences without changing files
#   make sanitize      build and run the engine's tests with AddressSanitizer + UBSan in build-sanitize/
#   make icons         redraw res/logicsim.png, .ico and .icns from the logo drawn in code
#   make clean         remove build/
# Options: make BUILD_TYPE=Debug, make BUILD_DIR=build-wsl

BUILD_DIR ?= build
BUILD_TYPE ?= Release
CMAKE ?= cmake
CLANG_FORMAT ?= clang-format
SOURCES = $(wildcard src/*.cpp src/*/*.cpp inc/*/*.hpp tests/*.cpp tests/*.hpp tests/*/*.cpp tools/*.cpp)

all: $(BUILD_DIR)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR) --parallel

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

run: all
	$(BUILD_DIR)/bin/logicsim

test: all
	ctest --test-dir $(BUILD_DIR) --output-on-failure

format:
	$(CLANG_FORMAT) -i $(SOURCES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(SOURCES)

sanitize:
	$(CMAKE) -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DLOGICSIM_BUILD_GUI=OFF -DLOGICSIM_SANITIZE=ON
	$(CMAKE) --build build-sanitize --parallel
	ctest --test-dir build-sanitize --output-on-failure

icons: $(BUILD_DIR)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR) --target logicsim_icons
	$(BUILD_DIR)/bin/logicsim_icons res

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)

.PHONY: all run test format format-check sanitize icons clean
