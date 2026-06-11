#include "WireRenderer.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

extern std::unordered_map<int, bool> signalEnabled;
extern std::set<int> visibleLayers;

namespace {

constexpr int RING_VERTEX_COUNT = TUBE_SIDES + 1;
constexpr int STRIP_VERTEX_COUNT = RING_VERTEX_COUNT * 2;
constexpr int CONNECTION_KEY_SCALE = 1000;
constexpr int SHADER_LOG_SIZE = 512;
constexpr unsigned int POSITION_FLOAT_COUNT = 3u;
const glm::vec3 WORLD_UP(WIRE_ZERO, WIRE_ONE, WIRE_ZERO);
const glm::vec3 WORLD_RIGHT(WIRE_ONE, WIRE_ZERO, WIRE_ZERO);
const glm::vec3 INACTIVE_WIRE_COLOR(0.55f, 0.55f, 0.60f);
const glm::vec3 SIGNAL_COLOR(0.20f, 0.85f, 0.40f);

/// Load one shader file into memory for OpenGL compilation.
bool loadShaderSource(const char* path, std::string& source)
{
    std::ifstream file(path);
    if (!file) {
        std::cerr << "[Elec3D] WireRenderer: failed to open shader: " << path << "\n";
        return false;
    }

    std::stringstream stream;
    stream << file.rdbuf();
    source = stream.str();
    return true;
}

/// Compile one shader object and log stderr details on failure.
GLuint compileShader(GLenum type, const char* path, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[SHADER_LOG_SIZE];
        glGetShaderInfoLog(shader, SHADER_LOG_SIZE, nullptr, infoLog);
        std::cerr << "[Elec3D] WireRenderer: shader compile failed: " << path << "\n";
        std::cerr << infoLog << "\n";
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

/// Link the wire shader program and log stderr details on failure.
GLuint loadWireShader()
{
    std::string vertexSource;
    std::string fragmentSource;
    if (!loadShaderSource("../shaders/wire.vert", vertexSource) ||
        !loadShaderSource("../shaders/wire.frag", fragmentSource)) {
        return 0;
    }

    GLuint vertex = compileShader(GL_VERTEX_SHADER, "../shaders/wire.vert", vertexSource.c_str());
    if (vertex == 0) {
        return 0;
    }

    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, "../shaders/wire.frag", fragmentSource.c_str());
    if (fragment == 0) {
        glDeleteShader(vertex);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[SHADER_LOG_SIZE];
        glGetProgramInfoLog(program, SHADER_LOG_SIZE, nullptr, infoLog);
        std::cerr << "[Elec3D] WireRenderer: shader link failed\n";
        std::cerr << infoLog << "\n";
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

/// Return a component pointer by ID without changing circuit storage.
const Component* findComponent(const std::vector<Component>& components, int id)
{
    for (const auto& component : components) {
        if (component.id == id) {
            return &component;
        }
    }
    return nullptr;
}

/// Convert component data into the same world-space convention as Renderer.
glm::vec3 componentPosition(const Component& component)
{
    return glm::vec3(component.x, component.y + static_cast<float>(component.layer), component.z);
}

/// Return true if connection endpoints changed since the previous rebuild.
bool connectionsChanged(const std::vector<Connection>& a, const std::vector<Connection>& b)
{
    if (a.size() != b.size()) {
        return true;
    }

    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].from_id != b[i].from_id || a[i].to_id != b[i].to_id) {
            return true;
        }
    }

    return false;
}

/// Return true if component positions changed since the previous rebuild.
bool positionsChanged(const std::vector<Component>& components,
                      const std::vector<glm::vec3>& cachedPositions)
{
    if (components.size() != cachedPositions.size()) {
        return true;
    }

    for (size_t i = 0; i < components.size(); ++i) {
        if (componentPosition(components[i]) != cachedPositions[i]) {
            return true;
        }
    }

    return false;
}

/// Append one position-only vertex to the CPU-side strip buffer.
void appendVertex(std::vector<float>& vertices, const glm::vec3& position)
{
    vertices.push_back(position.x);
    vertices.push_back(position.y);
    vertices.push_back(position.z);
}

