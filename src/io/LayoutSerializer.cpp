#include "LayoutSerializer.h"

#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

/// Applies type defaults only when the JSON omitted that electrical field.
static void applyComponentDefaults(Component& component, const json& item)
{
    if (component.type == "Resistor") {
        if (!item.contains("resistance")) component.resistance = 1.0f;
    } else if (component.type == "Capacitor") {
        if (!item.contains("capacitance")) component.capacitance = 0.5f;
        if (!item.contains("resistance")) component.resistance = 0.0f;
    } else if (component.type == "Inductor") {
        if (!item.contains("inductance")) component.inductance = 0.8f;
        if (!item.contains("resistance")) component.resistance = 0.0f;
    } else if (component.type == "Diode") {
        if (!item.contains("resistance")) component.resistance = 2.0f;
    } else if (component.type == "Battery") {
        if (!item.contains("resistance")) component.resistance = 0.1f;
        if (!item.contains("voltageSource")) component.voltageSource = 5.0f;
        if (!item.contains("voltage")) component.voltage = 5.0f;
    }
}

CircuitGraph LayoutSerializer::load(const std::string& path)
{
    CircuitGraph graph;
    const std::string loadPath = path == "src/layout.json" ? "../src/layout.json" : path;
    std::ifstream file(loadPath);
    if (!file) {
        std::cerr << "[Elec3D] Failed to open: " << loadPath << " - file not found\n";
        return graph;
    }

    json layout;
    file >> layout;

    for (const auto& item : layout.value("components", json::array())) {
        Component c;
        c.id = item.value("id", 0);
        c.type = item.value("type", std::string{});
        c.resistance = item.value("resistance", 1.0f);
        c.voltage = item.value("voltage", 0.0f);
        c.capacitance = item.value("capacitance", 0.0f);
        c.inductance = item.value("inductance", 0.0f);
        c.voltageSource = item.value("voltageSource", 0.0f);

        const json position = item.value("position", json::array({0.0f, 0.0f, 0.0f}));
        c.x = position.size() > 0 ? position[0].get<float>() : 0.0f;
        c.y = position.size() > 1 ? position[1].get<float>() : 0.0f;
        c.z = position.size() > 2 ? position[2].get<float>() : 0.0f;
        c.layer = item.value("layer", 0);

        applyComponentDefaults(c, item);

        graph.components.push_back(c);
    }

    for (const auto& pair : layout.value("connections", json::array())) {
        Connection conn;
        conn.from_id = pair.value("from", 0);
        conn.to_id = pair.value("to", 0);
        graph.connections.push_back(conn);
    }

    return graph;
}

void LayoutSerializer::save(const CircuitGraph& graph, const std::string& path)
{
    json output;

    for (const auto& c : graph.components) {
        json compJson;
        compJson["id"] = c.id;
        compJson["type"] = c.type;
        compJson["layer"] = c.layer;
        compJson["position"] = {c.x, c.y, c.z};
        compJson["resistance"] = c.resistance;
        compJson["capacitance"] = c.capacitance;
        compJson["inductance"] = c.inductance;
        compJson["voltageSource"] = c.voltageSource;
        output["components"].push_back(compJson);
    }

    for (const auto& conn : graph.connections) {
        json connJson;
        connJson["from"] = conn.from_id;
        connJson["to"] = conn.to_id;
        output["connections"].push_back(connJson);
    }

    std::ofstream outFile(path);
    if (!outFile) {
        std::cerr << "[Elec3D] Failed to open: " << path << " - file not found\n";
        return;
    }

    outFile << output.dump(4);
}
