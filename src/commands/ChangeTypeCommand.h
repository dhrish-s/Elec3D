#pragma once

#include <string>

#include "../circuit/Circuit.h"
#include "Command.h"

constexpr float DEFAULT_RESISTANCE = 1.0f;
constexpr float DEFAULT_CAPACITANCE = 0.0f;
constexpr float DEFAULT_INDUCTANCE = 0.0f;
constexpr float DEFAULT_VOLTAGE_SOURCE = 0.0f;
constexpr float BATTERY_VOLTAGE_SOURCE = 5.0f;
constexpr float BATTERY_DEFAULT_RESISTANCE = 0.1f;
constexpr float CAPACITOR_DEFAULT_CAPACITANCE = 0.5f;
constexpr float INDUCTOR_DEFAULT_INDUCTANCE = 0.8f;
constexpr float DIODE_DEFAULT_RESISTANCE = 2.0f;

/// Changes a component type and its bundled electrical defaults.
class ChangeTypeCommand : public Command {
public:
    /// Stores complete old and new type/default snapshots for exact undo/redo.
    ChangeTypeCommand(CircuitGraph& graph, int id,
                      std::string oldType, float oldResistance,
                      float oldCapacitance, float oldInductance,
                      float oldVoltageSource,
                      std::string newType, float newResistance,
                      float newCapacitance, float newInductance,
                      float newVoltageSource);

    /// Applies the new type and default values selected by the UI.
    void execute() override;

    /// Restores the previous type and all previous electrical values.
    void undo() override;

private:
    /// Writes one complete type/default snapshot into the target component.
    bool applySnapshot(const std::string& type, float resistance,
                       float capacitance, float inductance,
                       float voltageSource);

    CircuitGraph& m_graph;
    int m_id;

    std::string m_oldType;
    float m_oldResistance;
    float m_oldCapacitance;
    float m_oldInductance;
    float m_oldVoltageSource;

    std::string m_newType;
    float m_newResistance;
    float m_newCapacitance;
    float m_newInductance;
    float m_newVoltageSource;
};
