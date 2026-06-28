#pragma once

#include <glm/glm.hpp>

#include "../circuit/Circuit.h"
#include "Command.h"

/// Moves a component from an old position to a new position.
class MoveComponentCommand : public Command {
public:
    /// Stores the target component ID and both endpoint positions.
    MoveComponentCommand(CircuitGraph& graph, int id,
                         glm::vec3 oldPosition, glm::vec3 newPosition);

    /// Sets the component position to the new position.
    void execute() override;

    /// Sets the component position back to the old position.
    void undo() override;

private:
    /// Writes one position into the target component if the ID still exists.
    bool setPosition(glm::vec3 position);

    CircuitGraph& m_graph;
    int m_id;
    glm::vec3 m_oldPosition;
    glm::vec3 m_newPosition;
};
