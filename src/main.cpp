#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include<sstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <unordered_map>

#include <set>  // Required for layer toggle functionality
#include <deque>

#include <Eigen/Dense>
#include<unordered_set>

#include "commands/AddComponentCommand.h"
#include "commands/ChangeLayerCommand.h"
#include "commands/ChangeTypeCommand.h"
#include "commands/Command.h"
#include "commands/ConnectCommand.h"
#include "commands/DeleteComponentCommand.h"
#include "commands/EditPropertyCommand.h"
#include "commands/MoveComponentCommand.h"
#include "circuit/Circuit.h"
#include "io/LayoutSerializer.h"
#include "renderer/Camera.h"
#include "renderer/Renderer.h"
#include "sim/MNASolver.h"
#include "sim/TransientSolver.h"


std::set<int> visibleLayers = {1, 2};  // Initially visible layers

int screenWidth = 1280;
int screenHeight = 720;

float lastX = screenWidth / 2.0f;
float lastY = screenHeight / 2.0f;
bool firstMouse = true;

bool isDragging = false;
bool isPanning = false;
Camera camera;


bool showGrid = true;  // Toggle visibility

CommandHistory commandHistory;
bool simulationDirty = true;

//Now voltage
std::unordered_map<int, std::deque<float>> voltageHistory;
const int maxVoltageHistory = 200;  // Number of samples to keep per component
constexpr float TRANSIENT_DT_DEFAULT = 1e-5f;
constexpr float TRANSIENT_TOTAL_DEFAULT = 5e-3f;
constexpr int FIRST_SOLVE_LOG_NODE_LIMIT = 5;
float transientDt = TRANSIENT_DT_DEFAULT;
float transientTotal = TRANSIENT_TOTAL_DEFAULT;
int transientNode = 0;
std::vector<float> transientResult;



void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        const bool ctrlDown = (mods & GLFW_MOD_CONTROL) != 0;
        // WantTextInput blocks shortcuts only during active typing, not after a field keeps keyboard focus.
        const bool imguiTextActive =
            ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;

        // Undo/redo should not steal keystrokes while the user is typing in ImGui.
        if (ctrlDown && !imguiTextActive && key == GLFW_KEY_Z) {
            if (commandHistory.canUndo()) {
                commandHistory.undo();
                simulationDirty = true;  // Undo may change values/topology, so solve once again.
            }
            return;
        }

        // Redo mirrors undo and marks the solver cache stale for the replayed edit.
        if (ctrlDown && !imguiTextActive && key == GLFW_KEY_Y) {
            if (commandHistory.canRedo()) {
                commandHistory.redo();
                simulationDirty = true;  // Redo reapplies a model change that voltages depend on.
            }
            return;
        }

        int layer = key - GLFW_KEY_0;  // Maps '1' -> 1, '2' -> 2, etc.
        if (layer >= 0 && layer <= 9) {
            if (visibleLayers.count(layer))
                visibleLayers.erase(layer);  // Hide layer
            else
                visibleLayers.insert(layer);  // Show layer
        }
    }
}


void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
        firstMouse = true;
        return;
    }

    if (!isDragging && !isPanning) return;

    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos - lastX);
    float yoffset = static_cast<float>(lastY - ypos);
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    if (isDragging) {
        camera.onMouseDrag(xoffset, yoffset);
    }

    if (isPanning) {
        camera.pan(xoffset, yoffset);
    }
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
                isDragging = false;
                isPanning = false;
                firstMouse = true;
                return;
            }

            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                isDragging = true;
            } else {
                isPanning = true;
            }
            firstMouse = true;  // reset to prevent jump
        } else if (action == GLFW_RELEASE) {
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                isDragging = false;
            } else {
                isPanning = false;
            }
        }
    }
}


void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) 
    // Adjust the field of view (FOV) based on scroll input
{
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
        return;
    }

    camera.onScroll(static_cast<float>(yoffset));
}

std::unordered_map<int, std::vector<PulseTrail>> pulseTrails;

std::unordered_map<int, float> componentVoltages;

std::unordered_map<int, bool> signalEnabled;  // connectionKey -> toggle state

int hoverComponentId = -1;  // ID of the component currently hovered over

int selectedComponentId = -1;  // ID of component currently selected (on click)

bool RayIntersectsAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
    const glm::vec3& boxMin, const glm::vec3& boxMax, float& t)
{
    float tmin = (boxMin.x - rayOrigin.x) / rayDir.x;
    float tmax = (boxMax.x - rayOrigin.x) / rayDir.x;

    if (tmin > tmax) std::swap(tmin, tmax);

    float tymin = (boxMin.y - rayOrigin.y) / rayDir.y;
    float tymax = (boxMax.y - rayOrigin.y) / rayDir.y;

    if ((tmin > tymax) || (tymin > tmax)) return false;

    if (tymin > tmin) tmin = tymin;
    if (tymax < tmax) tmax = tymax;

    float tzmin = (boxMin.z - rayOrigin.z) / rayDir.z;
    float tzmax = (boxMax.z - rayOrigin.z) / rayDir.z;

    if(tzmin > tzmax) std::swap(tzmin, tzmax);

    if ((tmin > tzmax) || (tzmin > tmax)) return false;

    t = tmin;
    return true; // Ray intersects AABB
}

