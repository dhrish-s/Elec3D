#pragma once

#include <vector>

#include "../circuit/Circuit.h"

class MNASolver {
public:
    /// Solve the DC operating point of the circuit.
    /// Returns one voltage per component node.
    /// Index matches component.id.
    /// Returns empty vector if circuit is unsolvable.
    static std::vector<float> solve(
        const CircuitGraph& graph,
        int groundNodeId
    );
};
