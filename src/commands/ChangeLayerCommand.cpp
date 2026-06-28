#include "ChangeLayerCommand.h"

#include <iostream>

ChangeLayerCommand::ChangeLayerCommand(CircuitGraph& graph, int id,
                                       int oldLayer, int newLayer)
    : m_graph(graph),
      m_id(id),
      m_oldLayer(oldLayer),
      m_newLayer(newLayer)
{
    // Store both layers so undo/redo never depends on current UI state.
}

void ChangeLayerCommand::execute()
{
    // Execute moves the component to the layer chosen in the editor.
    setLayer(m_newLayer);
}

void ChangeLayerCommand::undo()
{
    // Undo moves it back to the layer captured before editing started.
    setLayer(m_oldLayer);
}

bool ChangeLayerCommand::setLayer(int layer)
{
    // Search by component ID because layer edits should survive vector reorder.
    for (auto& component : m_graph.components) {
        if (component.id == m_id) {
            component.layer = layer;
            return true;
        }
    }

    // A missing component means the command history no longer matches the graph.
    std::cerr << "[Elec3D] ChangeLayerCommand failed: component "
              << m_id << " was not found\n";
    return false;
}
