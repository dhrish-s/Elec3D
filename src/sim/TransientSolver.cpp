#include "TransientSolver.h"

#include "MNASolver.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <unordered_map>

std::vector<float> TransientSolver::solve(
    const CircuitGraph& graph,
    int groundNodeId,
    float dt,
    float tTotal,
    int observeNode)
{
    if (graph.components.empty() || dt <= 0.0f || tTotal <= 0.0f) {
        return {};
    }

    // STEP 1 - Setup.
    int steps = static_cast<int>(tTotal / dt);
    std::vector<float> history;
    history.reserve(static_cast<size_t>(steps));

    std::unordered_map<int, float> capVoltage;
    std::unordered_map<int, float> indCurrent;
    for (const auto& component : graph.components) {
        capVoltage[component.id] = 0.0f;
        indCurrent[component.id] = 0.0f;
    }

    const Component* observedComponent = nullptr;
    for (const auto& component : graph.components) {
        if (component.id == observeNode) {
            observedComponent = &component;
            break;
        }
    }

    // STEP 2 - Time loop.
    for (int step = 0; step < steps; ++step) {
        std::ostringstream solverLogSink;
        std::streambuf* originalCerr = std::cerr.rdbuf(solverLogSink.rdbuf());
        std::vector<float> nodeVoltages = MNASolver::solve(graph, groundNodeId);
        std::cerr.rdbuf(originalCerr);
        if (nodeVoltages.empty()) {
            history.push_back(0.0f);
            continue;
        }

        for (const auto& cap : graph.components) {
            if (cap.capacitance <= 0.0f) {
                continue;
            }

            float V = 0.0f;
            if (cap.id >= 0 && cap.id < static_cast<int>(nodeVoltages.size())) {
                V = nodeVoltages[static_cast<size_t>(cap.id)];
            }
            float R = cap.resistance > 0.0f ? cap.resistance : 1e-3f;
            float I = (V - capVoltage[cap.id]) / R;
            // Forward-Euler: V[t+dt] = V[t] + (dV/dt)*dt
            // Approximate - use dt < RC/10 for accurate results.
            capVoltage[cap.id] += (I / cap.capacitance) * dt;
        }

        for (const auto& ind : graph.components) {
            if (ind.inductance <= 0.0f) {
                continue;
            }

            float V = 0.0f;
            if (ind.id >= 0 && ind.id < static_cast<int>(nodeVoltages.size())) {
                V = nodeVoltages[static_cast<size_t>(ind.id)];
            }
            // Forward-Euler: V[t+dt] = V[t] + (dV/dt)*dt
            // Approximate - use dt < RC/10 for accurate results.
            indCurrent[ind.id] += (V / ind.inductance) * dt;
        }

        float observedVoltage = 0.0f;
        if (observedComponent && observedComponent->capacitance > 0.0f) {
            observedVoltage = capVoltage[observeNode];
        } else if (observeNode >= 0 && observeNode < static_cast<int>(nodeVoltages.size())) {
            observedVoltage = nodeVoltages[static_cast<size_t>(observeNode)];
        }
        history.push_back(observedVoltage);
    }

    return history;
}

#ifdef ELEC3D_TEST_TRANSIENT

void TestTransientSolver()
{
    CircuitGraph graph;
    graph.components.push_back({0, "Battery", 0.0f, 0.0f, 0.0f, 1, 0.1f, 5.0f});
    graph.components.push_back({1, "Resistor", 2.0f, 0.0f, 0.0f, 1, 1000.0f, 0.0f});
    graph.components.push_back({2, "Capacitor", 4.0f, 0.0f, 0.0f, 1, 1000.0f, 0.0f});
    graph.components[0].voltageSource = 5.0f;
    graph.components[2].capacitance = 1e-6f;
    graph.connections.push_back({0, 1});
    graph.connections.push_back({1, 2});
    graph.connections.push_back({2, 0});

    const float dt = 1e-5f;
    const float tTotal = 5e-3f;
    const std::vector<float> history = TransientSolver::solve(graph, 0, dt, tTotal, 2);
    const int indexAtRc = 100;
    assert(static_cast<int>(history.size()) > indexAtRc);

    const float expected = 5.0f * (1.0f - 1.0f / static_cast<float>(std::exp(1.0f)));
    const float actual = history[static_cast<size_t>(indexAtRc)];
    const float tolerance = expected * 0.05f;
    std::cerr << "[Elec3D] Transient RC @ tau: " << actual << " V\n";
    assert(std::fabs(actual - expected) <= tolerance);
}

#endif
