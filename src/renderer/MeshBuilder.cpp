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
constexpr size_t VERTEX_FLOAT_COUNT = 6;

// Axis-aligned face normals, reused across every box-shaped mesh part.
const glm::vec3 NORMAL_POS_X(1.0f, 0.0f, 0.0f);
const glm::vec3 NORMAL_NEG_X(-1.0f, 0.0f, 0.0f);
const glm::vec3 NORMAL_POS_Y(0.0f, 1.0f, 0.0f);
const glm::vec3 NORMAL_NEG_Y(0.0f, -1.0f, 0.0f);
const glm::vec3 NORMAL_POS_Z(0.0f, 0.0f, 1.0f);
const glm::vec3 NORMAL_NEG_Z(0.0f, 0.0f, -1.0f);

/// Append one interleaved position+normal vertex and return its index.
uint32_t appendVertex(Mesh& mesh, const glm::vec3& pos, const glm::vec3& normal)
{
    mesh.vertices.push_back(pos.x);
    mesh.vertices.push_back(pos.y);
    mesh.vertices.push_back(pos.z);
    mesh.vertices.push_back(normal.x);
    mesh.vertices.push_back(normal.y);
    mesh.vertices.push_back(normal.z);
    return static_cast<uint32_t>(mesh.vertices.size() / VERTEX_FLOAT_COUNT) - 1u;
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

/// Add one flat quad face with its own 4 duplicated vertices and a single
/// constant normal, so it never shares geometry with a neighboring face.
void appendFlatQuad(Mesh& mesh, const glm::vec3& normal,
                     const glm::vec3& p0, const glm::vec3& p1,
                     const glm::vec3& p2, const glm::vec3& p3)
{
    const uint32_t a = appendVertex(mesh, p0, normal);
    const uint32_t b = appendVertex(mesh, p1, normal);
    const uint32_t c = appendVertex(mesh, p2, normal);
    const uint32_t d = appendVertex(mesh, p3, normal);
    appendQuad(mesh, a, b, c, d);
}

/// Build a rectangular box centered at the origin; each of the 6 faces
/// gets its own 4 duplicated vertices and constant axis-aligned normal.
Mesh buildBox(float length, float height, float depth)
{
    Mesh mesh;
    const float hx = length * HALF;
    const float hy = height * HALF;
    const float hz = depth * HALF;

    const glm::vec3 p000(-hx, -hy, -hz);
    const glm::vec3 p100(hx, -hy, -hz);
    const glm::vec3 p110(hx, hy, -hz);
    const glm::vec3 p010(-hx, hy, -hz);
    const glm::vec3 p001(-hx, -hy, hz);
    const glm::vec3 p101(hx, -hy, hz);
    const glm::vec3 p111(hx, hy, hz);
    const glm::vec3 p011(-hx, hy, hz);

    appendFlatQuad(mesh, NORMAL_NEG_Z, p000, p100, p110, p010);
    appendFlatQuad(mesh, NORMAL_POS_Z, p001, p101, p111, p011);
    appendFlatQuad(mesh, NORMAL_NEG_Y, p000, p100, p101, p001);
    appendFlatQuad(mesh, NORMAL_POS_Y, p010, p110, p111, p011);
    appendFlatQuad(mesh, NORMAL_POS_X, p100, p110, p111, p101);
    appendFlatQuad(mesh, NORMAL_NEG_X, p000, p010, p011, p001);
    return mesh;
}

/// Add a cylinder aligned to the Y axis. The curved side wall is smoothly
/// shaded (shared ring vertices, radial normal); the flat top/bottom caps
/// get their own duplicated rim vertices so the cap-to-wall edge stays sharp.
void appendCylinder(Mesh& mesh, float radius, float height, int sides)
{
    const float bottomY = -height * HALF;
    const float topY = height * HALF;

    std::vector<uint32_t> bottomRing;
    std::vector<uint32_t> topRing;
    bottomRing.reserve(static_cast<size_t>(sides));
    topRing.reserve(static_cast<size_t>(sides));

    for (int i = 0; i < sides; ++i) {
        const float angle = FULL_TURN * static_cast<float>(i) / static_cast<float>(sides);
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;
        const glm::vec3 radial = glm::normalize(glm::vec3(std::cos(angle), ZERO, std::sin(angle)));
        bottomRing.push_back(appendVertex(mesh, glm::vec3(x, bottomY, z), radial));
        topRing.push_back(appendVertex(mesh, glm::vec3(x, topY, z), radial));
    }

    for (int i = 0; i < sides; ++i) {
        const int next = (i + 1) % sides;
        appendQuad(mesh, bottomRing[static_cast<size_t>(i)], bottomRing[static_cast<size_t>(next)],
                   topRing[static_cast<size_t>(next)], topRing[static_cast<size_t>(i)]);
    }

    std::vector<uint32_t> bottomCapRing;
    std::vector<uint32_t> topCapRing;
    bottomCapRing.reserve(static_cast<size_t>(sides));
    topCapRing.reserve(static_cast<size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const float angle = FULL_TURN * static_cast<float>(i) / static_cast<float>(sides);
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;
        bottomCapRing.push_back(appendVertex(mesh, glm::vec3(x, bottomY, z), NORMAL_NEG_Y));
        topCapRing.push_back(appendVertex(mesh, glm::vec3(x, topY, z), NORMAL_POS_Y));
    }
    const uint32_t bottomCenter = appendVertex(mesh, glm::vec3(ZERO, bottomY, ZERO), NORMAL_NEG_Y);
    const uint32_t topCenter = appendVertex(mesh, glm::vec3(ZERO, topY, ZERO), NORMAL_POS_Y);

    for (int i = 0; i < sides; ++i) {
        const int next = (i + 1) % sides;
        appendTriangle(mesh, bottomCenter, bottomCapRing[static_cast<size_t>(next)],
                       bottomCapRing[static_cast<size_t>(i)]);
        appendTriangle(mesh, topCenter, topCapRing[static_cast<size_t>(i)],
                       topCapRing[static_cast<size_t>(next)]);
    }
}

/// Add a thin disc as a short cylinder to avoid single-plane z-fighting.
void appendDisc(Mesh& mesh, float centerX, float radius, float thickness)
{
    const size_t vertexStart = mesh.vertices.size();
    appendCylinder(mesh, radius, thickness, CYLINDER_SIDES);

    // Disc plates are shifted along X so capacitor gaps are visible from the default view.
    for (size_t i = vertexStart; i < mesh.vertices.size(); i += VERTEX_FLOAT_COUNT) {
        mesh.vertices[i] += centerX;
    }
}

/// Add a cone aligned to the Z axis (apex at +Z, base ring at -Z). The slant
/// side is smoothly shaded with the cone's true (non-radial) surface normal;
/// the flat base cap gets its own duplicated rim vertices.
void appendCone(Mesh& mesh, float radius, float height, int sides)
{
    std::vector<uint32_t> sideBaseRing;
    std::vector<glm::vec3> slantNormals;
    sideBaseRing.reserve(static_cast<size_t>(sides));
    slantNormals.reserve(static_cast<size_t>(sides));

    const float baseZ = -height * HALF;
    const float apexZ = height * HALF;

    for (int i = 0; i < sides; ++i) {
        const float angle = FULL_TURN * static_cast<float>(i) / static_cast<float>(sides);
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);
        // Slant normal: axial component carries the base radius, radial
        // components carry the height, per the cone's true (non-radial) surface.
        const glm::vec3 slant = glm::normalize(glm::vec3(height * cosA, height * sinA, radius));
        slantNormals.push_back(slant);
        sideBaseRing.push_back(appendVertex(mesh, glm::vec3(cosA * radius, sinA * radius, baseZ), slant));
    }

    for (int i = 0; i < sides; ++i) {
        const int next = (i + 1) % sides;
        // Apex is duplicated per triangle (a true apex normal is undefined);
        // reuse this wedge's own slant normal so shading stays smooth and outward.
        const uint32_t apex = appendVertex(mesh, glm::vec3(ZERO, ZERO, apexZ), slantNormals[static_cast<size_t>(i)]);
        appendTriangle(mesh, apex, sideBaseRing[static_cast<size_t>(i)], sideBaseRing[static_cast<size_t>(next)]);
    }

    std::vector<uint32_t> capRing;
    capRing.reserve(static_cast<size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const float angle = FULL_TURN * static_cast<float>(i) / static_cast<float>(sides);
        capRing.push_back(appendVertex(mesh, glm::vec3(std::cos(angle) * radius, std::sin(angle) * radius, baseZ),
                                        NORMAL_NEG_Z));
    }
    const uint32_t baseCenter = appendVertex(mesh, glm::vec3(ZERO, ZERO, baseZ), NORMAL_NEG_Z);
    for (int i = 0; i < sides; ++i) {
        const int next = (i + 1) % sides;
        appendTriangle(mesh, baseCenter, capRing[static_cast<size_t>(next)], capRing[static_cast<size_t>(i)]);
    }
}