/// Build one stable cross-section frame without normalizing a near-zero tangent.
void buildFrame(const glm::vec3& P0, const glm::vec3& P1,
                const glm::vec3& P2, const glm::vec3& P3, float t,
                glm::vec3& previousTangent, glm::vec3& normal, glm::vec3& binormal)
{
    glm::vec3 tangent = bezierTangent(P0, P1, P2, P3, t);
    if (glm::length(tangent) < WIRE_TANGENT_EPSILON) {
        tangent = previousTangent;
    } else {
        tangent = glm::normalize(tangent);
        previousTangent = tangent;
    }

    const glm::vec3 upReference =
        std::abs(glm::dot(tangent, WORLD_UP)) < WIRE_UP_DOT_LIMIT ? WORLD_UP : WORLD_RIGHT;
    binormal = glm::normalize(glm::cross(tangent, upReference));
    normal = glm::cross(binormal, tangent);
}

/// Build one closed tube ring around a Bezier sample point.
std::vector<glm::vec3> buildRing(const glm::vec3& P0, const glm::vec3& P1,
                                 const glm::vec3& P2, const glm::vec3& P3, float t,
                                 glm::vec3& previousTangent)
{
    glm::vec3 normal;
    glm::vec3 binormal;
    buildFrame(P0, P1, P2, P3, t, previousTangent, normal, binormal);

    const glm::vec3 center = bezierPoint(P0, P1, P2, P3, t);
    std::vector<glm::vec3> ring;
    ring.reserve(static_cast<size_t>(RING_VERTEX_COUNT));
    for (int i = 0; i <= TUBE_SIDES; ++i) {
        const float angle = WIRE_FULL_TURN * static_cast<float>(i) / static_cast<float>(TUBE_SIDES);
        const glm::vec3 radial = std::cos(angle) * normal + std::sin(angle) * binormal;
        ring.push_back(center + TUBE_RADIUS * radial);
    }
    return ring;
}

} // namespace

/// Evaluate a cubic Bezier curve at parameter t in [0, 1].
glm::vec3 bezierPoint(
    const glm::vec3& P0, const glm::vec3& P1,
    const glm::vec3& P2, const glm::vec3& P3,
    float t)
{
    const float u = WIRE_ONE - t;
    return u * u * u * P0
        + WIRE_THREE * u * u * t * P1
        + WIRE_THREE * u * t * t * P2
        + t * t * t * P3;
}

/// Evaluate the first derivative of the cubic Bezier at t.
glm::vec3 bezierTangent(
    const glm::vec3& P0, const glm::vec3& P1,
    const glm::vec3& P2, const glm::vec3& P3,
    float t)
{
    const float u = WIRE_ONE - t;
    return WIRE_THREE * u * u * (P1 - P0)
        + WIRE_SIX * u * t * (P2 - P1)
        + WIRE_THREE * t * t * (P3 - P2);
}

/// Build a UV sphere centered at origin, radius 1.0.
Mesh buildSignalSphere()
{
    Mesh mesh;
    for (int stack = 0; stack <= SPHERE_STACKS; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(SPHERE_STACKS);
        const float phi = WIRE_PI * v;
        const float y = std::cos(phi);
        const float ringRadius = std::sin(phi);

        for (int slice = 0; slice <= SPHERE_SLICES; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(SPHERE_SLICES);
            const float theta = WIRE_FULL_TURN * u;
            mesh.vertices.push_back(ringRadius * std::cos(theta));
            mesh.vertices.push_back(y);
            mesh.vertices.push_back(ringRadius * std::sin(theta));
        }
    }

    const int rowWidth = SPHERE_SLICES + 1;
    for (int stack = 0; stack < SPHERE_STACKS; ++stack) {
        for (int slice = 0; slice < SPHERE_SLICES; ++slice) {
            const uint32_t a = static_cast<uint32_t>(stack * rowWidth + slice);
            const uint32_t b = static_cast<uint32_t>((stack + 1) * rowWidth + slice);
            const uint32_t c = b + 1u;
            const uint32_t d = a + 1u;
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
            mesh.indices.push_back(a);
        }
    }

    return mesh;
}

/// Initialize shaders and signal sphere mesh.
bool WireRenderer::init()
{
    m_wireShader = loadWireShader();
    if (m_wireShader == 0) {
        return false;
    }

    m_modelLoc = glGetUniformLocation(m_wireShader, "model");
    m_viewLoc = glGetUniformLocation(m_wireShader, "view");
    m_projectionLoc = glGetUniformLocation(m_wireShader, "projection");
    m_colorLoc = glGetUniformLocation(m_wireShader, "objectColor");
    if (m_modelLoc == -1 || m_viewLoc == -1 || m_projectionLoc == -1 || m_colorLoc == -1) {
        std::cerr << "[Elec3D] WireRenderer: required uniform missing\n";
        return false;
    }

    m_sphereMesh = buildSignalSphere();
    MeshBuilder::upload(m_sphereMesh);
    if (m_sphereMesh.vao == 0 || m_sphereMesh.vbo == 0 ||
        m_sphereMesh.ebo == 0 || m_sphereMesh.indexCount == 0) {
        std::cerr << "[Elec3D] WireRenderer: failed to upload signal sphere\n";
        return false;
    }

    return true;
}

