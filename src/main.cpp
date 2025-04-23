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



std::set<int> visibleLayers = {1, 2};  // Initially visible layers



using json = nlohmann::json;

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
unsigned int axisVAO, axisVBO;
unsigned int gridVAO, gridVBO;
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

struct Component {
    int id;
    std::string type;
    float x,y,z;
    int layer;

    float resistance = 1.0f;
    float voltage = 0.0f;
};

struct Connection {
    int from_id;
    int to_id;
};

struct PulseTrail {
    glm::vec3 pos;
    float age;
};



std::unordered_map<int, std::vector<PulseTrail>> pulseTrails;

std::unordered_map<int, float> componentVoltages;

std::unordered_map<int, bool> signalEnabled;  // connectionKey -> toggle state

int hoverComponentId = -1;  // ID of the component currently hovered over

int selectedComponentId = -1;  // ID of component currently selected (on click)



// Utility function to load, compile, and link shaders
unsigned int LoadShaderProgram(const char* vertexPath, const char* fragmentPath) 
{
    std::ifstream vFile(vertexPath);
    std::ifstream fFile(fragmentPath);
    std::stringstream vStream, fStream;

    if (!vFile || !fFile) {
        std::cerr << "Failed to load shader files." << std::endl;
        return 0;
    }

    vStream << vFile.rdbuf();
    fStream << fFile.rdbuf();

    std::string vertexCode = vStream.str();
    std::string fragmentCode = fStream.str();
    const char* vShaderCode = vertexCode.c_str();
    const char* fShaderCode = fragmentCode.c_str();

    // Compile vertex shader
    unsigned int vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, nullptr);
    glCompileShader(vertex);

    int success;
    char infoLog[512];
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertex, 512, nullptr, infoLog);
        std::cerr << "Vertex Shader Compilation Failed:\n" << infoLog << std::endl;
    }

    // Compile fragment shader
    unsigned int fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, nullptr);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragment, 512, nullptr, infoLog);
        std::cerr << "Fragment Shader Compilation Failed:\n" << infoLog << std::endl;
    }

    // Link shaders
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertex);
    glAttachShader(shaderProgram, fragment);
    glLinkProgram(shaderProgram);
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cerr << "Shader Linking Failed:\n" << infoLog << std::endl;
    }

    // Cleanup
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return shaderProgram;
}

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

    std::ifstream file("../src/layout.json");
    if(!file){
        std::cerr << "Failed to open layout.json" << std::endl;
        return -1;
    }

    json layout;
    file >> layout;

    std::vector<Component> components;

    std::vector<Connection> connections;

    for (const auto& item : layout["components"]) {
        Component c;
        c.id = item["id"];
        c.type = item["type"];

        // Assign resistance and voltage based on type
        if (c.type == "Resistor") c.resistance = 1.0f;
        else if (c.type == "Capacitor") c.resistance = 0.5f;
        else if (c.type == "Inductor") c.resistance = 0.8f;
        else if (c.type == "Diode") c.resistance = 2.0f;
        else if (c.type == "Battery") 
        {
        c.resistance = 0.1f;   // Internal resistance
        c.voltage = 5.0f;      // Provide 5V
        }

        c.x = item["position"][0];
        c.y = item["position"][1];
        c.z = item["position"][2];
        c.layer = item["layer"];

        components.push_back(c);
    }

    for(const auto& pair : layout["connections"]){
        Connection conn;
        conn.from_id = pair["from"];
        conn.to_id = pair["to"];
        connections.push_back(conn);

        int key = conn.from_id * 1000 + conn.to_id;
        signalEnabled[key] = true;  // default to ON
    }

    for (const auto& c : components) {
        std::cout << "Component " << c.id << " (" << c.type << ") at ["<< c.x << ", " << c.y << ", " << c.z << "] on layer " << c.layer << std::endl;
    }


    //cube vertices
    float cubeVertices[]={
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
        0, 1, 2, 2, 3, 0,  // back face
        4, 5, 6, 6, 7, 4,  // front face
        0, 1, 5, 5, 4, 0,  // bottom face
        2, 3, 7, 7, 6, 2,  // top face
        1, 2, 6, 6, 5, 1,  // right face
        0, 3, 7, 7, 4, 0   // left face
    };

    // Below is the VAO, VBO, and EBO setup.

    //VAO is a Vertex Array Object, VBO is a Vertex Buffer Object, and EBO is an Element Buffer Object
    unsigned int VAO, VBO, EBO;
    //glGenvertexArrays generates a vertex array object, glGenBuffers generates a buffer object, and glBindBuffer binds the buffer object to the specified target.
    //glBufferData creates and initializes a buffer object's data store.
    //glVertexAttribPointer specifies the location and data format of the array of generic vertex attributes at index 0. glEnableVertexAttribArray enables the vertex attribute array at index 0.
    //glBindVertexArray binds the vertex array object.
    //glBindBuffer binds the buffer object to the specified target.
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    // bind the Vertex Array Object first, then bind and set vertex buffer(s) and attribute pointer(s).
    glBindVertexArray(VAO);

    // bind the vertex buffer object
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    // index buffer object
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);

    //position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    //unbind the VBO and EBO (not the VAO) to avoid accidental modification
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    // The cube VAO setup is done. You can now use it to draw the cube.

    //Line VAO/VBO setup
    unsigned int lineVAO, lineVBO;
    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineVBO);

    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glBufferData(GL_ARRAY_BUFFER,sizeof(float)*6,nullptr,GL_DYNAMIC_DRAW); // 6 floats for 2 points (3 floats each)
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    // The line VAO setup is done. You can now use it to draw lines.

    // Pulse VAO/VBO setup
    unsigned int pulseVAO, pulseVBO;
    glGenVertexArrays(1, &pulseVAO);
    glGenBuffers(1, &pulseVBO);

    glBindVertexArray(pulseVAO);
    glBindBuffer(GL_ARRAY_BUFFER, pulseVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 3, nullptr, GL_DYNAMIC_DRAW); // Single point

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);


    glGenVertexArrays(1, &axisVAO);
    glGenBuffers(1, &axisVBO);

    float axisVertices[] = {
        // X axis (red)
        0.0f, 0.0f, 0.0f,   3.0f, 0.0f, 0.0f,
        // Y axis (green)
        0.0f, 0.0f, 0.0f,   0.0f, 3.0f, 0.0f,
        // Z axis (blue)
        0.0f, 0.0f, 0.0f,   0.0f, 0.0f, 3.0f
    };

    glBindVertexArray(axisVAO);
    glBindBuffer(GL_ARRAY_BUFFER, axisVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axisVertices), axisVertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);
    // The axis VAO setup is done. You can now use it to draw the axes.


    // Generating grid lines and settng up VAO/VBO
    std ::vector<float> gridVertices;
    const int gridSize = 10; // Size of the grid (10x10) and grid extends from -10 to 10 in both x and z directions

    for (int i = -gridSize; i <= gridSize; ++i) {
        // Lines parallel to Z (constant X)
        gridVertices.push_back((float)i); gridVertices.push_back(0.0f); gridVertices.push_back((float)-gridSize);
        gridVertices.push_back((float)i); gridVertices.push_back(0.0f); gridVertices.push_back((float)gridSize);
    
        // Lines parallel to X (constant Z)
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






    unsigned int shaderProgram = LoadShaderProgram("../shaders/cube.vert", "../shaders/cube.frag");

    // Enable depth test once, before the loop (you already have this — keep it!)
    glEnable(GL_DEPTH_TEST);

    // === Auto-connect popup state ===
    static bool showConnectionPopup = false;
    int watchedComponentId = -1;  // -1 = none selected
    // this watchedComponentId is used to determine which component to connect to when the popup is shown
    static int lastAddedComponentId = -1;
    static int popupDirection = 0;  // 0 = new -> existing, 1 = existing -> new
    static int popupStrategy = 0;   // 0 = nearest, 1 = first


    // Inside render loop:
    while (!glfwWindowShouldClose(window)) {

        SimulateVoltages(components, connections);

        if(watchedComponentId!=-1)
        {
            float voltage = componentVoltages.count(watchedComponentId) ? componentVoltages[watchedComponentId] : 0.0f;
            voltageHistory[watchedComponentId].push_back(voltage);

            if (voltageHistory[watchedComponentId].size() > maxVoltageHistory) {
                voltageHistory[watchedComponentId].pop_front();  // Remove oldest sample
            }
        }

        const float maxVoltage = 5.0f;  // Use the max voltage of your circuit



        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    
        glClearColor(0.12f, 0.12f, 0.15f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // -- drawing 3d scene --
        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);

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
        

        int modelLoc = glGetUniformLocation(shaderProgram, "model");
        int viewLoc = glGetUniformLocation(shaderProgram, "view");
        int projLoc = glGetUniformLocation(shaderProgram, "projection");
        int colorLoc = glGetUniformLocation(shaderProgram, "objectColor");

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

        for (const auto& c : components) {
            
            if (visibleLayers.count(c.layer) == 0) continue;
            glm::vec3 pos(c.x, c.y + c.layer * 1.0f, c.z);
        
            // Type-based scaling
            glm::vec3 scale(1.0f);
            if (c.type == "Resistor")    scale = glm::vec3(1.0f, 0.5f, 0.5f);
            else if (c.type == "Capacitor") scale = glm::vec3(0.7f, 1.0f, 0.7f);
            else if (c.type == "Inductor")  scale = glm::vec3(1.2f, 1.2f, 1.2f);
            else if (c.type == "Diode")     scale = glm::vec3(0.5f, 0.5f, 1.5f);
        
            
            // Voltage → Color mapping (blue = 0V, red = 5V)
            float voltage = componentVoltages.count(c.id) ? componentVoltages[c.id] : 0.0f;
            float normV = glm::clamp(voltage / maxVoltage, 0.0f, 1.0f);
            glm::vec3 color = glm::mix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), normV);


            if(c.id == hoverComponentId) {
                color = glm::vec3(1.0f, 1.0f, 0.0f); // Highlight color (yellow)
            }
            glUniform3fv(colorLoc, 1, glm::value_ptr(color));

            float pulse = 0.5f + 0.5f * sin(glfwGetTime() * 3.0f);
            glUniform1f(glGetUniformLocation(shaderProgram, "brightness"), pulse);

            
            //  Animate rotation
            float angle = (float)glfwGetTime();
        
            //  Final model matrix (scale → rotate → translate)
            glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
            model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, scale);

            // Upload and draw
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        // ... [camera/view/model loops and glDrawElements]
        glBindVertexArray(0);


        // Draw axes (X, Y, Z) in red, green, and blue respectively
        // -- AXIS LINES --
        glBindVertexArray(axisVAO);
        glm::mat4 axisModel = glm::mat4(1.0f);
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(axisModel));

        // X axis - red
        glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(1.0f, 0.0f, 0.0f)));
        glDrawArrays(GL_LINES, 0, 2);

        // Y axis - green
        glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(0.0f, 1.0f, 0.0f)));
        glDrawArrays(GL_LINES, 2, 2);

        // Z axis - blue
        glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f, 1.0f)));
        glDrawArrays(GL_LINES, 4, 2);

        if (showGrid) {
            glBindVertexArray(gridVAO);
            glm::mat4 gridModel = glm::mat4(1.0f);
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(gridModel));
            glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(0.4f))); // subtle gray
            glDrawArrays(GL_LINES, 0, (gridSize * 4 + 4));  // 4 verts per line pair
            glBindVertexArray(0);
        }
        

        glBindVertexArray(0);

        

        // the above code draws the cubes. Now we will draw the lines between them.

        glBindVertexArray(lineVAO);
        for(const auto& conn : connections)
        {
            // 1. Lookup components
            // 2. Skip invisible layers
            // 3. Compute fromPos / toPos
            // 4. Draw gray connection line
            // 5. Compute pulsePos using sin(...)
            // 6. Update trail for pulseTrails[connKey]
            // 7. === DRAW updated trail for this connection === 
            const Component* from = nullptr;
            const Component* to = nullptr;

            for(const auto& c : components)
            {
                if(c.id == conn.from_id) from = &c;
                if(c.id == conn.to_id) to = &c;
            }
            
            if (!from || !to) continue;

            bool fromVisible = visibleLayers.count(from->layer);
            bool toVisible = visibleLayers.count(to->layer);

            if (!fromVisible || !toVisible) {
                int connKey = from->id * 1000 + to->id;
                pulseTrails[connKey].clear(); // Prevent ghost trails
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

            glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(0.8f))); // light gray
            glDrawArrays(GL_LINES, 0, 2);
            glBindVertexArray(0);


            // === TRAIL DRAWING ===
            int connKey = from->id * 1000 + to->id;
            if (!signalEnabled[connKey]) continue;  // Skip if disabled

            auto& trail = pulseTrails[connKey];

            // === PULSE SIGNAL === (Now using separate VAO/VBO)
            // Compute position based on sin (bidirectional motion)
            float t = 0.5f * (1.0f + sin(glfwGetTime() * 1.0f));
            glm::vec3 pulsePos = (1.0f - t) * fromPos + t * toPos;

            // UPdate trail
            trail.push_back({ pulsePos, 0.0f });

            for (auto& pt : trail)
                pt.age += 0.02f;
            
            trail.erase(std::remove_if(trail.begin(), trail.end(),
                [](const PulseTrail& p) { return p.age > 1.0f; }), trail.end());

                
            
            glBindVertexArray(pulseVAO);
            glDisable(GL_DEPTH_TEST);
            // Step 1: Prepare data
            std::vector<glm::vec3> pulsePoints;
            std::vector<float> pulseBrightness;

            for (const auto& pt : trail) {
                pulsePoints.push_back(pt.pos);
                pulseBrightness.push_back(1.0f - pt.age);  // brightness for each point
            }

            // Step 2: Upload all positions
            glBindVertexArray(pulseVAO);
            glBindBuffer(GL_ARRAY_BUFFER, pulseVBO);
            glBufferData(GL_ARRAY_BUFFER, pulsePoints.size() * sizeof(glm::vec3), pulsePoints.data(), GL_DYNAMIC_DRAW);

            // Set up vertex attrib pointer (position)
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);

            // Step 3: Draw loop with brightness per vertex (simple version for now)
            for (size_t i = 0; i < pulsePoints.size(); ++i) {
                glm::mat4 model = glm::mat4(1.0f);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
                glUniform1f(glGetUniformLocation(shaderProgram, "brightness"), pulseBrightness[i]);
                glUniform3fv(colorLoc, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.0f)));  // yellow glow
                glPointSize(10.0f);
                glDrawArrays(GL_POINTS, i, 1);
            }

            glEnable(GL_DEPTH_TEST);





        }

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
            if (watchedComponentId != -1 && voltageHistory.count(watchedComponentId)) {
                const std::deque<float>& history = voltageHistory[watchedComponentId];

                // ImGui expects float*, so we convert
                static std::vector<float> historyVec;
                historyVec.assign(history.begin(), history.end());

                ImGui::PlotLines("Voltage (V)", historyVec.data(), static_cast<int>(historyVec.size()), 0,
                                nullptr, 0.0f, 5.0f, ImVec2(250, 100));
            }

        ImGui::End();

        // Now we will do component type selection in the UI
        static int selectedType = 0;
        const char* componentTypes[] = { "Resistor", "Capacitor", "Inductor", "Diode" };
        static int newLayer = 1; // Default to layer 1  
        static float newX = 0.0f, newY = 0.0f, newz = 0.0f;

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
                json output;

                // 1. Serialize components
                for (const auto& c : components) {
                    json compJson;
                    compJson["id"] = c.id;
                    compJson["type"] = c.type;
                    compJson["layer"] = c.layer;
                    compJson["position"] = { c.x, c.y, c.z };
                    output["components"].push_back(compJson);
                }

                // 2. Serialize connections
                for (const auto& conn : connections) {
                    json connJson;
                    connJson["from"] = conn.from_id;
                    connJson["to"] = conn.to_id;
                    output["connections"].push_back(connJson);
                }

                // 3. Write to the file
                std::ofstream outFile("outpuy_layout.json");
                outFile<<output.dump(4); // Pretty print with 4 spaces

                outFile.close();
                std::cout << "Layout saved to output_layout.json" << std::endl;
            }

            // Loading the Layout
            if(ImGui::Button("Load Layout"))
            {
                std::ifstream inFile("output_layout.json");
                if(!inFile)
                {
                    std::cerr << "Error opening file for reading \n";
                }
                else
                {
                    json loadedLayout;
                    inFile >> loadedLayout;

                    // Clear existing components and connections
                    components.clear();
                    connections.clear();
                    pulseTrails.clear();
                    signalEnabled.clear();
                    visibleLayers.clear();

                    for(const auto& item: loadedLayout["components"])
                    {
                        Component c;
                        c.id = item["id"];
                        c.type = item["type"];
                        c.x = item["position"][0];
                        c.y = item["position"][1];
                        c.z = item["position"][2];

                        c.layer = item["layer"];
                        components.push_back(c);
                        visibleLayers.insert(c.layer); // Show the layer of the loaded component

                    }

                    for(const auto& pair: loadedLayout["connections"])
                    {
                        Connection conn;
                        conn.from_id = pair["from"];
                        conn.to_id = pair["to"];
                        connections.push_back(conn);
                        int key = conn.from_id * 1000 + conn.to_id;
                        signalEnabled[key] = true; // Enable signal by default

                    }
                    std::cout << "Layout loaded from output_layout.json\n";
                }
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
        
                ImGui::Text("Editing ID: %d", selected->id);
                ImGui::InputText("Type", &selected->type[0], selected->type.size() + 1);
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
