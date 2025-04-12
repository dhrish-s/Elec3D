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

#include <set>  // Required for layer toggle functionality

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

float radius = 10.0f;   // Distance from origin



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
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos - lastX);
    float yoffset = static_cast<float>(lastY - ypos); // reversed since y-coordinates go from bottom to top
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    float sensitivity = 0.1f; // change this value to your liking
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    // Make sure that when pitch is out of bounds, screen doesn't get flipped
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;
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
};

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
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);  // Lock cursor


    //Load OpenGL function pointers using glad
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

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
    for (const auto& item : layout["components"]) {
        Component c;
        c.id = item["id"];
        c.type = item["type"];
        c.x = item["position"][0];
        c.y = item["position"][1];
        c.z = item["position"][2];
        c.layer = item["layer"];

        components.push_back(c);
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


    unsigned int shaderProgram = LoadShaderProgram("../shaders/cube.vert", "../shaders/cube.frag");

    // Enable depth test once, before the loop (you already have this — keep it!)
    glEnable(GL_DEPTH_TEST);

    // Inside render loop:
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.12f, 0.12f, 0.15f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);

        int maxLayer = 0;
        for (const auto& c : components) {
            if (c.layer > maxLayer) maxLayer = c.layer;
        }

        float cameraZ = - (5.0f + maxLayer * 2.0f);  // dynamic distance based on number of components
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
        
            glm::vec3 color(1.0f);  // white color
            if (c.type == "Resistor")      color = glm::vec3(0.55f, 0.27f, 0.07f);
            else if (c.type == "Capacitor") color = glm::vec3(0.0f, 0.5f, 1.0f);
            else if (c.type == "Inductor")  color = glm::vec3(0.0f, 1.0f, 0.0f);
            else if (c.type == "Diode")     color = glm::vec3(1.0f, 0.0f, 0.0f);
            
            glUniform3fv(colorLoc, 1, glm::value_ptr(color));
            
            //  Animate rotation
            float angle = (float)glfwGetTime();
        
            //  Final model matrix (scale → rotate → translate)
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::scale(model, scale);
            model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::translate(model, pos);
        
            // Upload and draw
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        glBindVertexArray(0);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    

    // Clean up and exit
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
    

}
