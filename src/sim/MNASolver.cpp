#include "MNASolver.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <unordered_map>

#include <Eigen/Dense>

namespace {

struct VoltageSourceStamp {
    int sourceNode;
    int plusNode;
    float voltage;
};

/// Inductors are treated as near-short-circuits in DC analysis.
/// A tiny series resistance prevents division-by-zero in the
/// conductance matrix while preserving near-zero DC voltage drop.
static constexpr float INDUCTOR_DC_RESISTANCE = 1e-4f;

bool isBattery(const Component& component)
{
    return component.type == "Battery" && component.voltageSource > 0.0f;
}

int findLowestComponentId(const CircuitGraph& graph)
{
    int lowest = std::numeric_limits<int>::max();
    for (const auto& component : graph.components) {
        if (component.id < lowest) {
            lowest = component.id;
        }
    }
    return lowest;
}

bool hasComponentId(const CircuitGraph& graph, int id)
{
    for (const auto& component : graph.components) {
        if (component.id == id) {
            return true;
        }
    }
    return false;
}

const Component* findComponent(const std::unordered_map<int, const Component*>& componentsById, int id)
{
    auto it = componentsById.find(id);
    return it == componentsById.end() ? nullptr : it->second;
}

int findSourcePlusNode(const CircuitGraph& graph, const Component& source, int groundId)
{
    if (source.id != groundId) {
        return source.id;
    }

    for (const auto& connection : graph.connections) {
        if (connection.to_id == source.id && connection.from_id != groundId) {
            return connection.from_id;
        }
    }

    for (const auto& connection : graph.connections) {
        if (connection.from_id == source.id && connection.to_id != groundId) {
            return connection.to_id;
        }
    }

    return source.id;
}

bool effectiveDcResistance(const Component& component, float& resistance)
{
    if (isBattery(component)) {
        return false;
    }

    resistance = component.resistance;

    // Capacitor is open circuit at DC.
    // Skip it so no DC current path is added to the matrix.
    if (component.type == "Capacitor") {
        return false;
    }

    // Inductor is short circuit at DC.
    // Use tiny resistance to avoid division by zero.
    if (component.type == "Inductor") {
        resistance = INDUCTOR_DC_RESISTANCE;
    }

    if (resistance <= 0.0f) {
        // TODO(future): model other ideal zero-resistance parts as KCL constraints.
        return false;
    }

    return true;
}

void stampConductance(Eigen::MatrixXf& G, int i, int j, float resistance)
{
    const float conductance = 1.0f / resistance;

    // Conductance stamp - each resistor adds its conductance to both diagonal entries
    // and subtracts from the off-diagonal entries. This encodes Kirchhoff's current law.
    G(i, i) += conductance;
    G(j, j) += conductance;
    G(i, j) -= conductance;
    G(j, i) -= conductance;
}

} // namespace