/// Draw all wire tubes and active signal spheres.
void WireRenderer::draw(const std::vector<Connection>& connections,
                        const std::vector<Component>& components,
                        float elapsedTime,
                        const glm::mat4& view,
                        const glm::mat4& projection,
                        const glm::vec3& viewPos)
{
    (void)viewPos;
    if (connectionsChanged(connections, m_cachedConnections) ||
        positionsChanged(components, m_cachedPositions)) {
        markDirty();
    }

    if (m_wiresDirty) {
        rebuildWireMeshes(connections, components);
        m_cachedConnections = connections;
        m_cachedPositions.clear();
        m_cachedPositions.reserve(components.size());
        for (const auto& component : components) {
            m_cachedPositions.push_back(componentPosition(component));
        }
        m_wiresDirty = false;
    }

    glUseProgram(m_wireShader);
    glUniformMatrix4fv(m_viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(m_projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

    const glm::mat4 model = glm::mat4(WIRE_ONE);
    glUniformMatrix4fv(m_modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniform3fv(m_colorLoc, 1, glm::value_ptr(INACTIVE_WIRE_COLOR));

    const std::unordered_set<int> loopedSet = FindLoopedComponents(components, connections);
    for (size_t i = 0; i < m_wireMeshes.size() && i < connections.size(); ++i) {
        const Component* from = findComponent(components, connections[i].from_id);
        const Component* to = findComponent(components, connections[i].to_id);
        if (!from || !to) {
            continue;
        }
        if (!loopedSet.count(from->id) || !loopedSet.count(to->id)) {
            continue;
        }
        if (!visibleLayers.count(from->layer) || !visibleLayers.count(to->layer)) {
            continue;
        }

        const auto& wireMesh = m_wireMeshes[i];
        glBindVertexArray(wireMesh.vao);
        for (int segment = 0; segment < WIRE_SEGMENTS; ++segment) {
            glDrawArrays(GL_TRIANGLE_STRIP,
                         segment * STRIP_VERTEX_COUNT,
                         STRIP_VERTEX_COUNT);
        }
    }

    for (size_t i = 0; i < connections.size() && i < m_bezierCache.size(); ++i) {
        const Component* from = findComponent(components, connections[i].from_id);
        const Component* to = findComponent(components, connections[i].to_id);
        if (!from || !to) {
            continue;
        }
        if (!loopedSet.count(from->id) || !loopedSet.count(to->id)) {
            continue;
        }
        if (!visibleLayers.count(from->layer) || !visibleLayers.count(to->layer)) {
            continue;
        }

        const int connKey = connections[i].from_id * CONNECTION_KEY_SCALE + connections[i].to_id;
        if (signalEnabled[connKey]) {
            drawSignalSphere(m_bezierCache[i], elapsedTime, view, projection);
        }
    }

    glBindVertexArray(0);
}

/// Mark wire geometry as needing rebuild next draw call.
void WireRenderer::markDirty()
{
    m_wiresDirty = true;
}

/// Free all GPU resources.
void WireRenderer::shutdown()
{
    for (auto& wireMesh : m_wireMeshes) {
        if (wireMesh.vbo != 0) {
            glDeleteBuffers(1, &wireMesh.vbo);
            wireMesh.vbo = 0;
        }
        if (wireMesh.vao != 0) {
            glDeleteVertexArrays(1, &wireMesh.vao);
            wireMesh.vao = 0;
        }
    }
    m_wireMeshes.clear();
    m_bezierCache.clear();

    if (m_sphereMesh.ebo != 0) {
        glDeleteBuffers(1, &m_sphereMesh.ebo);
        m_sphereMesh.ebo = 0;
    }
    if (m_sphereMesh.vbo != 0) {
        glDeleteBuffers(1, &m_sphereMesh.vbo);
        m_sphereMesh.vbo = 0;
    }
    if (m_sphereMesh.vao != 0) {
        glDeleteVertexArrays(1, &m_sphereMesh.vao);
        m_sphereMesh.vao = 0;
    }
    if (m_wireShader != 0) {
        glDeleteProgram(m_wireShader);
        m_wireShader = 0;
    }
}

/// Rebuild tube VBOs for the current connection list.
void WireRenderer::rebuildWireMeshes(const std::vector<Connection>& connections,
                                     const std::vector<Component>& components)
{
    for (auto& wireMesh : m_wireMeshes) {
        if (wireMesh.vbo != 0) {
            glDeleteBuffers(1, &wireMesh.vbo);
        }
        if (wireMesh.vao != 0) {
            glDeleteVertexArrays(1, &wireMesh.vao);
        }
    }
    m_wireMeshes.clear();
    m_bezierCache.clear();

    for (const auto& connection : connections) {
        const Component* from = findComponent(components, connection.from_id);
        const Component* to = findComponent(components, connection.to_id);
        if (!from || !to) {
            continue;
        }

        BezierParams bp;
        bp.P0 = componentPosition(*from) + glm::vec3(WIRE_ZERO, WIRE_ZERO, WIRE_END_OFFS);
        bp.P3 = componentPosition(*to) + glm::vec3(WIRE_ZERO, WIRE_ZERO, -WIRE_END_OFFS);
        bp.P1 = bp.P0 + glm::vec3(WIRE_ZERO, WIRE_CTRL_LIFT, WIRE_ZERO);
        bp.P2 = bp.P3 + glm::vec3(WIRE_ZERO, WIRE_CTRL_LIFT, WIRE_ZERO);
        m_bezierCache.push_back(bp);
        WireMesh mesh = uploadWireMesh(bp);
        if (mesh.vao == 0 || mesh.vbo == 0 || mesh.vertexCount == 0) {
            std::cerr << "[Elec3D] WireRenderer: failed to upload wire mesh\n";
        }
        m_wireMeshes.push_back(mesh);
    }
}

/// Upload one Bezier tube mesh to a VAO/VBO pair.
WireRenderer::WireMesh WireRenderer::uploadWireMesh(const BezierParams& bp)
{
    std::vector<std::vector<glm::vec3>> rings;
    rings.reserve(static_cast<size_t>(WIRE_SEGMENTS + 1));

    glm::vec3 previousTangent(WIRE_ZERO, WIRE_ZERO, WIRE_ONE);
    for (int segment = 0; segment <= WIRE_SEGMENTS; ++segment) {
        const float t = static_cast<float>(segment) / static_cast<float>(WIRE_SEGMENTS);
        rings.push_back(buildRing(bp.P0, bp.P1, bp.P2, bp.P3, t, previousTangent));
    }

    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(WIRE_SEGMENTS * STRIP_VERTEX_COUNT * POSITION_FLOAT_COUNT));
    for (int segment = 0; segment < WIRE_SEGMENTS; ++segment) {
        for (int side = 0; side < RING_VERTEX_COUNT; ++side) {
            appendVertex(vertices, rings[static_cast<size_t>(segment)][static_cast<size_t>(side)]);
            appendVertex(vertices, rings[static_cast<size_t>(segment + 1)][static_cast<size_t>(side)]);
        }
    }

    WireMesh mesh;
    mesh.vertexCount = static_cast<int>(vertices.size() / POSITION_FLOAT_COUNT);
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return mesh;
}

/// Draw one animated signal sphere on a cached Bezier curve.
void WireRenderer::drawSignalSphere(const BezierParams& bp, float elapsedTime,
                                    const glm::mat4& view, const glm::mat4& projection)
{
    const float t = std::fmod(elapsedTime * SIGNAL_SPEED, WIRE_ONE);
    const glm::vec3 position = bezierPoint(bp.P0, bp.P1, bp.P2, bp.P3, t);
    glm::mat4 model = glm::translate(glm::mat4(WIRE_ONE), position);
    model = glm::scale(model, glm::vec3(SIGNAL_RADIUS));

    glUseProgram(m_wireShader);
    glUniformMatrix4fv(m_modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(m_viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(m_projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(m_colorLoc, 1, glm::value_ptr(SIGNAL_COLOR));
    glBindVertexArray(m_sphereMesh.vao);
    glDrawElements(GL_TRIANGLES, m_sphereMesh.indexCount, GL_UNSIGNED_INT, nullptr);
}
