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

constexpr int INSTANCE_ATTRIB_LOCATION_BASE = 2;   // mat4 -> locations 2,3,4,5
constexpr int INSTANCE_COLOR_ATTRIB_LOCATION = 6;
constexpr int INITIAL_INSTANCE_CAPACITY = 64;      // starting reserve per mesh type, doubled on growth

/// Per-instance data uploaded to the GPU for batched component rendering.
/// One entry per component sharing a given mesh type.
struct InstanceData {
    glm::mat4 modelMatrix;
    glm::vec3 color;
    float pad;   // rounds struct to 80 bytes (16-byte aligned)
};
// A silent layout mismatch here corrupts every instanced draw without any
// compiler error, so the byte size is checked explicitly.
static_assert(sizeof(InstanceData) == 80,
    "InstanceData layout changed - update glVertexAttribPointer offsets");

/// GPU-side state for one mesh type's instance buffer.
struct InstanceBuffer {
    GLuint vbo = 0;
    int capacity = 0;   // current allocated slot count
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
    /// Grow a mesh type's instance buffer if needed. Never shrinks. Never
    /// reallocates when the existing capacity already fits the request.
    void ensureInstanceCapacity(InstanceBuffer& buf, int neededCount);

    /// Draws one FR4-style PCB substrate under each visible circuit layer.
    void drawPcbSubstrates(const CircuitGraph& graph,
                           const glm::mat4& view,
                           const glm::mat4& projection);


    unsigned int shaderProgram = 0;
    unsigned int m_shaderLit = 0;
    unsigned int m_shaderPcb = 0;
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
    unsigned int pcbVAO = 0;
    unsigned int pcbVBO = 0;
    unsigned int pcbEBO = 0;

    static constexpr bool USE_COMPONENT_MESHES = true;
    std::map<std::string, Mesh> m_meshRegistry;
    static constexpr bool USE_BEZIER_WIRES = true;
    WireRenderer m_wireRenderer;
    static constexpr bool USE_BLINN_PHONG = true;
    static constexpr bool USE_GPU_INSTANCING = true;
    std::map<std::string, InstanceBuffer> m_instanceBuffers; // keyed same as m_meshRegistry

    int modelLoc = -1;
    int viewLoc = -1;
    int projLoc = -1;
    int colorLoc = -1;
    int brightnessLoc = -1;

    int m_litModelLoc = -1;
    int m_litViewLoc = -1;
    int m_litProjLoc = -1;
    int m_litColorLoc = -1;
    int m_litLightDirLoc = -1;
    int m_litViewPosLoc = -1;
    int m_litUseInstancingLoc = -1;

    int m_pcbModelLoc = -1;
    int m_pcbViewLoc = -1;
    int m_pcbProjLoc = -1;
    int m_pcbBoardColorLoc = -1;
    int m_pcbCopperColorLoc = -1;
};
