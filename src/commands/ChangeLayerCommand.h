#pragma once

#include "../circuit/Circuit.h"
#include "Command.h"

/// Moves one component from an old layer to a new layer.
class ChangeLayerCommand : public Command {
public:
    /// Captures the target component ID and both layer values.
    ChangeLayerCommand(CircuitGraph& graph, int id, int oldLayer, int newLayer);

    /// Applies the new layer selected by the user.
    void execute() override;

    /// Restores the layer value that existed before editing.
    void undo() override;

private:
    /// Writes a layer value into the target component if it still exists.
    bool setLayer(int layer);

    CircuitGraph& m_graph;
    int m_id;
    int m_oldLayer;
    int m_newLayer;
};
