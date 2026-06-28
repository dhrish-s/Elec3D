#include "EditPropertyCommand.h"

#include <iostream>
#include <utility>

EditPropertyCommand::EditPropertyCommand(CircuitGraph& graph, int id,
                                         std::string propertyName,
                                         float oldValue, float newValue)
    : m_graph(graph),
      m_id(id),
      m_propertyName(std::move(propertyName)),
      m_oldValue(oldValue),
      m_newValue(newValue)
{
    // Property name keeps one small command reusable for all float fields.
}

void EditPropertyCommand::execute()
{
    // Execute always moves the model forward to the user's new value.
    setValue(m_newValue);
}

void EditPropertyCommand::undo()
{
    // Undo restores the exact previous value captured at command creation.
    setValue(m_oldValue);
}

bool EditPropertyCommand::setValue(float value)
{
    // Search by ID because component order is allowed to change after edits.
    for (auto& component : m_graph.components) {
        if (component.id != m_id) {
            continue;
        }

        // Keep property names byte-stable with JSON/UI field names where possible.
        if (m_propertyName == "resistance") {
            component.resistance = value;
        } else if (m_propertyName == "capacitance") {
            component.capacitance = value;
        } else if (m_propertyName == "inductance") {
            component.inductance = value;
        } else if (m_propertyName == "voltageSource") {
            component.voltageSource = value;
        } else if (m_propertyName == "voltage") {
            component.voltage = value;
        } else {
            std::cerr << "[Elec3D] EditPropertyCommand failed: unsupported property "
                      << m_propertyName << "\n";
            return false;
        }

        // Returning here proves exactly one component consumed the edit.
        return true;
    }

    // Missing IDs are reported instead of crashing during undo/redo.
    std::cerr << "[Elec3D] EditPropertyCommand failed: component "
              << m_id << " was not found\n";
    return false;
}
