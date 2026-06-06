#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// Stores the editable data for one circuit component.
struct Component {
    int id;
    std::string type;
    float x,y,z;
    int layer;

    float resistance = 1.0f;
    float voltage = 0.0f;
    float capacitance = 0.0f;
    float inductance = 0.0f;
    float voltageSource = 0.0f;
};

/// Stores a directed connection between two component IDs.
struct Connection {
    int from_id;
    int to_id;
};

/// Owns circuit data and exposes adjacency-based graph queries.
class CircuitGraph {
public:
    std::vector<Component> components;
    std::vector<Connection> connections;

    /// Returns component IDs that cannot be reached from the first component.
    std::vector<int> findDisconnectedComponents() const;

    /// Returns true when the undirected connection graph contains any cycle.
    bool isCircuitLooped() const;

    /// Returns every component ID that participates in at least one cycle.
    std::unordered_set<int> findLoopedComponents() const;

private:
    /// Builds an undirected adjacency list from the current connections.
    std::unordered_map<int, std::vector<int>> buildAdjacency() const;
};

/// Returns component IDs that cannot be reached from the first component.
std::vector<int> FindDisconnectedComponents(const std::vector<Component>& components,
                                            const std::vector<Connection>& connections);

/// Returns true when a DFS finds a non-parent edge in the undirected graph.
bool HasCycleDFS(int node, int parent,
                 const std::unordered_map<int, std::vector<int>>& adj,
                 std::unordered_set<int>& visited);

/// Returns true when the undirected circuit graph contains any cycle.
bool IsCircuitLooped(const std::vector<Component>& components,
                     const std::vector<Connection>& connections);

/// Returns every component ID that participates in at least one cycle.
std::unordered_set<int> FindLoopedComponents(const std::vector<Component>& components,
                                             const std::vector<Connection>& connections);