void SimulateVoltages(const std::vector<Component>& components, const std::vector<Connection>& connections) {
    componentVoltages.clear();  // Reset voltages

    // 1. Set voltage for batteries
    for (const auto& c : components) {
        if (c.type == "Battery") {
            componentVoltages[c.id] = c.voltage;  // Initialize voltage
        }
    }

    // 2. Propagate voltage through connections
    bool updated = true;
    int maxIterations = 20;  // Prevent infinite loops

    while (updated && maxIterations-- > 0) {
        updated = false;

        for (const auto& conn : connections) {
            const Component* from = nullptr;
            const Component* to = nullptr;
            for (const auto& c : components) {
                if (c.id == conn.from_id) from = &c;
                if (c.id == conn.to_id) to = &c;
            }

            if (!from || !to) continue;

            float fromV = componentVoltages.count(from->id) ? componentVoltages[from->id] : -1;
            float toV = componentVoltages.count(to->id) ? componentVoltages[to->id] : -1;

            if (fromV >= 0 && toV < 0) {
                componentVoltages[to->id] = fromV - to->resistance;
                updated = true;
            } else if (toV >= 0 && fromV < 0) {
                componentVoltages[from->id] = toV - from->resistance;
                updated = true;
            }
        }
    }
}


void SolveKirchhoffVoltages(const std::vector<Component>& components, const std::vector<Connection>& connections, int groundId) {
    componentVoltages.clear();
    
    auto loopedSet = FindLoopedComponents(components, connections);

    int N = components.size();

    // === Step 0: Check for disconnected components ===
    auto disconnected = FindDisconnectedComponents(components, connections);
    // if (!disconnected.empty()) {
    //     std::cerr << "Disconnected components found: ";
    //     for (int id : disconnected) {
    //         std::cerr << id << " ";
    //     }
    //     std::cerr << std::endl;
    //     return;  // Early exit if there are disconnected components
    // }
    // the above line is used to check for disconnected components in the graph

    // === Step 1: Create ID ↔ Index mappings ===
    std::unordered_map<int, int> idToIndex;
    std::unordered_map<int, int> indexToId;
    for (int i = 0; i < N; ++i) {
        idToIndex[components[i].id] = i;
        indexToId[i] = components[i].id;
    }

    // === Step 2: Build matrix A and vector b ===
    Eigen::MatrixXd A = Eigen::MatrixXd::Zero(N, N);
    Eigen::VectorXd b = Eigen::VectorXd::Zero(N);

    // === Step 3: Fill A with conductances (1/R) from connections ===
    for (const auto& conn : connections) {

        if (!loopedSet.count(conn.from_id) || !loopedSet.count(conn.to_id))
        continue;  // skip non-loop parts


        int i = idToIndex[conn.from_id];
        int j = idToIndex[conn.to_id];

        float R1 = components[i].resistance;
        float R2 = components[j].resistance;
        float R = R1 + R2;  // Total resistance in the path
        
        if (R == 0) R = 1.0f;  // Prevent division by zero (fallback)
        
        float G = 1.0f / R;  // Conductance
        
        A(i, i) += G;
        A(j, j) += G;
        A(i, j) -= G;
        A(j, i) -= G;

    }

    // === Step 4: Set known voltages from batteries ===
    for (const auto& c : components) {
        if (c.type == "Battery" && loopedSet.count(c.id)) {
            int idx = idToIndex[c.id];
            A.row(idx).setZero();
            A(idx, idx) = 1.0;
            b(idx) = c.voltage;
        }
    }

    //  === Step 5: Apply ground constraint based on user selection ===
    if (idToIndex.count(groundId)) {
        int groundIdx = idToIndex[groundId];
        A.row(groundIdx).setZero();
        A(groundIdx, groundIdx) = 1.0;
        b(groundIdx) = 0.0;
    }

    // === Step 6: Solve Ax = b ===
    Eigen::VectorXd voltages = A.colPivHouseholderQr().solve(b);

    // === Step 7: Store voltages by original component ID ===
    for (int i = 0; i < N; ++i) {
        int id = indexToId[i];
        componentVoltages[id] = static_cast<float>(voltages(i));
    }
}



