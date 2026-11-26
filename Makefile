# Shortcuts around CMake (the real build). Needs CMake 3.28+, a C++23 compiler and, for the app,
# SFML 3.1 (downloaded automatically when it is not installed). Dear ImGui and nlohmann/json are
# downloaded when needed.
#   make            configure (first time) and build into build/
#   make run        build and start the app
#   make test       build and run the tests
#   make format     clang-format every source file
#   make clean      remove build/
# Options: make BUILD_TYPE=Debug, make BUILD_DIR=build-wsl

BUILD_DIR ?= build
BUILD_TYPE ?= Release
CMAKE ?= cmake
CLANG_FORMAT ?= clang-format

all: $(BUILD_DIR)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR) --parallel

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

run: all
	$(BUILD_DIR)/bin/logicsim

test: all
	ctest --test-dir $(BUILD_DIR) --output-on-failure

format:
	$(CLANG_FORMAT) -i $(wildcard src/*.cpp src/*/*.cpp inc/*/*.hpp tests/*.cpp tests/*.hpp)

clean:
	$(CMAKE) -E rm -rf $(BUILD_DIR)

.PHONY: all run test format clean
