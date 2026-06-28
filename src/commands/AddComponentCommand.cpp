#include "AddComponentCommand.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <utility>

AddComponentCommand::AddComponentCommand(CircuitGraph& graph, Component component)
    : m_graph(graph), m_component(std::move(component))
{
    // Store a full component snapshot so redo can recreate the same object.
}

void AddComponentCommand::execute()
{
    // Duplicate IDs make selection, rendering, and solver lookup ambiguous.
    const auto sameId = [this](const Component& c) { return c.id == m_component.id; };
    if (std::any_of(m_graph.components.begin(), m_graph.components.end(), sameId)) {
        std::cerr << "[Elec3D] AddComponentCommand failed: component "
                  << m_component.id << " already exists\n";
        return;
    }

    // CommandHistory::push() calls execute(), so this is the only add site.
    m_graph.components.push_back(m_component);
}

void AddComponentCommand::undo()
{
    // Undo removes by stable ID because vector positions can change over time.
    const auto sameId = [this](const Component& c) { return c.id == m_component.id; };
    const size_t oldSize = m_graph.components.size();
    m_graph.components.erase(
        std::remove_if(m_graph.components.begin(), m_graph.components.end(), sameId),
        m_graph.components.end());

    // A missing component means external state changed unexpectedly.
    if (m_graph.components.size() == oldSize) {
        std::cerr << "[Elec3D] AddComponentCommand undo failed: component "
                  << m_component.id << " was not found\n";
    }

    // Removing attached connections prevents invisible dangling edges after undo.
    m_graph.connections.erase(
        std::remove_if(m_graph.connections.begin(), m_graph.connections.end(),
            [this](const Connection& conn) {
                return conn.from_id == m_component.id || conn.to_id == m_component.id;
            }),
        m_graph.connections.end());
}
