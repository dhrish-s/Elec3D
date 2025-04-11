#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>

using json = nlohmann::json;

int screenWidth = 1280;
int screenHeight = 720;


void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

struct Component {
    int id;
    std::string type;
    float x,y,z;
    int layer;
};

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


    //rendering loop
    while (!glfwWindowShouldClose(window)) {

        glClearColor(0.12f, 0.12f, 0.15f, 1.0f); 
        //dark background color


        // Clear the color buffer
        glClear(GL_COLOR_BUFFER_BIT);

        // Swap front and back buffers
        glfwSwapBuffers(window);

        // Poll for and process events
        glfwPollEvents();
    }


    // Clean up and exit
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
    

}
