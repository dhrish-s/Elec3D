#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <glad/glad.h>

/// A GPU-ready mesh: vertex buffer, index buffer, and draw count.
/// build*() functions below interleave {x, y, z, nx, ny, nz} (stride 6).
/// WireRenderer's signal sphere instead uploads position-only stride-3
/// data through the same upload() entry point (see hasNormals below).
struct Mesh {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

namespace MeshBuilder {
    /// Upload CPU-side vertex and index data to GPU; fill vao/vbo/ebo.
    /// hasNormals=false binds stride-3 position-only at location 0 (the
    /// existing behavior WireRenderer's sphere mesh depends on); true
    /// binds stride-6 position+normal at locations 0 and 1.
    void upload(Mesh& mesh, bool hasNormals = false);

    /// Build battery: cylinder r=0.12, h=0.5, 16 sides, disc top/bottom caps.
    Mesh buildBattery();

    /// Build resistor: box 0.4x0.15x0.15 with flat beveled end caps.
    Mesh buildResistor();

    /// Build capacitor: two discs r=0.18, gap=0.08, thin connecting stub.
    Mesh buildCapacitor();

    /// Build inductor: cylinder r=0.15, h=0.3, plus helix polyline on surface.
    Mesh buildInductor();

    /// Build diode: cone base r=0.12, height 0.25, apex pointing in +Z.
    Mesh buildDiode();

    /// Build cube: fallback geometry for unknown component types.
    Mesh buildCube();

    /// Build and upload all meshes; return registry keyed by component type string.
    std::map<std::string, Mesh> buildRegistry();
}
