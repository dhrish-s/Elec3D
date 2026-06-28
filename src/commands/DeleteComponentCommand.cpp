#include "DeleteComponentCommand.h"

#include <algorithm>
#include <iostream>

DeleteComponentCommand::DeleteComponentCommand(CircuitGraph& graph, int componentId)
    : m_graph(graph), m_componentId(componentId)
{
    // The snapshot is captured during execute() so it reflects deletion time.
}

void DeleteComponentCommand::execute()
{
    // Find by component ID because UI IDs remain stable even if vector order changes.
    const auto target = [this](const Component& c) { return c.id == m_componentId; };
    auto it = std::find_if(m_graph.components.begin(), m_graph.components.end(), target);
    if (it == m_graph.components.end()) {
        std::cerr << "[Elec3D] DeleteComponentCommand failed: component "
                  << m_componentId << " was not found\n";
        return;
    }

    // Keep the full component so undo restores all fields, not just position/type.
    m_component = *it;
    m_connections.clear();

    // Attached connections must come back on undo to restore graph topology exactly.
    for (const auto& conn : m_graph.connections) {
        if (conn.from_id == m_componentId || conn.to_id == m_componentId) {
            m_connections.push_back(conn);
        }
    }

    // Remove the component first; remaining connection cleanup prevents dangling IDs.
    m_graph.components.erase(it);
    m_graph.connections.erase(
        std::remove_if(m_graph.connections.begin(), m_graph.connections.end(),
            [this](const Connection& conn) {
                return conn.from_id == m_componentId || conn.to_id == m_componentId;
            }),
        m_graph.connections.end());

    // Undo is only valid after execute() successfully captured state.
    m_hasSnapshot = true;
}

void DeleteComponentCommand::undo()
{
    // Without a snapshot there is no safe state to restore.
    if (!m_hasSnapshot) {
        std::cerr << "[Elec3D] DeleteComponentCommand undo failed: no component snapshot\n";
        return;
    }

    // Avoid creating duplicate IDs if external code already restored the component.
    const auto sameId = [this](const Component& c) { return c.id == m_component.id; };
    if (std::any_of(m_graph.components.begin(), m_graph.components.end(), sameId)) {
        std::cerr << "[Elec3D] DeleteComponentCommand undo skipped: component "
                  << m_component.id << " already exists\n";
        return;
    }

    // Restore the component before connections so every endpoint exists again.
    m_graph.components.push_back(m_component);
    for (const auto& conn : m_connections) {
        m_graph.connections.push_back(conn);
    }
}