std::vector<float> MNASolver::solve(const CircuitGraph& graph, int groundNodeId)
{
    // STEP 1 - Count nodes.
    // Number of nodes = number of components. Each component.id is a node index.
    const int n = static_cast<int>(graph.components.size());
    if (n == 0) {
        return {};
    }

    std::unordered_map<int, const Component*> componentsById;
    componentsById.reserve(graph.components.size());
    for (const auto& component : graph.components) {
        if (component.id < 0 || component.id >= n) {
            std::cerr << "[Elec3D] MNA: component id out of range\n";
            return {};
        }
        componentsById[component.id] = &component;
    }

    int groundId = groundNodeId;
    if (!hasComponentId(graph, groundId)) {
        groundId = findLowestComponentId(graph);
    }
    std::cerr << "[Elec3D] Ground node: " << groundId << "\n";

    std::vector<VoltageSourceStamp> voltageSources;
    for (const auto& component : graph.components) {
        if (isBattery(component)) {
            voltageSources.push_back({
                component.id,
                findSourcePlusNode(graph, component, groundId),
                component.voltageSource
            });
        }
    }

    const int matrixSize = n + static_cast<int>(voltageSources.size());

    // STEP 2 - Build conductance matrix G (n x n) and RHS vector b (n x 1).
    // Initialize both to zero, then grow the system for source currents.
    Eigen::MatrixXf G = Eigen::MatrixXf::Zero(matrixSize, matrixSize);
    Eigen::VectorXf b = Eigen::VectorXf::Zero(matrixSize);

    for (const auto& connection : graph.connections) {
        const Component* from = findComponent(componentsById, connection.from_id);
        const Component* to = findComponent(componentsById, connection.to_id);
        if (!from || !to) {
            continue;
        }

        float resistance = 0.0f;
        if (effectiveDcResistance(*from, resistance)) {
            stampConductance(G, from->id, to->id, resistance);
        }
        if (effectiveDcResistance(*to, resistance)) {
            stampConductance(G, from->id, to->id, resistance);
        }
    }

    // STEP 3 - Stamp voltage sources.
    // Voltage source stamp - introduces a new unknown (the source current) so the solver
    // can enforce the voltage constraint exactly.
    for (int sourceIndex = 0; sourceIndex < static_cast<int>(voltageSources.size()); ++sourceIndex) {
        const VoltageSourceStamp& source = voltageSources[sourceIndex];
        const int k = n + sourceIndex;
        const int plus = source.plusNode;
        const int minus = groundId;

        G(plus, k) += 1.0f;
        G(minus, k) -= 1.0f;
        G(k, plus) += 1.0f;
        G(k, minus) -= 1.0f;
        b(k) = source.voltage;
    }

    // STEP 4 - Apply ground node.
    // Ground elimination - forces the ground node voltage to zero, giving the solver
    // an absolute reference to work from.
    for (int i = 0; i < matrixSize; ++i) {
        G(groundId, i) = 0.0f;
        G(i, groundId) = 0.0f;
    }
    G(groundId, groundId) = 1.0f;
    b(groundId) = 0.0f;

    // STEP 5 - Solve G * V = b.
    Eigen::FullPivLU<Eigen::MatrixXf> solver(G);
    if (!solver.isInvertible()) {
        std::cerr << "[Elec3D] MNA: matrix singular, circuit may be disconnected\n";
        return {};
    }

    const Eigen::VectorXf solution = solver.solve(b);
    std::vector<float> voltages(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        voltages[static_cast<size_t>(i)] = solution(i);
    }

    return voltages;
}

#ifdef ELEC3D_TEST_MNA

void TestMNASolver()
{
    CircuitGraph graph;
    graph.components.push_back({0, "Battery", 0.0f, 0.0f, 0.0f, 1, 0.1f, 5.0f});
    graph.components.push_back({1, "Resistor", 2.0f, 0.0f, 0.0f, 1, 10.0f, 0.0f});
    graph.components.push_back({2, "Resistor", 4.0f, 0.0f, 0.0f, 1, 10.0f, 0.0f});
    graph.components[0].voltageSource = 5.0f;
    graph.connections.push_back({0, 1});
    graph.connections.push_back({1, 2});
    graph.connections.push_back({2, 0});

    const std::vector<float> voltages = MNASolver::solve(graph, 0);
    assert(voltages.size() == 3);
    assert(std::fabs(voltages[0] - 0.0f) <= 0.001f);
    assert(std::fabs(voltages[1] - 2.5f) <= 0.001f);
    assert(std::fabs(voltages[2] - 5.0f) <= 0.001f);

    const float r1Drop = voltages[1] - voltages[0];
    const float r2Drop = voltages[2] - voltages[1];
    assert(std::fabs((r1Drop + r2Drop) - 5.0f) <= 0.001f);

    const float r1Current = r1Drop / 10.0f;
    const float r2Current = r2Drop / 10.0f;
    assert(std::fabs(r1Current - r2Current) <= 0.001f);
}

#endif
