#pragma once

#include <string>

#include "../circuit/Circuit.h"
#include "Command.h"

/// Changes one float property of a component and restores it on undo.
class EditPropertyCommand : public Command {
public:
    /// Stores the target component, property name, and old/new float values.
    EditPropertyCommand(CircuitGraph& graph, int id,
                        std::string propertyName,
                        float oldValue, float newValue);

    /// Sets the property to the new value.
    void execute() override;

    /// Sets the property back to the old value.
    void undo() override;

private:
    /// Writes one supported float property into the target component.
    bool setValue(float value);

    CircuitGraph& m_graph;
    int m_id;
    std::string m_propertyName;
    float m_oldValue;
    float m_newValue;
};
