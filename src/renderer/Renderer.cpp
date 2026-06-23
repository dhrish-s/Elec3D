#include "Renderer.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "MeshBuilder.h"
#include "WireRenderer.h"

namespace {
constexpr float PCB_HALF_EXTENT = 0.5f;
constexpr float PCB_UV_MIN = 0.0f;
constexpr float PCB_UV_MAX = 1.0f;
constexpr float PCB_LAYER_HEIGHT = 0.8f;
constexpr float PCB_MARGIN = 1.0f;
constexpr float PCB_MIN_SIZE = 1.0f;
constexpr float PCB_Y_SCALE = 1.0f;
constexpr float PCB_BOARD_R = 26.0f / 255.0f;
constexpr float PCB_BOARD_G = 58.0f / 255.0f;
constexpr float PCB_BOARD_B = 42.0f / 255.0f;
constexpr float PCB_COPPER_R = 184.0f / 255.0f;
constexpr float PCB_COPPER_G = 115.0f / 255.0f;
constexpr float PCB_COPPER_B = 51.0f / 255.0f;
constexpr int PCB_FLOATS_PER_VERTEX = 5;
constexpr int PCB_POSITION_COMPONENT_COUNT = 3;
constexpr int PCB_UV_COMPONENT_COUNT = 2;
constexpr int PCB_INDEX_COUNT = 6;
constexpr float PCB_MARGIN_SIDE_COUNT = 2.0f;
}

extern std::set<int> visibleLayers;
extern bool showGrid;
extern std::unordered_map<int, std::vector<PulseTrail>> pulseTrails;
extern std::unordered_map<int, float> componentVoltages;
extern std::unordered_map<int, bool> signalEnabled;
extern int hoverComponentId;

/// Loads one shader source file into a string for OpenGL compilation.
static bool loadShaderSource(const char* path, std::string& source)
{
    std::ifstream file(path);
    if (!file) {
        std::cerr << "[Elec3D] Failed to open: " << path << " - file not found\n";
        return false;
    }

    std::stringstream stream;
    stream << file.rdbuf();
    source = stream.str();
    return true;
}

