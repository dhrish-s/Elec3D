#pragma once

#include "../circuit/Circuit.h"
#include "Command.h"

/// Adds a new component to the circuit and removes it on undo.
class AddComponentCommand : public Command {
public:
    /// Stores the graph reference and component snapshot used by execute() and undo().
    AddComponentCommand(CircuitGraph& graph, Component component);

    /// Adds the saved component when no component with the same ID already exists.
    void execute() override;

    /// Removes the saved component ID and any dangling connections that touch it.
    void undo() override;

private:
    CircuitGraph& m_graph;
    Component m_component;
};
