#include <glad/glad.h>
#include <GLFW/glfw3.h>
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

#include "circuit/Circuit.h"
#include "io/LayoutSerializer.h"
#include "renderer/Renderer.h"


std::set<int> visibleLayers = {1, 2};  // Initially visible layers



int screenWidth = 1280;
int screenHeight = 720;

// yaw is rotation around the y-axis, pitch is rotation around the x-axis
// These angles are used to control the camera direction
float yaw = -90.0f;     // horizontal angle (left-right)
float pitch = 0.0f;     // vertical angle (up-down)
float lastX = screenWidth / 2.0f;
float lastY = screenHeight / 2.0f;
float fov = 45.0f;
bool firstMouse = true;

bool isDragging = false;


float radius = 10.0f;   // Distance from origin
bool showGrid = true;  // Toggle visibility

//Now voltage
std::unordered_map<int, std::deque<float>> voltageHistory;
const int maxVoltageHistory = 200;  // Number of samples to keep per component



void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
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
    if (!isDragging) return;  // Only orbit when dragging

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos - lastX);
    float yoffset = static_cast<float>(lastY - ypos); // reversed y
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            isDragging = true;
            firstMouse = true;  // reset to prevent jump
        } else if (action == GLFW_RELEASE) {
            isDragging = false;
        }
    }
}


void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) 
    // Adjust the field of view (FOV) based on scroll input
{
    fov -= static_cast<float>(yoffset);
    if(fov<10.0f) fov = 10.0f;
    if(fov>90.0f) fov = 90.0f;
}

std::unordered_map<int, std::vector<PulseTrail>> pulseTrails;

std::unordered_map<int, float> componentVoltages;

std::unordered_map<int, bool> signalEnabled;  // connectionKey -> toggle state

int hoverComponentId = -1;  // ID of the component currently hovered over

int selectedComponentId = -1;  // ID of component currently selected (on click)


