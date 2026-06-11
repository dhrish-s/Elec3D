#pragma once

#include <glm/glm.hpp>

#include <map>
#include <string>
#include <vector>

#include "../circuit/Circuit.h"
#include "Camera.h"
#include "MeshBuilder.h"
#include "WireRenderer.h"

/// Stores one animated signal trail point for connection rendering.
struct PulseTrail {
    glm::vec3 pos;
    float age;
};

/// Draws the existing cube, axis, grid, connection, and pulse OpenGL scene.
class Renderer {
public:
    /// Initializes shader programs, buffers, vertex arrays, and fixed OpenGL state.
    bool init();

    /// Draws the circuit using the camera view and projection for the current aspect ratio.
    void draw(const CircuitGraph& graph, const Camera& camera,
              float aspectRatio, float elapsedTime);

private:
    unsigned int shaderProgram = 0;
    unsigned int cubeVAO = 0;
    unsigned int cubeVBO = 0;
    unsigned int cubeEBO = 0;
    unsigned int lineVAO = 0;
    unsigned int lineVBO = 0;
    unsigned int pulseVAO = 0;
    unsigned int pulseVBO = 0;
    unsigned int axisVAO = 0;
    unsigned int axisVBO = 0;
    unsigned int gridVAO = 0;
    unsigned int gridVBO = 0;

    static constexpr bool USE_COMPONENT_MESHES = true;
    std::map<std::string, Mesh> m_meshRegistry;
    static constexpr bool USE_BEZIER_WIRES = true;
    WireRenderer m_wireRenderer;

    int modelLoc = -1;
    int viewLoc = -1;
    int projLoc = -1;
    int colorLoc = -1;
    int brightnessLoc = -1;
};
