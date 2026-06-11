#pragma once

#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "../circuit/Circuit.h"
#include "MeshBuilder.h"

constexpr int WIRE_SEGMENTS = 32;
constexpr int TUBE_SIDES = 8;
constexpr float TUBE_RADIUS = 0.02f;
constexpr float WIRE_CTRL_LIFT = 0.4f;
constexpr float WIRE_END_OFFS = 0.2f;
constexpr float SIGNAL_SPEED = 0.8f;
constexpr float SIGNAL_RADIUS = 0.04f;
constexpr int SPHERE_STACKS = 6;
constexpr int SPHERE_SLICES = 8;

constexpr float WIRE_ZERO = 0.0f;
constexpr float WIRE_ONE = 1.0f;
constexpr float WIRE_TWO = 2.0f;
constexpr float WIRE_THREE = 3.0f;
constexpr float WIRE_SIX = 6.0f;
constexpr float WIRE_PI = 3.14159265358979323846f;
constexpr float WIRE_FULL_TURN = WIRE_TWO * WIRE_PI;
constexpr float WIRE_TANGENT_EPSILON = 1e-4f;
constexpr float WIRE_UP_DOT_LIMIT = 0.99f;

/// Evaluate a cubic Bezier curve at parameter t in [0, 1].
glm::vec3 bezierPoint(
    const glm::vec3& P0, const glm::vec3& P1,
    const glm::vec3& P2, const glm::vec3& P3,
    float t);

/// Evaluate the first derivative of the cubic Bezier at t.
glm::vec3 bezierTangent(
    const glm::vec3& P0, const glm::vec3& P1,
    const glm::vec3& P2, const glm::vec3& P3,
    float t);

/// Build a UV sphere centered at origin, radius 1.0.
Mesh buildSignalSphere();

class WireRenderer {
public:
    /// Initialize shaders and signal sphere mesh.
    bool init();

    /// Draw all wire tubes and active signal spheres.
    void draw(const std::vector<Connection>& connections,
              const std::vector<Component>& components,
              float elapsedTime,
              const glm::mat4& view,
              const glm::mat4& projection,
              const glm::vec3& viewPos);

    /// Mark wire geometry as needing rebuild next draw call.
    void markDirty();

    /// Free all GPU resources.
    void shutdown();

private:
    GLuint m_wireShader = 0;
    Mesh m_sphereMesh;
    bool m_wiresDirty = true;

    struct WireMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        int vertexCount = 0;
    };
    std::vector<WireMesh> m_wireMeshes;

    struct BezierParams {
        glm::vec3 P0, P1, P2, P3;
    };
    std::vector<BezierParams> m_bezierCache;
    std::vector<glm::vec3> m_cachedPositions;
    std::vector<Connection> m_cachedConnections;

    int m_modelLoc = -1;
    int m_viewLoc = -1;
    int m_projectionLoc = -1;
    int m_colorLoc = -1;

    /// Rebuild tube VBOs for the current connection list.
    void rebuildWireMeshes(const std::vector<Connection>& connections,
                           const std::vector<Component>& components);

    /// Upload one Bezier tube mesh to a VAO/VBO pair.
    WireMesh uploadWireMesh(const BezierParams& bp);

    /// Draw one animated signal sphere on a cached Bezier curve.
    void drawSignalSphere(const BezierParams& bp, float elapsedTime,
                          const glm::mat4& view, const glm::mat4& projection);
};
