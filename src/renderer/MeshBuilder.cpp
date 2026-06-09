#include "MeshBuilder.h"

#include <cmath>
#include <iostream>
#include <utility>

#include <glm/glm.hpp>

namespace {

constexpr int CYLINDER_SIDES = 16;
constexpr float BATTERY_RADIUS = 0.12f;
constexpr float BATTERY_HEIGHT = 0.50f;
constexpr float RESISTOR_LEN = 0.40f;
constexpr float RESISTOR_THICK = 0.15f;
constexpr float CAP_DISC_RADIUS = 0.18f;
constexpr float CAP_GAP = 0.08f;
constexpr float CAP_STUB_RADIUS = 0.04f;
constexpr float INDUCTOR_RADIUS = 0.15f;
constexpr float INDUCTOR_HEIGHT = 0.30f;
constexpr int HELIX_SEGMENTS = 32;
constexpr int HELIX_COILS = 4;
constexpr float DIODE_BASE_R = 0.12f;
constexpr float DIODE_HEIGHT = 0.25f;

constexpr float ZERO = 0.0f;
constexpr float HALF = 0.5f;
constexpr float TWO = 2.0f;
constexpr float PI = 3.14159265358979323846f;
constexpr float FULL_TURN = TWO * PI;
constexpr float CUBE_HALF_EXTENT = 0.5f;
constexpr float BEVEL_DEPTH = 0.04f;
constexpr float HELIX_TUBE_RADIUS = 0.025f;
constexpr float HELIX_INSET = 0.015f;
constexpr float CAP_DISC_THICKNESS = 0.025f;
constexpr float DIODE_BASE_Z = -DIODE_HEIGHT * HALF;
constexpr size_t POSITION_FLOAT_COUNT = 3;

/// Append one position-only vertex and return its index.
uint32_t appendVertex(Mesh& mesh, float x, float y, float z)
{
    mesh.vertices.push_back(x);
    mesh.vertices.push_back(y);
    mesh.vertices.push_back(z);
    return static_cast<uint32_t>(mesh.vertices.size() / POSITION_FLOAT_COUNT) - 1u;
}

/// Add one triangle to the mesh index buffer.
void appendTriangle(Mesh& mesh, uint32_t a, uint32_t b, uint32_t c)
{
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}

/// Add one quad as two triangles so all meshes share GL_TRIANGLES.
void appendQuad(Mesh& mesh, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    appendTriangle(mesh, a, b, c);
    appendTriangle(mesh, c, d, a);
}

/// Build a rectangular box centered at the origin.
Mesh buildBox(float length, float height, float depth)
{
    Mesh mesh;
    const float hx = length * HALF;
    const float hy = height * HALF;
    const float hz = depth * HALF;

    const uint32_t v0 = appendVertex(mesh, -hx, -hy, -hz);
    const uint32_t v1 = appendVertex(mesh,  hx, -hy, -hz);
    const uint32_t v2 = appendVertex(mesh,  hx,  hy, -hz);
    const uint32_t v3 = appendVertex(mesh, -hx,  hy, -hz);
    const uint32_t v4 = appendVertex(mesh, -hx, -hy,  hz);
    const uint32_t v5 = appendVertex(mesh,  hx, -hy,  hz);
    const uint32_t v6 = appendVertex(mesh,  hx,  hy,  hz);
    const uint32_t v7 = appendVertex(mesh, -hx,  hy,  hz);

    appendQuad(mesh, v0, v1, v2, v3);
    appendQuad(mesh, v4, v5, v6, v7);
    appendQuad(mesh, v0, v1, v5, v4);
    appendQuad(mesh, v2, v3, v7, v6);
    appendQuad(mesh, v1, v2, v6, v5);
    appendQuad(mesh, v0, v3, v7, v4);
    return mesh;
}

/// Add a cylinder aligned to the Y axis.
void appendCylinder(Mesh& mesh, float radius, float height, int sides)
{
    const float bottomY = -height * HALF;
    const float topY = height * HALF;
    const uint32_t bottomCenter = appendVertex(mesh, ZERO, bottomY, ZERO);
    const uint32_t topCenter = appendVertex(mesh, ZERO, topY, ZERO);

    std::vector<uint32_t> bottomRing;
    std::vector<uint32_t> topRing;
    bottomRing.reserve(static_cast<size_t>(sides));
    topRing.reserve(static_cast<size_t>(sides));

    for (int i = 0; i < sides; ++i) {
        const float angle = FULL_TURN * static_cast<float>(i) / static_cast<float>(sides);
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;
        bottomRing.push_back(appendVertex(mesh, x, bottomY, z));
        topRing.push_back(appendVertex(mesh, x, topY, z));
    }

    for (int i = 0; i < sides; ++i) {
        const int next = (i + 1) % sides;
        appendQuad(mesh, bottomRing[static_cast<size_t>(i)], bottomRing[static_cast<size_t>(next)],
                   topRing[static_cast<size_t>(next)], topRing[static_cast<size_t>(i)]);
        appendTriangle(mesh, bottomCenter, bottomRing[static_cast<size_t>(next)],
                       bottomRing[static_cast<size_t>(i)]);
        appendTriangle(mesh, topCenter, topRing[static_cast<size_t>(i)],
                       topRing[static_cast<size_t>(next)]);
    }
}

/// Add a thin disc as a short cylinder to avoid single-plane z-fighting.
void appendDisc(Mesh& mesh, float centerX, float radius, float thickness)
{
    const size_t vertexStart = mesh.vertices.size();
    appendCylinder(mesh, radius, thickness, CYLINDER_SIDES);

    // Disc plates are shifted along X so capacitor gaps are visible from the default view.
    for (size_t i = vertexStart; i < mesh.vertices.size(); i += POSITION_FLOAT_COUNT) {
        mesh.vertices[i] += centerX;
    }
}

/// Add a cone aligned to the Z axis.
void appendCone(Mesh& mesh, float radius, float height, int sides)
{
    const uint32_t baseCenter = appendVertex(mesh, ZERO, ZERO, -height * HALF);
    const uint32_t apex = appendVertex(mesh, ZERO, ZERO, height * HALF);
    std::vector<uint32_t> baseRing;
    baseRing.reserve(static_cast<size_t>(sides));

    for (int i = 0; i < sides; ++i) {
        const float angle = FULL_TURN * static_cast<float>(i) / static_cast<float>(sides);
        baseRing.push_back(appendVertex(mesh, std::cos(angle) * radius,
                                        std::sin(angle) * radius, -height * HALF));
    }

    for (int i = 0; i < sides; ++i) {
        const int next = (i + 1) % sides;
        appendTriangle(mesh, apex, baseRing[static_cast<size_t>(i)],
                       baseRing[static_cast<size_t>(next)]);
        appendTriangle(mesh, baseCenter, baseRing[static_cast<size_t>(next)],
                       baseRing[static_cast<size_t>(i)]);
    }
}

/// Append one short tube section between helix samples.
void appendHelixSegment(Mesh& mesh, const glm::vec3& a, const glm::vec3& b)
{
    const glm::vec3 tangent = glm::normalize(b - a);
    const glm::vec3 radialA = glm::normalize(glm::vec3(a.x, ZERO, a.z));
    const glm::vec3 radialB = glm::normalize(glm::vec3(b.x, ZERO, b.z));
    const glm::vec3 sideA = glm::normalize(glm::cross(tangent, radialA)) * HELIX_TUBE_RADIUS;
    const glm::vec3 sideB = glm::normalize(glm::cross(tangent, radialB)) * HELIX_TUBE_RADIUS;

    // Four points form a tiny ribbon segment, enough to read as a coil without a normal attribute.
    const uint32_t v0 = appendVertex(mesh, a.x + sideA.x, a.y + sideA.y, a.z + sideA.z);
    const uint32_t v1 = appendVertex(mesh, a.x - sideA.x, a.y - sideA.y, a.z - sideA.z);
    const uint32_t v2 = appendVertex(mesh, b.x - sideB.x, b.y - sideB.y, b.z - sideB.z);
    const uint32_t v3 = appendVertex(mesh, b.x + sideB.x, b.y + sideB.y, b.z + sideB.z);
    appendQuad(mesh, v0, v1, v2, v3);
}

/// Add a raised helix on the inductor cylinder surface.
void appendHelix(Mesh& mesh)
{
    const float radius = INDUCTOR_RADIUS + HELIX_INSET;
    for (int i = 0; i < HELIX_SEGMENTS; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(HELIX_SEGMENTS);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(HELIX_SEGMENTS);
        const float angle0 = FULL_TURN * static_cast<float>(HELIX_COILS) * t0;
        const float angle1 = FULL_TURN * static_cast<float>(HELIX_COILS) * t1;
        const glm::vec3 a(std::cos(angle0) * radius,
                          -INDUCTOR_HEIGHT * HALF + INDUCTOR_HEIGHT * t0,
                          std::sin(angle0) * radius);
        const glm::vec3 b(std::cos(angle1) * radius,
                          -INDUCTOR_HEIGHT * HALF + INDUCTOR_HEIGHT * t1,
                          std::sin(angle1) * radius);
        appendHelixSegment(mesh, a, b);
    }
}

/// Add two beveled end caps to make the resistor read differently from a cube.
void appendResistorBevels(Mesh& mesh)
{
    const float x0 = -RESISTOR_LEN * HALF;
    const float x1 = RESISTOR_LEN * HALF;
    const float y = RESISTOR_THICK * HALF;
    const float z = RESISTOR_THICK * HALF;
    const float innerLeft = x0 + BEVEL_DEPTH;
    const float innerRight = x1 - BEVEL_DEPTH;

    const uint32_t leftTip = appendVertex(mesh, x0, ZERO, ZERO);
    const uint32_t leftA = appendVertex(mesh, innerLeft, -y, -z);
    const uint32_t leftB = appendVertex(mesh, innerLeft,  y, -z);
    const uint32_t leftC = appendVertex(mesh, innerLeft,  y,  z);
    const uint32_t leftD = appendVertex(mesh, innerLeft, -y,  z);
    appendTriangle(mesh, leftTip, leftA, leftB);
    appendTriangle(mesh, leftTip, leftB, leftC);
    appendTriangle(mesh, leftTip, leftC, leftD);
    appendTriangle(mesh, leftTip, leftD, leftA);

    const uint32_t rightTip = appendVertex(mesh, x1, ZERO, ZERO);
    const uint32_t rightA = appendVertex(mesh, innerRight, -y, -z);
    const uint32_t rightB = appendVertex(mesh, innerRight,  y, -z);
    const uint32_t rightC = appendVertex(mesh, innerRight,  y,  z);
    const uint32_t rightD = appendVertex(mesh, innerRight, -y,  z);
    appendTriangle(mesh, rightTip, rightB, rightA);
    appendTriangle(mesh, rightTip, rightC, rightB);
    appendTriangle(mesh, rightTip, rightD, rightC);
    appendTriangle(mesh, rightTip, rightA, rightD);
}

/// Build one registry entry and log if GPU upload did not produce drawable handles.
void addUploadedMesh(std::map<std::string, Mesh>& registry, const std::string& key, Mesh mesh)
{
    MeshBuilder::upload(mesh);
    if (mesh.vao == 0 || mesh.vbo == 0 || mesh.ebo == 0 || mesh.indexCount == 0) {
        std::cerr << "[Elec3D] MeshBuilder: failed to upload: " << key << "\n";
    }
    registry.emplace(key, std::move(mesh));
}

} // namespace