/// Compiles a shader and logs the task-required error format on failure.
static unsigned int compileShader(unsigned int type, const char* shaderName, const char* source)
{
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "[Elec3D] Shader compile failed: " << shaderName << "\n";
        std::cerr << infoLog << "\n";
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

/// Links the compiled cube shaders into the single shader program used by the old path.
static unsigned int loadShaderProgram(const char* vertexPath, const char* fragmentPath)
{
    std::string vertexCode;
    std::string fragmentCode;
    if (!loadShaderSource(vertexPath, vertexCode) || !loadShaderSource(fragmentPath, fragmentCode)) {
        return 0;
    }

    unsigned int vertex = compileShader(GL_VERTEX_SHADER, vertexPath, vertexCode.c_str());
    if (vertex == 0) {
        return 0;
    }

    unsigned int fragment = compileShader(GL_FRAGMENT_SHADER, fragmentPath, fragmentCode.c_str());
    if (fragment == 0) {
        glDeleteShader(vertex);
        return 0;
    }

    unsigned int program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "[Elec3D] Shader compile failed: program\n";
        std::cerr << infoLog << "\n";
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

/// Returns a uniform location and logs when the shader does not expose it.
static int findUniform(unsigned int program, const char* uniformName)
{
    int location = glGetUniformLocation(program, uniformName);
    if (location == -1) {
        std::cerr << "[Elec3D] Uniform not found: " << uniformName << "\n";
    }
    return location;
}

bool Renderer::init()
{
    float cubeVertices[] = {
        -0.5f, -0.5f, -0.5f,
        0.5f, -0.5f, -0.5f,
        0.5f,  0.5f, -0.5f,
       -0.5f,  0.5f, -0.5f,
       -0.5f, -0.5f,  0.5f,
        0.5f, -0.5f,  0.5f,
        0.5f,  0.5f,  0.5f,
       -0.5f,  0.5f,  0.5f
    };

    unsigned int cubeIndices[] = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        0, 1, 5, 5, 4, 0,
        2, 3, 7, 7, 6, 2,
        1, 2, 6, 6, 5, 1,
        0, 3, 7, 7, 4, 0
    };

    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    glGenBuffers(1, &cubeEBO);
    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineVBO);
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glGenVertexArrays(1, &pulseVAO);
    glGenBuffers(1, &pulseVBO);
    glBindVertexArray(pulseVAO);
    glBindBuffer(GL_ARRAY_BUFFER, pulseVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    float axisVertices[] = {
        0.0f, 0.0f, 0.0f,   3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,   0.0f, 3.0f, 0.0f,
        0.0f, 0.0f, 0.0f,   0.0f, 0.0f, 3.0f
    };

    glGenVertexArrays(1, &axisVAO);
    glGenBuffers(1, &axisVBO);
    glBindVertexArray(axisVAO);
    glBindBuffer(GL_ARRAY_BUFFER, axisVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axisVertices), axisVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    std::vector<float> gridVertices;
    const int gridSize = 10;
    for (int i = -gridSize; i <= gridSize; ++i) {
        gridVertices.push_back((float)i); gridVertices.push_back(0.0f); gridVertices.push_back((float)-gridSize);
        gridVertices.push_back((float)i); gridVertices.push_back(0.0f); gridVertices.push_back((float)gridSize);
        gridVertices.push_back((float)-gridSize); gridVertices.push_back(0.0f); gridVertices.push_back((float)i);
        gridVertices.push_back((float)gridSize);  gridVertices.push_back(0.0f); gridVertices.push_back((float)i);
    }

    glGenVertexArrays(1, &gridVAO);
    glGenBuffers(1, &gridVBO);
    glBindVertexArray(gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, gridVertices.size() * sizeof(float), gridVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    const float pcbVertices[] = {
        -PCB_HALF_EXTENT, 0.0f, -PCB_HALF_EXTENT, PCB_UV_MIN, PCB_UV_MIN,
         PCB_HALF_EXTENT, 0.0f, -PCB_HALF_EXTENT, PCB_UV_MAX, PCB_UV_MIN,
         PCB_HALF_EXTENT, 0.0f,  PCB_HALF_EXTENT, PCB_UV_MAX, PCB_UV_MAX,
        -PCB_HALF_EXTENT, 0.0f,  PCB_HALF_EXTENT, PCB_UV_MIN, PCB_UV_MAX
    };

    const unsigned int pcbIndices[] = {
        0, 1, 2,
        2, 3, 0
    };

    glGenVertexArrays(1, &pcbVAO);
    glGenBuffers(1, &pcbVBO);
    glGenBuffers(1, &pcbEBO);
    glBindVertexArray(pcbVAO);
    glBindBuffer(GL_ARRAY_BUFFER, pcbVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(pcbVertices), pcbVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pcbEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(pcbIndices), pcbIndices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, PCB_POSITION_COMPONENT_COUNT, GL_FLOAT, GL_FALSE,
        PCB_FLOATS_PER_VERTEX * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, PCB_UV_COMPONENT_COUNT, GL_FLOAT, GL_FALSE,
        PCB_FLOATS_PER_VERTEX * sizeof(float),
        reinterpret_cast<void*>(PCB_POSITION_COMPONENT_COUNT * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    shaderProgram = loadShaderProgram("../shaders/cube.vert", "../shaders/cube.frag");
    if (shaderProgram == 0) {
        return false;
    }

    modelLoc = findUniform(shaderProgram, "model");
    viewLoc = findUniform(shaderProgram, "view");
    projLoc = findUniform(shaderProgram, "projection");
    colorLoc = findUniform(shaderProgram, "objectColor");
    brightnessLoc = findUniform(shaderProgram, "brightness");
    if (modelLoc == -1 || viewLoc == -1 || projLoc == -1 || colorLoc == -1 || brightnessLoc == -1) {
        return false;
    }

    m_shaderLit = loadShaderProgram("../shaders/component.vert", "../shaders/component.frag");
    if (m_shaderLit == 0) {
        return false;
    }

    m_litModelLoc = findUniform(m_shaderLit, "uModel");
    m_litViewLoc = findUniform(m_shaderLit, "uView");
    m_litProjLoc = findUniform(m_shaderLit, "uProjection");
    m_litColorLoc = findUniform(m_shaderLit, "uColor");
    m_litLightDirLoc = findUniform(m_shaderLit, "uLightDir");
    m_litViewPosLoc = findUniform(m_shaderLit, "uViewPos");
    m_litUseInstancingLoc = findUniform(m_shaderLit, "uUseInstancing");
    if (m_litModelLoc == -1 || m_litViewLoc == -1 || m_litProjLoc == -1 ||
        m_litColorLoc == -1 || m_litLightDirLoc == -1 || m_litViewPosLoc == -1 ||
        m_litUseInstancingLoc == -1) {
        return false;
    }

    m_shaderPcb = loadShaderProgram("../shaders/pcb.vert", "../shaders/pcb.frag");
    if (m_shaderPcb == 0) {
        return false;
    }

    m_pcbModelLoc = findUniform(m_shaderPcb, "uModel");
    m_pcbViewLoc = findUniform(m_shaderPcb, "uView");
    m_pcbProjLoc = findUniform(m_shaderPcb, "uProjection");
    m_pcbBoardColorLoc = findUniform(m_shaderPcb, "uBoardColor");
    m_pcbCopperColorLoc = findUniform(m_shaderPcb, "uCopperColor");
    if (m_pcbModelLoc == -1 || m_pcbViewLoc == -1 || m_pcbProjLoc == -1 ||
        m_pcbBoardColorLoc == -1 || m_pcbCopperColorLoc == -1) {
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    if (USE_COMPONENT_MESHES) {
        m_meshRegistry = MeshBuilder::buildRegistry();

        // Instance VBOs attach to each mesh's EXISTING vao (built above by
        // MeshBuilder). This setup runs once regardless of
        // USE_GPU_INSTANCING so toggling the flag never needs re-init.
        for (auto& [key, mesh] : m_meshRegistry) {
            InstanceBuffer buf;
            glGenBuffers(1, &buf.vbo);
            buf.capacity = INITIAL_INSTANCE_CAPACITY;

            glBindVertexArray(mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, buf.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(buf.capacity * sizeof(InstanceData)),
                         nullptr, GL_DYNAMIC_DRAW);

            for (int i = 0; i < 4; ++i) {
                const GLuint loc = static_cast<GLuint>(INSTANCE_ATTRIB_LOCATION_BASE + i);
                glEnableVertexAttribArray(loc);
                glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                    reinterpret_cast<void*>(offsetof(InstanceData, modelMatrix) + i * sizeof(glm::vec4)));
                glVertexAttribDivisor(loc, 1);
            }
            glEnableVertexAttribArray(INSTANCE_COLOR_ATTRIB_LOCATION);
            glVertexAttribPointer(INSTANCE_COLOR_ATTRIB_LOCATION, 3, GL_FLOAT, GL_FALSE,
                sizeof(InstanceData), reinterpret_cast<void*>(offsetof(InstanceData, color)));
            glVertexAttribDivisor(INSTANCE_COLOR_ATTRIB_LOCATION, 1);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
            m_instanceBuffers[key] = buf;
        }
    }
    if (USE_BEZIER_WIRES) {
        if (!m_wireRenderer.init()) {
            std::cerr << "[Elec3D] WireRenderer failed to init\n";
            return false;
        }
    }
    return true;
}

/// Grow a mesh type's instance buffer if needed. Never shrinks; never
/// reallocates when the existing capacity already fits the request.
void Renderer::ensureInstanceCapacity(InstanceBuffer& buf, int neededCount)
{
    if (neededCount <= buf.capacity) {
        return;
    }
    const int newCapacity = std::max(neededCount,
        buf.capacity == 0 ? INITIAL_INSTANCE_CAPACITY : buf.capacity * 2);
    glBindBuffer(GL_ARRAY_BUFFER, buf.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(newCapacity * sizeof(InstanceData)),
                 nullptr, GL_DYNAMIC_DRAW);
    buf.capacity = newCapacity;
}

/// Draw one board-sized rectangle for every visible layer that has at least
/// one component. The geometry is scaled from the circuit's global X/Z bounds
/// so all layers line up like a stacked PCB.
void Renderer::drawPcbSubstrates(const CircuitGraph& graph,
                                 const glm::mat4& view,
                                 const glm::mat4& projection)
{
    if (graph.components.empty()) {
        return;
    }

    float minX = graph.components.front().x;
    float maxX = graph.components.front().x;
    float minZ = graph.components.front().z;
    float maxZ = graph.components.front().z;
    std::set<int> layersToDraw;

    for (const auto& c : graph.components) {
        minX = std::min(minX, c.x);
        maxX = std::max(maxX, c.x);
        minZ = std::min(minZ, c.z);
        maxZ = std::max(maxZ, c.z);

        if (visibleLayers.count(c.layer) > 0) {
            layersToDraw.insert(c.layer);
        }
    }

    if (layersToDraw.empty()) {
        return;
    }

    const float boardWidth = std::max((maxX - minX) + PCB_MARGIN * PCB_MARGIN_SIDE_COUNT, PCB_MIN_SIZE);
    const float boardDepth = std::max((maxZ - minZ) + PCB_MARGIN * PCB_MARGIN_SIDE_COUNT, PCB_MIN_SIZE);
    const float centerX = (minX + maxX) * PCB_HALF_EXTENT;
    const float centerZ = (minZ + maxZ) * PCB_HALF_EXTENT;
    const glm::vec3 boardColor(PCB_BOARD_R, PCB_BOARD_G, PCB_BOARD_B);
    const glm::vec3 copperColor(PCB_COPPER_R, PCB_COPPER_G, PCB_COPPER_B);

    glUseProgram(m_shaderPcb);
    glUniformMatrix4fv(m_pcbViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(m_pcbProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(m_pcbBoardColorLoc, 1, glm::value_ptr(boardColor));
    glUniform3fv(m_pcbCopperColorLoc, 1, glm::value_ptr(copperColor));

    glBindVertexArray(pcbVAO);
    for (int layer : layersToDraw) {
        const float layerY = static_cast<float>(layer) * PCB_LAYER_HEIGHT;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(centerX, layerY, centerZ));
        model = glm::scale(model, glm::vec3(boardWidth, PCB_Y_SCALE, boardDepth));
        glUniformMatrix4fv(m_pcbModelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glDrawElements(GL_TRIANGLES, PCB_INDEX_COUNT, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

void Renderer::draw(const CircuitGraph& graph, const Camera& camera,
                    float aspectRatio, float elapsedTime)
{
    // Frame-time diagnostic: useful for comparing instanced vs
    // non-instanced performance on large circuits. Reports at most once
    // per second so it never floods stderr during normal use.
    // Tracks two numbers: total inter-frame time (includes the caller's
    // per-frame simulation work in main.cpp, outside this function) and
    // render-only time (just this draw() call) — this separation matters
    // because the simulation solve, not rendering, can dominate at scale.
    static double lastFrameTime = 0.0;
    static double lastFpsReportTime = 0.0;
    static int framesSinceReport = 0;
    static double renderTimeAccumulatorMs = 0.0;
    const double now = glfwGetTime();
    const double frameDeltaMs = (now - lastFrameTime) * 1000.0;
    lastFrameTime = now;
    ++framesSinceReport;
    if (now - lastFpsReportTime >= 1.0) {
        const double fps = framesSinceReport / (now - lastFpsReportTime);
        const double avgRenderMs = renderTimeAccumulatorMs / framesSinceReport;
        std::cerr << "[Elec3D] Frame time: " << frameDeltaMs << " ms, FPS: " << fps
                   << ", avg render-only time: " << avgRenderMs << " ms\n";
        framesSinceReport = 0;
        lastFpsReportTime = now;
        renderTimeAccumulatorMs = 0.0;
    }
    const double renderStart = glfwGetTime();

    const float maxVoltage = 5.0f;
    const int gridSize = 10;
    const auto disconnectedNow = FindDisconnectedComponents(graph.components, graph.connections);
    const bool hasCycle = IsCircuitLooped(graph.components, graph.connections);
    const auto loopedSet = FindLoopedComponents(graph.components, graph.connections);
    const glm::mat4 view = camera.getView();
    const glm::mat4 projection = camera.getProjection(aspectRatio);
    const glm::vec3 cameraPosition = glm::vec3(glm::inverse(view)[3]);

    drawPcbSubstrates(graph, view, projection);

    const unsigned int activeShader = USE_BLINN_PHONG ? m_shaderLit : shaderProgram;
    glUseProgram(activeShader);
    if (!USE_COMPONENT_MESHES) {
        glBindVertexArray(cubeVAO);
    }
    if (USE_BLINN_PHONG) {
        const glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
        glUniformMatrix4fv(m_litViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(m_litProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(m_litLightDirLoc, 1, glm::value_ptr(lightDir));
        glUniform3fv(m_litViewPosLoc, 1, glm::value_ptr(cameraPosition));
        // Set once per frame for every component draw call that follows;
        // nothing between here and the next frame changes this value.
        glUniform1i(m_litUseInstancingLoc, USE_GPU_INSTANCING ? 1 : 0);
    } else {
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    }

    if (USE_GPU_INSTANCING && USE_COMPONENT_MESHES) {
        // Group every visible component's per-instance data by mesh-registry
        // key, using the exact same model/color logic as the non-instanced
        // loop below (voltage-to-color mix, hover highlight, Y rotation).
        std::map<std::string, std::vector<InstanceData>> grouped;
        const float angle = (float)glfwGetTime();
        for (const auto& c : graph.components) {
            if (visibleLayers.count(c.layer) == 0) continue;
            glm::vec3 pos(c.x, c.y + c.layer * 1.0f, c.z);

            float voltage = componentVoltages.count(c.id) ? componentVoltages[c.id] : 0.0f;
            float normV = glm::clamp(voltage / maxVoltage, 0.0f, 1.0f);
            glm::vec3 color = glm::mix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), normV);
            if (c.id == hoverComponentId) {
                color = glm::vec3(1.0f, 1.0f, 0.0f);
            }

            glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
            model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));

            const std::string key = (m_meshRegistry.count(c.type) > 0) ? c.type : std::string("Cube");
            InstanceData inst;
            inst.modelMatrix = model;
            inst.color = color;
            inst.pad = 0.0f;
            grouped[key].push_back(inst);
        }

        for (auto& [key, instances] : grouped) {
            if (instances.empty()) continue;
            InstanceBuffer& buf = m_instanceBuffers[key];
            ensureInstanceCapacity(buf, static_cast<int>(instances.size()));
            glBindBuffer(GL_ARRAY_BUFFER, buf.vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                static_cast<GLsizeiptr>(instances.size() * sizeof(InstanceData)), instances.data());
            const Mesh& mesh = m_meshRegistry.at(key);
            glBindVertexArray(mesh.vao);
            glDrawElementsInstanced(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr,
                static_cast<GLsizei>(instances.size()));
        }
        glBindVertexArray(0);
    } else {
        for (const auto& c : graph.components) {
            if (visibleLayers.count(c.layer) == 0) continue;
            glm::vec3 pos(c.x, c.y + c.layer * 1.0f, c.z);

            float voltage = componentVoltages.count(c.id) ? componentVoltages[c.id] : 0.0f;
            float normV = glm::clamp(voltage / maxVoltage, 0.0f, 1.0f);
            glm::vec3 color = glm::mix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), normV);

            if (c.id == hoverComponentId) {
                color = glm::vec3(1.0f, 1.0f, 0.0f);
            }

            if (USE_BLINN_PHONG) {
                glUniform3fv(m_litColorLoc, 1, glm::value_ptr(color));
            } else {
                glUniform3fv(colorLoc, 1, glm::value_ptr(color));
                float pulse = 0.5f + 0.5f * static_cast<float>(sin(glfwGetTime() * 3.0f));
                glUniform1f(brightnessLoc, pulse);
            }

            float angle = (float)glfwGetTime();
            glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
            model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
            if (USE_COMPONENT_MESHES) {
                const std::string& key = c.type;
                const Mesh& mesh = (m_meshRegistry.count(key) > 0)
                    ? m_meshRegistry.at(key)
                    : m_meshRegistry.at("Cube");
                glBindVertexArray(mesh.vao);
                glUniformMatrix4fv(USE_BLINN_PHONG ? m_litModelLoc : modelLoc, 1, GL_FALSE, glm::value_ptr(model));
                glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
            } else {
                glm::vec3 scale(1.0f);
                if (c.type == "Resistor")    scale = glm::vec3(1.0f, 0.5f, 0.5f);
                else if (c.type == "Capacitor") scale = glm::vec3(0.7f, 1.0f, 0.7f);
                else if (c.type == "Inductor")  scale = glm::vec3(1.2f, 1.2f, 1.2f);
                else if (c.type == "Diode")     scale = glm::vec3(0.5f, 0.5f, 1.5f);

                model = glm::scale(model, scale);
                glUniformMatrix4fv(USE_BLINN_PHONG ? m_litModelLoc : modelLoc, 1, GL_FALSE, glm::value_ptr(model));
                glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
            }
        }

        glBindVertexArray(0);
    }

    glUseProgram(shaderProgram);
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    if (USE_BLINN_PHONG) {
        // When the lit shader was active for components, brightnessLoc was
        // never touched this frame; without this it would default to 0 and
        // axis/grid lines would render black. Restore full brightness here.
        glUniform1f(brightnessLoc, 1.0f);
    }

    glBindVertexArray(axisVAO);
    glm::mat4 axisModel = glm::mat4(1.0f);
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(axisModel));

    glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(1.0f, 0.0f, 0.0f)));
    glDrawArrays(GL_LINES, 0, 2);
    glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(0.0f, 1.0f, 0.0f)));
    glDrawArrays(GL_LINES, 2, 2);
    glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f, 1.0f)));
    glDrawArrays(GL_LINES, 4, 2);

    if (showGrid) {
        glBindVertexArray(gridVAO);
        glm::mat4 gridModel = glm::mat4(1.0f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(gridModel));
        glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(0.4f)));
        glDrawArrays(GL_LINES, 0, (gridSize * 4 + 4));
        glBindVertexArray(0);
    }

    if (USE_BEZIER_WIRES) {
        m_wireRenderer.draw(
            graph.connections, graph.components,
            elapsedTime, view, projection,
            cameraPosition);
    } else {
        glBindVertexArray(0);
        glBindVertexArray(lineVAO);

        for (const auto& conn : graph.connections) {
        const Component* from = nullptr;
        const Component* to = nullptr;

        for (const auto& c : graph.components) {
            if (c.id == conn.from_id) from = &c;
            if (c.id == conn.to_id) to = &c;
        }

        if (!from || !to) continue;

        if (!loopedSet.count(from->id) || !loopedSet.count(to->id))
            continue;

        bool fromVisible = visibleLayers.count(from->layer);
        bool toVisible = visibleLayers.count(to->layer);

        if (!fromVisible || !toVisible) {
            int connKey = from->id * 1000 + to->id;
            pulseTrails[connKey].clear();
            continue;
        }

        glm::vec3 fromPos(from->x, from->y + from->layer * 1.0f, from->z);
        glm::vec3 toPos(to->x, to->y + to->layer * 1.0f, to->z);

        float lineVertices[] = {
            fromPos.x, fromPos.y, fromPos.z,
            toPos.x,   toPos.y,   toPos.z
        };

        glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(lineVertices), lineVertices);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

        glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(0.8f)));
        glDrawArrays(GL_LINES, 0, 2);
        glBindVertexArray(0);

        int connKey = from->id * 1000 + to->id;
        if (!signalEnabled[connKey]) continue;

        std::vector<glm::vec3> pulsePoints;
        std::vector<float> pulseBrightness;

        if (disconnectedNow.empty() && hasCycle) {
            auto& trail = pulseTrails[connKey];
            float period = 2.0f;
            float t = static_cast<float>(fmod(glfwGetTime(), period)) / period;
            glm::vec3 pulsePos = (1.0f - t) * fromPos + t * toPos;

            trail.push_back({ pulsePos, 0.0f });

            for (auto& pt : trail)
                pt.age += 0.02f;

            trail.erase(std::remove_if(trail.begin(), trail.end(),
                [](const PulseTrail& p) { return p.age > 1.0f; }), trail.end());

            glBindVertexArray(pulseVAO);
            glDisable(GL_DEPTH_TEST);

            for (const auto& pt : trail) {
                pulsePoints.push_back(pt.pos);
                pulseBrightness.push_back(1.0f - pt.age);
            }
        }

        glBindVertexArray(pulseVAO);
        glBindBuffer(GL_ARRAY_BUFFER, pulseVBO);
        glBufferData(GL_ARRAY_BUFFER, pulsePoints.size() * sizeof(glm::vec3), pulsePoints.data(), GL_DYNAMIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        if (disconnectedNow.empty() && hasCycle) {
            for (size_t i = 0; i < pulsePoints.size(); ++i) {
                glm::mat4 model = glm::mat4(1.0f);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
                glUniform1f(brightnessLoc, pulseBrightness[i]);
                glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.0f)));
                glPointSize(10.0f);
                glDrawArrays(GL_POINTS, static_cast<GLint>(i), 1);
            }
        }

        glEnable(GL_DEPTH_TEST);
        }
    }

    renderTimeAccumulatorMs += (glfwGetTime() - renderStart) * 1000.0;
}
