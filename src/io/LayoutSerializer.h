#pragma once

#include <string>

#include "../circuit/Circuit.h"

/// Loads and saves circuit layout JSON files.
class LayoutSerializer {
public:
    /// Loads a CircuitGraph from a JSON layout file.
    static CircuitGraph load(const std::string& path);

    /// Saves a CircuitGraph to a JSON layout file.
    static void save(const CircuitGraph& graph, const std::string& path);
};
