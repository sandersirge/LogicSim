#pragma once

// A tiny test helper: check() counts failed conditions, report() prints a summary and gives the
// exit code. No test framework needed. Run all tests with: ctest --test-dir build
#include <iostream>
#include <string>

namespace test {

inline int failures = 0;
inline int checks = 0;

inline bool check(bool condition, const std::string& description) {
	checks++;
	if (!condition) {
		failures++;
		// Long loops could print thousands of lines; the first ones are enough
		if (failures <= 30) {
			std::cerr << "  FAILED: " << description << '\n';
		}
	}
	return condition;
}

inline int report(const std::string& suite) {
	std::cout << suite << ": " << checks - failures << " / " << checks << " checks passed\n";
	return failures == 0 ? 0 : 1;
}

} // namespace test
