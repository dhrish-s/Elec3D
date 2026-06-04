#include "Renderer.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

    shaderProgram = loadShaderProgram("shaders/cube.vert", "shaders/cube.frag");
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

    glEnable(GL_DEPTH_TEST);
    return true;
}

void Renderer::draw(const CircuitGraph& graph,
                    const glm::mat4& view,
                    const glm::mat4& projection)
{
    const float maxVoltage = 5.0f;
    const int gridSize = 10;
    const auto disconnectedNow = FindDisconnectedComponents(graph.components, graph.connections);
    const bool hasCycle = IsCircuitLooped(graph.components, graph.connections);
    const auto loopedSet = FindLoopedComponents(graph.components, graph.connections);

    glUseProgram(shaderProgram);
    glBindVertexArray(cubeVAO);
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

    for (const auto& c : graph.components) {
        if (visibleLayers.count(c.layer) == 0) continue;
        glm::vec3 pos(c.x, c.y + c.layer * 1.0f, c.z);

        glm::vec3 scale(1.0f);
        if (c.type == "Resistor")    scale = glm::vec3(1.0f, 0.5f, 0.5f);
        else if (c.type == "Capacitor") scale = glm::vec3(0.7f, 1.0f, 0.7f);
        else if (c.type == "Inductor")  scale = glm::vec3(1.2f, 1.2f, 1.2f);
        else if (c.type == "Diode")     scale = glm::vec3(0.5f, 0.5f, 1.5f);

        float voltage = componentVoltages.count(c.id) ? componentVoltages[c.id] : 0.0f;
        float normV = glm::clamp(voltage / maxVoltage, 0.0f, 1.0f);
        glm::vec3 color = glm::mix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), normV);

        if (c.id == hoverComponentId) {
            color = glm::vec3(1.0f, 1.0f, 0.0f);
        }
        glUniform3fv(colorLoc, 1, glm::value_ptr(color));

        float pulse = 0.5f + 0.5f * static_cast<float>(sin(glfwGetTime() * 3.0f));
        glUniform1f(brightnessLoc, pulse);

        float angle = (float)glfwGetTime();
        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
        model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, scale);

        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    }

    glBindVertexArray(0);

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