/// Append one short flat ribbon segment between helix samples; the whole
/// quad shares one constant normal, oriented outward from the cylinder axis.
void appendHelixSegment(Mesh& mesh, const glm::vec3& a, const glm::vec3& b)
{
    const glm::vec3 tangent = glm::normalize(b - a);
    const glm::vec3 radialA = glm::normalize(glm::vec3(a.x, ZERO, a.z));
    const glm::vec3 radialB = glm::normalize(glm::vec3(b.x, ZERO, b.z));
    const glm::vec3 sideA = glm::normalize(glm::cross(tangent, radialA)) * HELIX_TUBE_RADIUS;
    const glm::vec3 sideB = glm::normalize(glm::cross(tangent, radialB)) * HELIX_TUBE_RADIUS;

    const glm::vec3 p0 = a + sideA;
    const glm::vec3 p1 = a - sideA;
    const glm::vec3 p2 = b - sideB;
    const glm::vec3 p3 = b + sideB;

    glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    const glm::vec3 midpointRadial = glm::normalize(glm::vec3((a.x + b.x) * HALF, ZERO, (a.z + b.z) * HALF));
    if (glm::dot(normal, midpointRadial) < ZERO) {
        normal = -normal;
    }

    appendFlatQuad(mesh, normal, p0, p1, p2, p3);
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
/// Each bevel triangle is its own face: 3 duplicated vertices, one constant
/// normal computed via cross product and oriented away from the box center.
void appendResistorBevels(Mesh& mesh)
{
    const float x0 = -RESISTOR_LEN * HALF;
    const float x1 = RESISTOR_LEN * HALF;
    const float y = RESISTOR_THICK * HALF;
    const float z = RESISTOR_THICK * HALF;
    const float innerLeft = x0 + BEVEL_DEPTH;
    const float innerRight = x1 - BEVEL_DEPTH;

    auto appendBevelTriangle = [&mesh](const glm::vec3& tip, const glm::vec3& p1, const glm::vec3& p2) {
        glm::vec3 normal = glm::normalize(glm::cross(p1 - tip, p2 - tip));
        const glm::vec3 faceCenter = (tip + p1 + p2) / 3.0f;
        if (glm::dot(normal, faceCenter) < ZERO) {
            normal = -normal;
        }
        const uint32_t a = appendVertex(mesh, tip, normal);
        const uint32_t b = appendVertex(mesh, p1, normal);
        const uint32_t c = appendVertex(mesh, p2, normal);
        appendTriangle(mesh, a, b, c);
    };

    const glm::vec3 leftTip(x0, ZERO, ZERO);
    const glm::vec3 leftA(innerLeft, -y, -z);
    const glm::vec3 leftB(innerLeft, y, -z);
    const glm::vec3 leftC(innerLeft, y, z);
    const glm::vec3 leftD(innerLeft, -y, z);
    appendBevelTriangle(leftTip, leftA, leftB);
    appendBevelTriangle(leftTip, leftB, leftC);
    appendBevelTriangle(leftTip, leftC, leftD);
    appendBevelTriangle(leftTip, leftD, leftA);

    const glm::vec3 rightTip(x1, ZERO, ZERO);
    const glm::vec3 rightA(innerRight, -y, -z);
    const glm::vec3 rightB(innerRight, y, -z);
    const glm::vec3 rightC(innerRight, y, z);
    const glm::vec3 rightD(innerRight, -y, z);
    appendBevelTriangle(rightTip, rightB, rightA);
    appendBevelTriangle(rightTip, rightC, rightB);
    appendBevelTriangle(rightTip, rightD, rightC);
    appendBevelTriangle(rightTip, rightA, rightD);
}

/// Build one registry entry and log if GPU upload did not produce drawable handles.
void addUploadedMesh(std::map<std::string, Mesh>& registry, const std::string& key, Mesh mesh)
{
    MeshBuilder::upload(mesh, true);
    if (mesh.vao == 0 || mesh.vbo == 0 || mesh.ebo == 0 || mesh.indexCount == 0) {
        std::cerr << "[Elec3D] MeshBuilder: failed to upload: " << key << "\n";
    }
    registry.emplace(key, std::move(mesh));
}

} // namespace

/// Upload CPU-side vertex and index data to GPU; fill vao/vbo/ebo.
void MeshBuilder::upload(Mesh& mesh, bool hasNormals)
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

    if (hasNormals) {
        const GLsizei stride = 6 * static_cast<GLsizei>(sizeof(float));
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                               reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
    } else {
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
    }

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
    const uint32_t indexOffset = static_cast<uint32_t>(mesh.vertices.size() / VERTEX_FLOAT_COUNT);
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
    const uint32_t indexOffset = static_cast<uint32_t>(mesh.vertices.size() / VERTEX_FLOAT_COUNT);
    for (size_t i = 0; i < base.vertices.size(); i += VERTEX_FLOAT_COUNT) {
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
