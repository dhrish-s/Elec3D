#include "MoveComponentCommand.h"

#include <iostream>

MoveComponentCommand::MoveComponentCommand(CircuitGraph& graph, int id,
                                           glm::vec3 oldPosition, glm::vec3 newPosition)
    : m_graph(graph),
      m_id(id),
      m_oldPosition(oldPosition),
      m_newPosition(newPosition)
{
    // Store both endpoints so redo and undo are exact, not relative.
}

void MoveComponentCommand::execute()
{
    // Execute applies the final position chosen by the user.
    setPosition(m_newPosition);
}

void MoveComponentCommand::undo()
{
    // Undo returns to the captured starting position.
    setPosition(m_oldPosition);
}

bool MoveComponentCommand::setPosition(glm::vec3 position)
{
    // IDs are stable handles; vector indices are not safe after add/delete.
    for (auto& component : m_graph.components) {
        if (component.id == m_id) {
            // Position is stored as separate floats in the current Component schema.
            component.x = position.x;
            component.y = position.y;
            component.z = position.z;
            return true;
        }
    }

    // A missing target is non-fatal but must be visible during debugging.
    std::cerr << "[Elec3D] MoveComponentCommand failed: component "
              << m_id << " was not found\n";
    return false;
}
