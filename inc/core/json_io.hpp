#pragma once

// Circuit files (.json): the main circuit and the chips it uses, so a file is self-contained.
// Wires name their pins ("in0", "Q"), so a file stays readable and survives layout changes.
//
// {
//   "format": "logicsim", "version": 1,
//   "circuit": {"name": "...", "components": [...], "wires": [...]},
//   "chips": {"Half adder": {"color": "#...", "description": "...", "circuit": {...}}}
// }
// component: {"id": 3, "type": "and", "x": 4, "y": 2, "rotation": 0, "label": "", "props": {...}}
// wire:      {"from": [3, "out"], "to": [5, "in0"], "bends": [[8, 3], [8, 6]]}
#include "core/circuit.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace logic {

struct LoadResult {
	bool ok = false;
	std::string error;                 // why loading failed (ok == false)
	std::vector<std::string> warnings; // parts that were skipped
	Document document;
};

std::string saveDocument(const Document& document);
LoadResult loadDocument(std::string_view text);

// A chip library on its own (File > Export chips): the same format without a main circuit
std::string saveLibrary(const Library& library);

bool saveFile(const std::filesystem::path& path, const std::string& text, std::string& error);
LoadResult loadFile(const std::filesystem::path& path);

} // namespace logic