/// Upload CPU-side vertex and index data to GPU; fill vao/vbo/ebo.
void MeshBuilder::upload(Mesh& mesh)
{
    mesh.indexCount = static_cast<GLsizei>(mesh.indices.size());
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)),
                 mesh.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

/// Build battery: cylinder r=0.12, h=0.5, 16 sides, disc top/bottom caps.
Mesh MeshBuilder::buildBattery()
{
    Mesh mesh;
    appendCylinder(mesh, BATTERY_RADIUS, BATTERY_HEIGHT, CYLINDER_SIDES);
    return mesh;
}

/// Build resistor: box 0.4x0.15x0.15 with flat beveled end caps.
Mesh MeshBuilder::buildResistor()
{
    Mesh mesh = buildBox(RESISTOR_LEN - BEVEL_DEPTH * TWO, RESISTOR_THICK, RESISTOR_THICK);
    appendResistorBevels(mesh);
    return mesh;
}

/// Build capacitor: two discs r=0.18, gap=0.08, thin connecting stub.
Mesh MeshBuilder::buildCapacitor()
{
    Mesh mesh;
    appendDisc(mesh, -CAP_GAP * HALF, CAP_DISC_RADIUS, CAP_DISC_THICKNESS);
    appendDisc(mesh, CAP_GAP * HALF, CAP_DISC_RADIUS, CAP_DISC_THICKNESS);

    Mesh stub = buildBox(CAP_GAP, CAP_STUB_RADIUS, CAP_STUB_RADIUS);
    const uint32_t indexOffset = static_cast<uint32_t>(mesh.vertices.size() / POSITION_FLOAT_COUNT);
    mesh.vertices.insert(mesh.vertices.end(), stub.vertices.begin(), stub.vertices.end());
    for (uint32_t index : stub.indices) {
        mesh.indices.push_back(index + indexOffset);
    }
    return mesh;
}