int main()
{
    //intialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    constexpr int MSAA_SAMPLES = 4;
    glfwWindowHint(GLFW_SAMPLES, MSAA_SAMPLES);


    // Create a windowed mode window and its OpenGL context
    GLFWwindow* window = glfwCreateWindow(screenWidth, screenHeight, "Welcome to Elec3D", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback); 
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);  // unLock cursor and for lock it is GLFW_CURSOR_DISABLED


    //Load OpenGL function pointers using glad
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    

    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    std::cout << "ImGui font atlas built: " << io.Fonts->IsBuilt() << std::endl;


    ImGui::StyleColorsDark();  // Optional: use dark theme

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Force font texture upload
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    unsigned char* pixels;
    int width, height;
    atlas->GetTexDataAsRGBA32(&pixels, &width, &height);


    #include <filesystem>
    std::cout << "Current working directory: " << std::filesystem::current_path() << std::endl;

    CircuitGraph graph = LayoutSerializer::load("src/layout.json");
    // Keep one shared graph model so commands and rendering edit the same data.
    std::vector<Component>& components = graph.components;
    // Connections live beside components because every command edits graph topology.
    std::vector<Connection>& connections = graph.connections;
    std::cerr << "[Elec3D] Loaded " << components.size() << " components\n";
    std::cerr << "[Elec3D] Loaded " << connections.size() << " connections\n";
    if (!connections.empty()) {
        std::cerr << "[Elec3D] First connection: "
                  << connections.front().from_id << " -> "
                  << connections.front().to_id << "\n";
    }

    for(const auto& conn : connections){
        int key = conn.from_id * 1000 + conn.to_id;
        signalEnabled[key] = true;  // default to ON
    }

    for (const auto& c : components) {
        std::cout << "Component " << c.id << " (" << c.type << ") at ["<< c.x << ", " << c.y << ", " << c.z << "] on layer " << c.layer << std::endl;
    }


    Renderer renderer;
    if (!renderer.init()) {
        return -1;
    }

    // === Auto-connect popup state ===
    static bool showConnectionPopup = false;
    int watchedComponentId = -1;  // -1 = none selected
    // this watchedComponentId is used to determine which component to connect to when the popup is shown
    static int lastAddedComponentId = -1;
    static int popupDirection = 0;  // 0 = new -> existing, 1 = existing -> new
    static int popupStrategy = 0;   // 0 = nearest, 1 = first

    static int groundComponentId = 0;
    simulationDirty = true;  // First frame must solve once so cached voltages start valid.
    std::vector<float> cachedNodeVoltages;

    // Inside render loop:
    while (!glfwWindowShouldClose(window)) {

        std::vector<int> disconnectedNow = FindDisconnectedComponents(components, connections);
        bool hasCycle = IsCircuitLooped(components, connections);

        int lowestComponentId = components.empty() ? -1 : components.front().id;
        bool groundNodeExists = false;
        for (const auto& c : components) {
            if (c.id < lowestComponentId) {
                lowestComponentId = c.id;
            }
            if (c.id == groundComponentId) {
                groundNodeExists = true;
            }
        }

        int activeGroundNodeId = groundNodeExists ? groundComponentId : lowestComponentId;
        static int lastLoggedGroundNodeId = -1;
        if (activeGroundNodeId != -1 && activeGroundNodeId != lastLoggedGroundNodeId) {
            std::cerr << "[Elec3D] Ground node: " << activeGroundNodeId << "\n";
            lastLoggedGroundNodeId = activeGroundNodeId;
        }

        std::ostringstream circuitSignatureStream;
        circuitSignatureStream << activeGroundNodeId;
        for (const auto& c : components) {
            circuitSignatureStream << "|" << c.id << ":" << c.type << ":" << c.x << ":" << c.y << ":" << c.z
                                   << ":" << c.layer << ":" << c.resistance << ":" << c.capacitance
                                   << ":" << c.inductance << ":" << c.voltageSource << ":" << c.voltage;
        }
        for (const auto& conn : connections) {
            circuitSignatureStream << "|" << conn.from_id << ">" << conn.to_id;
        }

        static std::string lastCircuitSignature;
        static bool solveFailureLogged = false;
        const std::string circuitSignature = circuitSignatureStream.str();
        if (circuitSignature != lastCircuitSignature) {
            solveFailureLogged = false;
            lastCircuitSignature = circuitSignature;
        }

        CircuitGraph simulationGraph;
        simulationGraph.components = components;
        simulationGraph.connections = connections;

        if (simulationDirty && activeGroundNodeId != -1) {
            std::cerr << "[Elec3D] Simulation dirty -  re-solving ("
                      << components.size() << " components)\n";
            std::ostringstream solverLogSink;
            std::streambuf* originalCerr = std::cerr.rdbuf(solverLogSink.rdbuf());
            cachedNodeVoltages = MNASolver::solve(simulationGraph, activeGroundNodeId);
            std::cerr.rdbuf(originalCerr);
            simulationDirty = false;  // Reuse this answer until UI edits change the circuit again.
        }

        std::vector<float> nodeVoltages = cachedNodeVoltages;

        int maxComponentId = -1;
        for (const auto& c : components) {
            if (c.id > maxComponentId) {
                maxComponentId = c.id;
            }
        }
        if (nodeVoltages.empty() && maxComponentId >= 0) {
            if (!solveFailureLogged) {
                std::cerr << "[Elec3D] Voltage solve failed -  circuit may be disconnected\n";
                solveFailureLogged = true;
            }
            nodeVoltages.assign(static_cast<size_t>(maxComponentId + 1), 0.0f);
        } else if (maxComponentId >= 0 && maxComponentId >= static_cast<int>(nodeVoltages.size())) {
            nodeVoltages.resize(static_cast<size_t>(maxComponentId + 1), 0.0f);
        }

        static bool firstVoltageSolveLogged = false;
        if (!firstVoltageSolveLogged && !nodeVoltages.empty()) {
            firstVoltageSolveLogged = true;
            std::cerr << "[Elec3D] First voltage solve:\n";
            const int nodesToLog = std::min(
                FIRST_SOLVE_LOG_NODE_LIMIT,
                static_cast<int>(nodeVoltages.size())
            );
            for (int i = 0; i < nodesToLog; ++i) {
                std::cerr << "  Node " << i << ": " << std::fixed << std::setprecision(3)
                          << nodeVoltages[static_cast<size_t>(i)] << " V\n";
            }
        }

        componentVoltages.clear();
        for (const auto& c : components) {
            float voltage = 0.0f;
            if (c.id >= 0 && c.id < static_cast<int>(nodeVoltages.size())) {
                voltage = nodeVoltages[static_cast<size_t>(c.id)];
            }

            componentVoltages[c.id] = voltage;
            voltageHistory[c.id].push_back(voltage);
            if (voltageHistory[c.id].size() > 120) {
                voltageHistory[c.id].pop_front();
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    
        glClearColor(0.12f, 0.12f, 0.15f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspectRatio = (float)screenWidth / (float)screenHeight;

        // Mouse Hover Detection
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        lastX = static_cast<float>(mouseX);
        lastY = static_cast<float>(mouseY);


        glm::vec3 rayDir = camera.screenPosToWorldRay(static_cast<float>(mouseX), static_cast<float>(mouseY), static_cast<float>(screenWidth), static_cast<float>(screenHeight));
        glm::vec3 rayOrigin = camera.getPosition();  // Camera position
        
        hoverComponentId = -1;  // Reset hover ID
        float closestT = 1e6;  // Initialize to a large value

        for (const auto& c:components)
        {
            if (visibleLayers.count(c.layer) == 0) continue;  // Skip invisible layers

            glm::vec3 pos(c.x, c.y + c.layer * 1.0f, c.z);
            glm::vec3 scale(1.0f);

            if (c.type == "Resistor")    scale = glm::vec3(1.0f, 0.5f, 0.5f);
            else if (c.type == "Capacitor") scale = glm::vec3(0.7f, 1.0f, 0.7f);
            else if (c.type == "Inductor")  scale = glm::vec3(1.2f, 1.2f, 1.2f);
            else if (c.type == "Diode")     scale = glm::vec3(0.5f, 0.5f, 1.5f);

            glm::vec3 halfExtents = scale * 0.5f;  // Half extents for AABB
            glm::vec3 boxMin = pos - halfExtents;
            glm::vec3 boxMax = pos + halfExtents;

            float t;
            if (RayIntersectsAABB(rayOrigin, rayDir, boxMin, boxMax, t)) {
                if (t < closestT) {
                    closestT = t;
                    hoverComponentId = c.id;  // Store the ID of the hovered component
                }
            }
        }

        // === CLICK SELECTION ===
        static bool wasMouseDown = false;
        static bool wasPressOverImGui = false;
        bool isMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        const bool imguiWantsMouse =
            ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
        if (isMouseDown && !wasMouseDown) {
            // Remember the press origin so closing a popup cannot leak its release into viewport selection.
            wasPressOverImGui = imguiWantsMouse;
        }

        if (!isMouseDown && wasMouseDown && !imguiWantsMouse && !wasPressOverImGui) {
            selectedComponentId = hoverComponentId;
        }
        if (!isMouseDown && wasMouseDown) {
            wasPressOverImGui = false;
        }

        wasMouseDown = isMouseDown;
        

        CircuitGraph renderGraph;
        renderGraph.components = components;
        renderGraph.connections = connections;
        renderer.draw(renderGraph, camera, aspectRatio, static_cast<float>(glfwGetTime()));

        glDisable(GL_DEPTH_TEST); // Important: Disable depth test before ImGui draw

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300, 100), ImGuiCond_Always);
        ImGui::Begin("DEBUG TEST WINDOW", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
        ImGui::Text("If you see this, ImGui is working");
        ImGui::Text("Camera Yaw: %.1f", camera.getYaw());
        ImGui::Text("Camera Pitch: %.1f", camera.getPitch());
        ImGui::End();

        // Layer Control Window
        std::set<int> allLayers;
        for (const auto& c : components)
            allLayers.insert(c.layer);

        ImGui::SetNextWindowPos(ImVec2(10, 120), ImGuiCond_Once);
        ImGui::Begin("Layer Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Toggle Layers:");
        for (int layer : allLayers) {
            bool isVisible = visibleLayers.count(layer);
            std::string label = "Layer " + std::to_string(layer);
            if (ImGui::Checkbox(label.c_str(), &isVisible)) {
                if (isVisible) visibleLayers.insert(layer);
                else visibleLayers.erase(layer);
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(10, 250), ImGuiCond_Once);
        ImGui::Begin("Display Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Checkbox("Show Grid", &showGrid);
        ImGui::End();

        // For voltage display, we can use a simple ImGui window
        ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_Once);
        ImGui::Begin("Voltage Watcher", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text(" Select Component to watch:");
        if (ImGui::BeginCombo("##WatchCombo", watchedComponentId == -1 ? "None" : std::to_string(watchedComponentId).c_str())) { 
            if (ImGui::Selectable("None", watchedComponentId == -1)) {
                watchedComponentId = -1;
            }
            for (const auto& c : components) {
                bool selected = (c.id == watchedComponentId);
                if (ImGui::Selectable(std::to_string(c.id).c_str(), selected)) {
                    watchedComponentId = c.id;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        
        int selectedId = watchedComponentId;
        float currentV = 0.0f;
        if (!nodeVoltages.empty() &&
            selectedId >= 0 &&
            selectedId < (int)nodeVoltages.size())
            currentV = nodeVoltages[selectedId];

        ImGui::Text("Node %d: %.3f V", selectedId, currentV);

        if (voltageHistory.count(selectedId) &&
            !voltageHistory[selectedId].empty()) {
            // Copy deque to a temporary vector for PlotLines
            std::vector<float> plotData(
                voltageHistory[selectedId].begin(),
                voltageHistory[selectedId].end());
            ImGui::PlotLines("##vwatch",
                             plotData.data(),
                             (int)plotData.size(),
                             0, nullptr,
                             -6.0f, 6.0f,
                             ImVec2(0, 60));
        }
                    
        ImGui::End(); // End of Voltage Watcher window

        ImGui::SetNextWindowPos(ImVec2(330, 400), ImGuiCond_Once);
        ImGui::Begin("Transient Sim", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::CollapsingHeader("Transient Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("dt (s)", &transientDt,
                               1e-6f, 1e-3f, "%.2e");
            ImGui::SliderFloat("Total (s)", &transientTotal,
                               1e-3f, 0.1f, "%.3f");

            if (!components.empty()) {
                bool nodeExists = false;
                for (const auto& c : components) {
                    if (c.id == transientNode) {
                        nodeExists = true;
                        break;
                    }
                }
                if (!nodeExists) {
                    transientNode = components.front().id;
                }

                if (ImGui::BeginCombo("Node", std::to_string(transientNode).c_str())) {
                    for (const auto& c : components) {
                        bool selected = (transientNode == c.id);
                        if (ImGui::Selectable(std::to_string(c.id).c_str(), selected)) {
                            transientNode = c.id;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::InputInt("Node", &transientNode);
            }

            if (ImGui::Button("Run Transient")) {
                CircuitGraph transientGraph;
                transientGraph.components = components;
                transientGraph.connections = connections;
                transientResult = TransientSolver::solve(
                    transientGraph, activeGroundNodeId,
                    transientDt, transientTotal, transientNode);
            }

            if (!transientResult.empty()) {
                ImGui::PlotLines("##transient",
                                 transientResult.data(),
                                 (int)transientResult.size(),
                                 0, nullptr, -6.0f, 6.0f,
                                 ImVec2(0, 80));
            }
            ImGui::Text("Samples: %d", (int)transientResult.size());
        }
        ImGui::End();

        // Now we will do component type selection in the UI
        static int selectedType = 0;
        const char* componentTypes[] = { "Resistor", "Capacitor", "Inductor", "Diode" };
        static int newLayer = 1; // Default to layer 1  
        static float newX = 0.0f, newY = 0.0f, newz = 0.0f;


        ImGui::SetNextWindowPos(ImVec2(10, 600), ImGuiCond_Once);
        ImGui::Begin("Simulation Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Choose Ground Node:");

        if (ImGui::BeginCombo("##GroundSelector", std::to_string(groundComponentId).c_str())) {
            for (const auto& c : components) {
                bool selected = (groundComponentId == c.id);
                if (ImGui::Selectable(std::to_string(c.id).c_str(), selected)) {
                    groundComponentId = c.id;
                    simulationDirty = true;  // Ground changes redefine every node voltage reference.
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::End();


        ImGui::SetNextWindowPos(ImVec2(10, 300), ImGuiCond_Once);
        ImGui::Begin("Add Component", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("Select Component Type:");
        ImGui::Combo("##Type", &selectedType, componentTypes, IM_ARRAYSIZE(componentTypes));
        
        
        // Auto-connect options
        static int autoConnectMode = 0;
        const char* autoOptions[] = { "No Auto-Connect", "Connect to Nearest", "Connect to First", "Ask at Runtime" };
        ImGui::Combo("Auto-Connect Mode", &autoConnectMode, autoOptions, IM_ARRAYSIZE(autoOptions));

        
        
        
        ImGui::InputInt("Layer",&newLayer);
        ImGui::InputFloat("X",&newX);
        ImGui::InputFloat("Y",&newY);
        ImGui::InputFloat("Z",&newz);

        if (ImGui::Button("Add Component")) 
        {
            Component newComp;
            newComp.id = static_cast<int>(components.size()); // Simple ID assignment
            newComp.type = componentTypes[selectedType];
            newComp.layer = newLayer;
            newComp.x = newX;
            newComp.y = newY;
            newComp.z = newz;

            // Push owns the mutation, so undo can remove this exact component later.
            commandHistory.push(std::make_unique<AddComponentCommand>(graph, newComp));
            simulationDirty = true;  // New circuit topology needs one fresh MNA solve.

            // === Handle auto-connect logic ===
        if (autoConnectMode == 1 && !components.empty()) 
        
        {
            // Option 1: Connect to nearest component
            float minDist = std::numeric_limits<float>::max();
            int nearestID = -1;

            for (const auto& other : components) 
            {
                if (other.id == newComp.id) continue;
                glm::vec3 p1(newComp.x, newComp.y, newComp.z);
                glm::vec3 p2(other.x, other.y, other.z);
                float dist = glm::distance(p1, p2);
                if (dist < minDist) 
                {
                    minDist = dist;
                    nearestID = other.id;
                }
            }

            if (nearestID != -1) {
                Connection newConnection{ newComp.id, nearestID };
                // Store the edge as a command so Ctrl+Z removes only this auto-link.
                commandHistory.push(std::make_unique<ConnectCommand>(graph, newConnection));
                simulationDirty = true;  // A new edge changes the conductance matrix.
                pulseTrails[newComp.id * 1000 + nearestID].clear();
                std::cout << "Auto-connected to nearest: " << nearestID << std::endl;
            }
        } else if (autoConnectMode == 2 && !components.empty()) 
        
        {
            // Option 2: Connect to first
            if (newComp.id != 0) {
                Connection newConnection{ newComp.id, 0 };
                // Keep this auto-link undoable as its own user-visible action.
                commandHistory.push(std::make_unique<ConnectCommand>(graph, newConnection));
                simulationDirty = true;  // A new edge changes the conductance matrix.
                pulseTrails[newComp.id * 1000 + 0].clear();
                std::cout << "Auto-connected to first component (ID 0)" << std::endl;
            }
        } 
        else if (autoConnectMode == 3) 
        {
            showConnectionPopup = true;
            lastAddedComponentId = newComp.id;
            std::cout << "Popup-based connect: Awaiting user input..." << std::endl;
        }
        




            visibleLayers.insert(newLayer); // Automatically show the new layer

            std::cout << "Added component " << newComp.id << " (" << newComp.type
            << ") at [" << newComp.x << ", " << newComp.y << ", " << newComp.z
            << "] on layer " << newComp.layer << std::endl;
        }
        ImGui::End();

        // === POPUP: Connect Newly Added Component ===
        if (showConnectionPopup) {
            ImGui::OpenPopup("Auto-Connect Options");
        }

        if (ImGui::BeginPopupModal("Auto-Connect Options", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Choose Auto-Connect Strategy:");

            const char* strategies[] = { "Nearest", "First" };
            ImGui::Combo("Strategy", &popupStrategy, strategies, IM_ARRAYSIZE(strategies));

            const char* directions[] = { "New - >  Existing", "Existing - > New" };
            ImGui::Combo("Direction", &popupDirection, directions, IM_ARRAYSIZE(directions));

            if (ImGui::Button("Connect")) {
                int targetID = -1;

                if (popupStrategy == 0) {
                    // Find nearest
                    float minDist = std::numeric_limits<float>::max();
                    glm::vec3 pNew;
                    for (const auto& c : components) {
                        if (c.id == lastAddedComponentId) {
                            pNew = glm::vec3(c.x, c.y, c.z);
                            break;
                        }
                    }
                    for (const auto& other : components) {
                        if (other.id == lastAddedComponentId) continue;
                        glm::vec3 pOther(other.x, other.y, other.z);
                        float dist = glm::distance(pNew, pOther);
                        if (dist < minDist) {
                            minDist = dist;
                            targetID = other.id;
                        }
                    }
                } else {
                    // First component (ID 0)
                    if (lastAddedComponentId != 0) {
                        targetID = 0;
                    }
                }

                if (targetID != -1) {
                    int fromID = (popupDirection == 0) ? lastAddedComponentId : targetID;
                    int toID   = (popupDirection == 0) ? targetID : lastAddedComponentId;

                    Connection popupConnection{ fromID, toID };
                    // Popup-created links follow the same undo path as manual links.
                    commandHistory.push(std::make_unique<ConnectCommand>(graph, popupConnection));
                    simulationDirty = true;  // Popup connection changes circuit topology.
                    pulseTrails[fromID * 1000 + toID].clear();

                    std::cout << "Auto-connected via popup: " << fromID << " - > " << toID << std::endl;
                }

                showConnectionPopup = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                showConnectionPopup = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        



        // the above code adds a new component to the layout. 

        // === ImGui: Connect Components UI ===
            static int fromID = 0;
            static int toID = 0;

            ImGui::SetNextWindowPos(ImVec2(10, 450), ImGuiCond_Once);
            ImGui::Begin("Connect Components", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

            // Dropdowns for component IDs
            ImGui::Text("From Component ID:");
            if (ImGui::BeginCombo("##fromCombo", std::to_string(fromID).c_str())) {
                for (const auto& c : components) {
                    bool isSelected = (fromID == c.id);
                    if (ImGui::Selectable(std::to_string(c.id).c_str(), isSelected)) {
                        fromID = c.id;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Text("To Component ID:");

            ImGui::Separator();
            ImGui::Text("Toggle Signal Per Connection:");
            for (const auto& conn : connections) {
                int connKey = conn.from_id * 1000 + conn.to_id;
                std::string label = "Signal: " + std::to_string(conn.from_id) + " - > " + std::to_string(conn.to_id);
                bool enabled = signalEnabled[connKey];
                if (ImGui::Checkbox(label.c_str(), &enabled)) {
                    signalEnabled[connKey] = enabled;
                }
            }

            if(ImGui::Button("Toggle All Signals")) {
                for (auto& pair : signalEnabled) {
                    pair.second = true;  // Toggle all signals
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Disable All Signals")) {
                for (auto& pair : signalEnabled) {
                    pair.second = false;  // Disable all signals
                }
            }



            if (ImGui::BeginCombo("##toCombo", std::to_string(toID).c_str())) {
                for (const auto& c : components) {
                    bool isSelected = (toID == c.id);
                    if (ImGui::Selectable(std::to_string(c.id).c_str(), isSelected)) {
                        toID = c.id;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Add connection button
            if (ImGui::Button("Add Connection")) {
                if (fromID != toID) {
                    Connection newConn{ fromID, toID };
                    // Manual edges become commands so undo removes the latest edge.
                    commandHistory.push(std::make_unique<ConnectCommand>(graph, newConn));
                    simulationDirty = true;  // Manual connection changes circuit topology.

                    // Ensure trail is initialized (optional)
                    int connKey = fromID * 1000 + toID;
                    pulseTrails[connKey].clear();

                    std::cout << "Connected Component " << fromID << " to " << toID << std::endl;
                } else {
                    std::cout << "Invalid: Cannot connect a component to itself." << std::endl;
                }
            }

            // Saving the Layout 

            if (ImGui :: Button ("Save Layout to Json"))
            {
                CircuitGraph graphToSave;
                graphToSave.components = components;
                graphToSave.connections = connections;
                LayoutSerializer::save(graphToSave, "output_layout.json");
                std::cout << "Layout saved to output_layout.json" << std::endl;
            }

            // Loading the Layout
            if(ImGui::Button("Load Layout"))
            {
                CircuitGraph loadedGraph = LayoutSerializer::load("output_layout.json");

                // Layout load clears history because full-graph undo is out of scope.
                commandHistory.clear();

                // Clear existing components and connections
                components.clear();
                connections.clear();
                pulseTrails.clear();
                signalEnabled.clear();
                visibleLayers.clear();

                components = loadedGraph.components;
                connections = loadedGraph.connections;
                simulationDirty = true;  // Loaded files replace the circuit being solved.

                for(const auto& c: components)
                {
                    visibleLayers.insert(c.layer); // Show the layer of the loaded component
                }

                for(const auto& conn: connections)
                {
                    int key = conn.from_id * 1000 + conn.to_id;
                    signalEnabled[key] = true; // Enable signal by default

                }
                std::cout << "Layout loaded from output_layout.json\n";
            }




            ImGui::End();



        bool show_demo = true;
        ImGui::ShowDemoWindow(&show_demo);
        
        // All your ImGui windows...

        // === Tooltip for Hovered Component ===
        if (hoverComponentId != -1) {
            const Component* hovered = nullptr;
            for (const auto& c : components) {
                if (c.id == hoverComponentId) {
                    hovered = &c;
                    break;
                }
            }

            if (hovered) {
                ImGui::SetNextWindowBgAlpha(0.8f);
                ImGui::BeginTooltip();
                ImGui::Text("Component ID: %d", hovered->id);
                ImGui::Text("Type: %s", hovered->type.c_str());
                ImGui::Text("Layer: %d", hovered->layer);
                ImGui::Text("Position: [%.1f, %.1f, %.1f]", hovered->x, hovered->y, hovered->z);
                
                float voltage = componentVoltages.count(hovered->id) ? componentVoltages[hovered->id] : 0.0f;
                ImGui::Text("Voltage: %.2f V", voltage);

                ImGui::Text("Resistance: %.2f ohms", hovered->resistance);


                
                ImGui::EndTooltip();


                
            }
        }


        if (selectedComponentId != -1) {
            Component* selected = nullptr;
            for (auto& c : components) {
                if (c.id == selectedComponentId) {
                    selected = &c;
                    break;
                }
            }
        
            if (selected) {
                ImGui::Begin("Edit Component");


                
                const char* compTypes[] = { "Resistor", "Capacitor", "Inductor", "Diode", "Battery" };
                int currentTypeIndex = 0;
                for (int i = 0; i < 5; ++i) {
                    if (selected->type == compTypes[i]) {
                        currentTypeIndex = i;
                        break;
                    }
                }
                if (ImGui::Combo("Type", &currentTypeIndex, compTypes, IM_ARRAYSIZE(compTypes))) {
                    const std::string oldType = selected->type;
                    const float oldResistance = selected->resistance;
                    const float oldCapacitance = selected->capacitance;
                    const float oldInductance = selected->inductance;
                    const float oldVoltageSource = selected->voltageSource;

                    // Start from neutral defaults, then override only what each type needs.
                    const std::string newType = compTypes[currentTypeIndex];
                    float newResistance = DEFAULT_RESISTANCE;
                    float newCapacitance = DEFAULT_CAPACITANCE;
                    float newInductance = DEFAULT_INDUCTANCE;
                    float newVoltageSource = DEFAULT_VOLTAGE_SOURCE;

                    // These branches preserve the existing Elec3D type defaults by name.
                    if (newType == "Capacitor") {
                        newResistance = REACTIVE_DEFAULT_RESISTANCE;
                        newCapacitance = CAPACITOR_DEFAULT_CAPACITANCE;
                    } else if (newType == "Inductor") {
                        newResistance = REACTIVE_DEFAULT_RESISTANCE;
                        newInductance = INDUCTOR_DEFAULT_INDUCTANCE;
                    } else if (newType == "Diode") {
                        newResistance = DIODE_DEFAULT_RESISTANCE;
                    } else if (newType == "Battery") {
                        newResistance = BATTERY_DEFAULT_RESISTANCE;
                        newVoltageSource = BATTERY_VOLTAGE_SOURCE;
                    }

                    // One command owns both the type string and its bundled defaults.
                    commandHistory.push(std::make_unique<ChangeTypeCommand>(
                        graph, selected->id,
                        oldType, oldResistance, oldCapacitance, oldInductance, oldVoltageSource,
                        newType, newResistance, newCapacitance, newInductance, newVoltageSource));
                    simulationDirty = true;  // Component type changes which electrical model MNA sees.
                }

                auto editFloatProperty = [&](const char* label, const char* propertyName, float& value) {
                    static std::unordered_map<std::string, float> editStartValues;
                    const std::string editKey = std::to_string(selected->id) + ":" + propertyName;

                    // ImGui edits the float live, so capture the old value before typing starts.
                    ImGui::InputFloat(label, &value);
                    if (ImGui::IsItemActivated()) {
                        editStartValues[editKey] = value;
                    }

                    // When editing ends, store one clean undo command instead of one per keystroke.
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        const float oldValue = editStartValues.count(editKey) ? editStartValues[editKey] : value;
                        commandHistory.push(std::make_unique<EditPropertyCommand>(
                            graph, selected->id, propertyName, oldValue, value));
                        simulationDirty = true;  // Electrical values changed, so cached MNA output is stale.
                    }
                };

                if (selected->type == "Battery") {
                    editFloatProperty("Resistance (Ohms)", "resistance", selected->resistance);
                    editFloatProperty("Voltage Source (V)", "voltageSource", selected->voltageSource);
                } else if (selected->type == "Resistor") {
                    editFloatProperty("Resistance (Ohms)", "resistance", selected->resistance);
                } else if (selected->type == "Capacitor") {
                    editFloatProperty("Capacitance (F)", "capacitance", selected->capacitance);
                    editFloatProperty("Resistance (Ohms)", "resistance", selected->resistance);
                } else if (selected->type == "Inductor") {
                    editFloatProperty("Inductance (H)", "inductance", selected->inductance);
                    editFloatProperty("Resistance (Ohms)", "resistance", selected->resistance);
                } else if (selected->type == "Diode") {
                    editFloatProperty("Resistance (Ohms)", "resistance", selected->resistance);
                }

                // === Editable Voltage for Battery ===
                if (selected->type == "Battery") {
                    editFloatProperty("Voltage (V)", "voltage", selected->voltage);
                }
        
                ImGui::Text("Editing ID: %d", selected->id);
                
                static std::unordered_map<int, glm::vec3> moveStartPositions;
                const glm::vec3 currentPosition(selected->x, selected->y, selected->z);

                // Capture the full position before any one axis starts changing.
                ImGui::InputFloat("X", &selected->x);
                if (ImGui::IsItemActivated()) {
                    moveStartPositions[selected->id] = currentPosition;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    const glm::vec3 oldPosition = moveStartPositions.count(selected->id)
                        ? moveStartPositions[selected->id] : currentPosition;
                    const glm::vec3 newPosition(selected->x, selected->y, selected->z);
                    commandHistory.push(std::make_unique<MoveComponentCommand>(
                        graph, selected->id, oldPosition, newPosition));
                    simulationDirty = true;  // Moving components changes wire lengths and solver topology layout.
                }

                // Y uses the same command path so each completed axis edit is undoable.
                ImGui::InputFloat("Y", &selected->y);
                if (ImGui::IsItemActivated()) {
                    moveStartPositions[selected->id] = currentPosition;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    const glm::vec3 oldPosition = moveStartPositions.count(selected->id)
                        ? moveStartPositions[selected->id] : currentPosition;
                    const glm::vec3 newPosition(selected->x, selected->y, selected->z);
                    commandHistory.push(std::make_unique<MoveComponentCommand>(
                        graph, selected->id, oldPosition, newPosition));
                    simulationDirty = true;  // Cached render/sim state must observe the new position.
                }

                // Z completes the position editor without introducing a separate command class.
                ImGui::InputFloat("Z", &selected->z);
                if (ImGui::IsItemActivated()) {
                    moveStartPositions[selected->id] = currentPosition;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    const glm::vec3 oldPosition = moveStartPositions.count(selected->id)
                        ? moveStartPositions[selected->id] : currentPosition;
                    const glm::vec3 newPosition(selected->x, selected->y, selected->z);
                    commandHistory.push(std::make_unique<MoveComponentCommand>(
                        graph, selected->id, oldPosition, newPosition));
                    simulationDirty = true;  // Wire paths depend on component position.
                }

                static std::unordered_map<int, int> layerStartValues;

                // Capture the old layer before ImGui mutates the integer live.
                ImGui::InputInt("Layer", &selected->layer);
                if (ImGui::IsItemActivated()) {
                    layerStartValues[selected->id] = selected->layer;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    const int oldLayer = layerStartValues.count(selected->id)
                        ? layerStartValues[selected->id] : selected->layer;
                    const int newLayerValue = selected->layer;

                    // Layer is an int field, so it has its own narrow command.
                    commandHistory.push(std::make_unique<ChangeLayerCommand>(
                        graph, selected->id, oldLayer, newLayerValue));
                    visibleLayers.insert(newLayerValue);
                    simulationDirty = true;  // Rendering and graph checks depend on layer visibility.
                }
        
                if (ImGui::Button("Close")) {
                    selectedComponentId = -1;  // Deselect
                }
        
                ImGui::End();
            }
        }

// === Circuit Validity Panel ===


if (!disconnectedNow.empty() || !hasCycle) {
    ImGui::SetNextWindowPos(ImVec2(10, 700), ImGuiCond_Once);
    ImGui::Begin("⚠️ Circuit Warning", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    if (!disconnectedNow.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Disconnected Components:");
        for (int id : disconnectedNow) {
            ImGui::BulletText("Component %d", id);
        }
    }

    if (!hasCycle) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "⚠️ No current loop! Add a cycle.");
    }

    ImGui::End();
}


ImGui::Render();
ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        glEnable(GL_DEPTH_TEST); // Re-enable depth test for next frame
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    

    // Clean up and exit
    glfwDestroyWindow(window);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
    return 0;
    

}
