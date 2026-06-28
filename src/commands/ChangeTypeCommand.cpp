#include "ChangeTypeCommand.h"

#include <iostream>
#include <utility>

ChangeTypeCommand::ChangeTypeCommand(CircuitGraph& graph, int id,
                                     std::string oldType, float oldResistance,
                                     float oldCapacitance, float oldInductance,
                                     float oldVoltageSource,
                                     std::string newType, float newResistance,
                                     float newCapacitance, float newInductance,
                                     float newVoltageSource)
    : m_graph(graph),
      m_id(id),
      m_oldType(std::move(oldType)),
      m_oldResistance(oldResistance),
      m_oldCapacitance(oldCapacitance),
      m_oldInductance(oldInductance),
      m_oldVoltageSource(oldVoltageSource),
      m_newType(std::move(newType)),
      m_newResistance(newResistance),
      m_newCapacitance(newCapacitance),
      m_newInductance(newInductance),
      m_newVoltageSource(newVoltageSource)
{
    // Both snapshots are stored up front so undo never guesses default values.
}

void ChangeTypeCommand::execute()
{
    // Execute applies the new type selected in the ImGui combo.
    applySnapshot(m_newType, m_newResistance, m_newCapacitance,
                  m_newInductance, m_newVoltageSource);
}

void ChangeTypeCommand::undo()
{
    // Undo restores the exact values that existed before the combo changed.
    applySnapshot(m_oldType, m_oldResistance, m_oldCapacitance,
                  m_oldInductance, m_oldVoltageSource);
}

bool ChangeTypeCommand::applySnapshot(const std::string& type, float resistance,
                                      float capacitance, float inductance,
                                      float voltageSource)
{
    // IDs are stable across UI edits; vector indices are not.
    for (auto& component : m_graph.components) {
        if (component.id != m_id) {
            continue;
        }

        // Type and defaults must change together to keep the editor coherent.
        component.type = type;
        component.resistance = resistance;
        component.capacitance = capacitance;
        component.inductance = inductance;
        component.voltageSource = voltageSource;
        return true;
    }

    // Missing IDs are non-fatal, but logging exposes broken history state.
    std::cerr << "[Elec3D] ChangeTypeCommand failed: component "
              << m_id << " was not found\n";
    return false;
}
