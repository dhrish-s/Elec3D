#pragma once

#include <vector>

#include "../circuit/Circuit.h"
#include "Command.h"

/// Removes a component and its attached connections, then restores both on undo.
class DeleteComponentCommand : public Command {
public:
    /// Captures the component ID that should be deleted from the graph.
    DeleteComponentCommand(CircuitGraph& graph, int componentId);

    /// Removes the target component and snapshots every connection touching it.
    void execute() override;

    /// Restores the deleted component and all saved attached connections.
    void undo() override;

private:
    CircuitGraph& m_graph;
    Component m_component{};
    std::vector<Connection> m_connections;
    int m_componentId;
    bool m_hasSnapshot = false;
};