glm::vec3 ScreenPosToWorldRay(float mouseX, float mouseY, const glm::mat4& projection, const glm::mat4& view)
{
    float x = (2.0f * mouseX) / screenWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / screenHeight; // Invert Y coordinate
    float z = 1.0f; // Near plane

    glm::vec3 ray_nds = glm::vec3(x, y, z);
    glm::vec4 ray_clip = glm::vec4(ray_nds,1.0);

    glm::vec4 ray_eye = glm::inverse(projection) * ray_clip;
    ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0, 0.0); // Transform to eye space

    glm::vec3 ray_wor = glm::vec3(glm::inverse(view) * ray_eye);
    ray_wor = glm::normalize(ray_wor); // Normalize the ray direction

    return ray_wor;
}

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
    std::vector<Component> components = graph.components;
    std::vector<Connection> connections = graph.connections;

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

    static int groundComponentId = 3;

    // Inside render loop:
    while (!glfwWindowShouldClose(window)) {

        std::vector<int> disconnectedNow = FindDisconnectedComponents(components, connections);
        bool hasCycle = IsCircuitLooped(components, connections);
        // Only solve if circuit is connected AND has a loop
        if (disconnectedNow.empty() && hasCycle) {
            SolveKirchhoffVoltages(components, connections, groundComponentId);
        }
        


        if(watchedComponentId!=-1)
        {
            float voltage = componentVoltages.count(watchedComponentId) ? componentVoltages[watchedComponentId] : 0.0f;
            voltageHistory[watchedComponentId].push_back(voltage);

            if (voltageHistory[watchedComponentId].size() > maxVoltageHistory) {
                voltageHistory[watchedComponentId].pop_front();  // Remove oldest sample
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    
        glClearColor(0.12f, 0.12f, 0.15f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        //Camera and transformation setup
        // Set up camera position and orientation based on yaw and pitch angles
        int maxLayer = 0;
        for (const auto& c : components) {
            if (c.layer > maxLayer) maxLayer = c.layer;
        }

        float cameraZ = - (5.0f + maxLayer * 2.0f);  // dynamic distance based on number of components but we arent using this
        float camX = radius * cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        float camY = radius * sin(glm::radians(pitch));
        float camZ = radius * sin(glm::radians(yaw)) * cos(glm::radians(pitch));

        glm::vec3 cameraPos = glm::vec3(camX, camY, camZ);
        glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, cameraUp);

        glm::mat4 projection = glm::perspective(glm::radians(fov),
                                (float)screenWidth / (float)screenHeight,
                                0.1f, 100.0f);

        // Mouse Hover Detection
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        lastX = static_cast<float>(mouseX);
        lastY = static_cast<float>(mouseY);


        glm::vec3 rayDir = ScreenPosToWorldRay(static_cast<float>(mouseX), static_cast<float>(mouseY), projection, view);
        glm::vec3 rayOrigin = cameraPos;  // Camera position
        
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
        bool isMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        
        if (!isMouseDown && wasMouseDown) {
            selectedComponentId = hoverComponentId;
        }
        
        wasMouseDown = isMouseDown;
        

        CircuitGraph renderGraph;
        renderGraph.components = components;
        renderGraph.connections = connections;
        renderer.draw(renderGraph, view, projection);

        glDisable(GL_DEPTH_TEST); // Important: Disable depth test before ImGui draw

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300, 100), ImGuiCond_Always);
        ImGui::Begin("DEBUG TEST WINDOW", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
        ImGui::Text("If you see this, ImGui is working");
        ImGui::Text("Camera Yaw: %.1f", yaw);
        ImGui::Text("Camera Pitch: %.1f", pitch);
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
        
                    // Plot voltage graph
                    auto it = voltageHistory.find(watchedComponentId);
                    if (watchedComponentId != -1 && it != voltageHistory.end()) {
                        const std::deque<float>& history = it->second;
                    
                        static std::vector<float> historyVec;
                        historyVec.assign(history.begin(), history.end());
                    
                        ImGui::PlotLines("Voltage (V)", historyVec.data(), static_cast<int>(historyVec.size()), 0,
                                         nullptr, 0.0f, 5.0f, ImVec2(250, 100));
                    }
                    
        ImGui::End(); // End of Voltage Watcher window

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

            components.push_back(newComp);

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
                connections.push_back({ newComp.id, nearestID });
                pulseTrails[newComp.id * 1000 + nearestID].clear();
                std::cout << "Auto-connected to nearest: " << nearestID << std::endl;
            }
        } else if (autoConnectMode == 2 && !components.empty()) 
        
        {
            // Option 2: Connect to first
            if (newComp.id != 0) {
                connections.push_back({ newComp.id, 0 });
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

                    connections.push_back({ fromID, toID });
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
                    connections.push_back(newConn);

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

                // Clear existing components and connections
                components.clear();
                connections.clear();
                pulseTrails.clear();
                signalEnabled.clear();
                visibleLayers.clear();

                components = loadedGraph.components;
                connections = loadedGraph.connections;

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
                    selected->type = compTypes[currentTypeIndex];

                    // Auto-assign default resistance and voltage
                    if (selected->type == "Resistor")      selected->resistance = 1.0f;
                    else if (selected->type == "Capacitor") selected->resistance = 0.5f;
                    else if (selected->type == "Inductor")  selected->resistance = 0.8f;
                    else if (selected->type == "Diode")     selected->resistance = 2.0f;
                    else if (selected->type == "Battery") {
                        selected->resistance = 0.1f;
                        selected->voltage = 5.0f;  // Default battery value
                    }
                }

                // === Editable Resistance ===
                ImGui::InputFloat("Resistance (Ohms)", &selected->resistance);

                // === Editable Voltage for Battery ===
                if (selected->type == "Battery") {
                    ImGui::InputFloat("Voltage (V)", &selected->voltage);
                }
        
                ImGui::Text("Editing ID: %d", selected->id);
                
                ImGui::InputFloat("X", &selected->x);
                ImGui::InputFloat("Y", &selected->y);
                ImGui::InputFloat("Z", &selected->z);
                ImGui::InputInt("Layer", &selected->layer);
        
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
