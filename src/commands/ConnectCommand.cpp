#include "ConnectCommand.h"

#include <algorithm>
#include <iostream>

ConnectCommand::ConnectCommand(CircuitGraph& graph, Connection connection)
    : m_graph(graph), m_connection(connection)
{
    // Store the exact directed edge so undo removes the same connection later.
}

void ConnectCommand::execute()
{
    // Connections to missing components would become invisible solver/render bugs.
    if (!componentExists(m_connection.from_id) || !componentExists(m_connection.to_id)) {
        std::cerr << "[Elec3D] ConnectCommand failed: missing endpoint "
                  << m_connection.from_id << " -> " << m_connection.to_id << "\n";
        return;
    }

    // The graph owns connection order, so appending preserves current UI behavior.
    m_graph.connections.push_back(m_connection);
}

void ConnectCommand::undo()
{
    // Remove only one matching edge so duplicate user-created edges undo one step at a time.
    auto it = std::find_if(m_graph.connections.begin(), m_graph.connections.end(),
        [this](const Connection& conn) {
            return conn.from_id == m_connection.from_id && conn.to_id == m_connection.to_id;
        });

    // Logging here catches history/model drift without crashing the UI.
    if (it == m_graph.connections.end()) {
        std::cerr << "[Elec3D] ConnectCommand undo failed: connection "
                  << m_connection.from_id << " -> " << m_connection.to_id
                  << " was not found\n";
        return;
    }

    // Erasing the exact iterator avoids touching unrelated equal-looking commands.
    m_graph.connections.erase(it);
}

bool ConnectCommand::componentExists(int id) const
{
    // IDs, not vector indices, are the stable references used throughout Elec3D.
    return std::any_of(m_graph.components.begin(), m_graph.components.end(),
        [id](const Component& component) { return component.id == id; });
}
