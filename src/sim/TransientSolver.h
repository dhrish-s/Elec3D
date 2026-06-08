#pragma once

#include <vector>

#include "../circuit/Circuit.h"

class TransientSolver {
public:
    /// Simulate circuit voltage over time using forward-Euler.
    /// dt:          timestep in seconds (e.g. 1e-5)
    /// tTotal:      total simulation time in seconds (e.g. 5e-3)
    /// observeNode: component id whose voltage is recorded.
    /// Returns one voltage sample per timestep.
    /// Returns empty vector if circuit is unsolvable.
    static std::vector<float> solve(
        const CircuitGraph& graph,
        int   groundNodeId,
        float dt,
        float tTotal,
        int   observeNode
    );
};
