#pragma once

#include "../circuit/Circuit.h"
#include "Command.h"

/// Creates a connection between two components and removes it on undo.
class ConnectCommand : public Command {
public:
    /// Stores the exact connection that execute() will add.
    ConnectCommand(CircuitGraph& graph, Connection connection);

    /// Adds the connection if both endpoint component IDs exist.
    void execute() override;

    /// Removes one matching connection from the graph.
    void undo() override;

private:
    /// Reports whether a component ID exists before a connection references it.
    bool componentExists(int id) const;

    CircuitGraph& m_graph;
    Connection m_connection;
};
