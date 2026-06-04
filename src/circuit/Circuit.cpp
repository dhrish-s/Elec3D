#include "Circuit.h"

#include <algorithm>
#include <functional>

std::unordered_map<int, std::vector<int>> CircuitGraph::buildAdjacency() const
{
    std::unordered_map<int, std::vector<int>> adj;
    for (const auto& conn : connections) {
        adj[conn.from_id].push_back(conn.to_id);
        adj[conn.to_id].push_back(conn.from_id);
    }
    return adj;
}

std::vector<int> CircuitGraph::findDisconnectedComponents() const
{
    return FindDisconnectedComponents(components, connections);
}

bool CircuitGraph::isCircuitLooped() const
{
    return IsCircuitLooped(components, connections);
}

std::unordered_set<int> CircuitGraph::findLoopedComponents() const
{
    return FindLoopedComponents(components, connections);
}

std::vector<int> FindDisconnectedComponents(const std::vector<Component>& components,
                                            const std::vector<Connection>& connections)
{
    std::unordered_set<int> visited;
    std::unordered_map<int, std::vector<int>> adj;

    for (const auto& conn : connections) {
        adj[conn.from_id].push_back(conn.to_id);
        adj[conn.to_id].push_back(conn.from_id);
    }

    std::function<void(int)> dfs = [&](int node) {
        visited.insert(node);
        for (int neighbor : adj[node]) {
            if (!visited.count(neighbor)) {
                dfs(neighbor);
            }
        }
    };

    if (!components.empty()) {
        dfs(components[0].id);
    }

    std::vector<int> disconnected;
    for (const auto& c : components) {
        if (!visited.count(c.id)) {
            disconnected.push_back(c.id);
        }
    }

    return disconnected;
}

bool HasCycleDFS(int node, int parent,
                 const std::unordered_map<int, std::vector<int>>& adj,
                 std::unordered_set<int>& visited)
{
    visited.insert(node);
    for (int neighbor : adj.at(node)) {
        if (!visited.count(neighbor)) {
            if (HasCycleDFS(neighbor, node, adj, visited)) {
                return true;
            }
        } else if (neighbor != parent) {
            return true;
        }
    }
    return false;
}

bool IsCircuitLooped(const std::vector<Component>& components,
                     const std::vector<Connection>& connections)
{
    std::unordered_map<int, std::vector<int>> adj;
    for (const auto& conn : connections) {
        adj[conn.from_id].push_back(conn.to_id);
        adj[conn.to_id].push_back(conn.from_id);
    }

    std::unordered_set<int> visited;
    for (const auto& c : components) {
        if (!visited.count(c.id)) {
            if (HasCycleDFS(c.id, -1, adj, visited)) {
                return true;
            }
        }
    }
    return false;
}

std::unordered_set<int> FindLoopedComponents(const std::vector<Component>& components,
                                             const std::vector<Connection>& connections)
{
    std::unordered_map<int, std::vector<int>> adj;
    for (const auto& conn : connections) {
        adj[conn.from_id].push_back(conn.to_id);
        adj[conn.to_id].push_back(conn.from_id);
    }

    std::unordered_set<int> visited, inCycle;

    std::function<bool(int, int, std::vector<int>&)> dfs =
        [&](int node, int parent, std::vector<int>& path) {
            visited.insert(node);
            path.push_back(node);

            for (int neighbor : adj[node]) {
                if (neighbor == parent) {
                    continue;
                }

                if (!visited.count(neighbor)) {
                    if (dfs(neighbor, node, path)) {
                        return true;
                    }
                } else {
                    auto it = std::find(path.begin(), path.end(), neighbor);
                    if (it != path.end()) {
                        for (; it != path.end(); ++it) {
                            inCycle.insert(*it);
                        }
                        inCycle.insert(neighbor);
                    }
                }
            }

            path.pop_back();
            return false;
        };

    for (const auto& c : components) {
        if (!visited.count(c.id)) {
            std::vector<int> path;
            dfs(c.id, -1, path);
        }
    }

    return inCycle;
}