/// Build inductor: cylinder r=0.15, h=0.3, plus helix tube faces.
Mesh MeshBuilder::buildInductor()
{
    Mesh mesh;
    appendCylinder(mesh, INDUCTOR_RADIUS, INDUCTOR_HEIGHT, CYLINDER_SIDES);
    appendHelix(mesh);
    return mesh;
}

/// Build diode: cone base r=0.12, height 0.25, apex pointing in +Z.
Mesh MeshBuilder::buildDiode()
{
    Mesh mesh;
    appendCone(mesh, DIODE_BASE_R, DIODE_HEIGHT, CYLINDER_SIDES);

    // A base plate makes the diode direction readable even without lighting normals.
    Mesh base = buildBox(DIODE_BASE_R * TWO, DIODE_BASE_R * TWO, CAP_DISC_THICKNESS);
    const uint32_t indexOffset = static_cast<uint32_t>(mesh.vertices.size() / POSITION_FLOAT_COUNT);
    for (size_t i = 0; i < base.vertices.size(); i += POSITION_FLOAT_COUNT) {
        base.vertices[i + 2u] += DIODE_BASE_Z - CAP_DISC_THICKNESS;
    }
    mesh.vertices.insert(mesh.vertices.end(), base.vertices.begin(), base.vertices.end());
    for (uint32_t index : base.indices) {
        mesh.indices.push_back(index + indexOffset);
    }
    return mesh;
}

/// Build cube: fallback geometry for unknown component types.
Mesh MeshBuilder::buildCube()
{
    return buildBox(CUBE_HALF_EXTENT * TWO, CUBE_HALF_EXTENT * TWO, CUBE_HALF_EXTENT * TWO);
}

/// Build and upload all meshes; return registry keyed by component type string.
std::map<std::string, Mesh> MeshBuilder::buildRegistry()
{
    std::map<std::string, Mesh> registry;
    addUploadedMesh(registry, "Battery", buildBattery());
    addUploadedMesh(registry, "Resistor", buildResistor());
    addUploadedMesh(registry, "Capacitor", buildCapacitor());
    addUploadedMesh(registry, "Inductor", buildInductor());
    addUploadedMesh(registry, "Diode", buildDiode());
    addUploadedMesh(registry, "Cube", buildCube());
    return registry;
}
