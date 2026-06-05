#include "LayoutSerializer.h"

#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

/// Applies the same type defaults that main.cpp used before JSON loading moved here.
static void applyComponentDefaults(Component& component)
{
    if (component.type == "Resistor") {
        component.resistance = 1.0f;
    } else if (component.type == "Capacitor") {
        component.resistance = 0.5f;
    } else if (component.type == "Inductor") {
        component.resistance = 0.8f;
    } else if (component.type == "Diode") {
        component.resistance = 2.0f;
    } else if (component.type == "Battery") {
        component.resistance = 0.1f;
        component.voltage = 5.0f;
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

        applyComponentDefaults(c);

        const json position = item.value("position", json::array({0.0f, 0.0f, 0.0f}));
        c.x = position.size() > 0 ? position[0].get<float>() : 0.0f;
        c.y = position.size() > 1 ? position[1].get<float>() : 0.0f;
        c.z = position.size() > 2 ? position[2].get<float>() : 0.0f;
        c.layer = item.value("layer", 0);

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
