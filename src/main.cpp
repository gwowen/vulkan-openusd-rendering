#include "scene.hpp"

#include <GLFW/glfw3.h>

#include "renderer.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        Scene scene = Scene::create();
        scene.exportUsda("scene.usda");

        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("failed to initialize GLFW");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        GLFWwindow* window = glfwCreateWindow(1280, 720, "OpenUSD Vulkan", nullptr, nullptr);
        if (window == nullptr) {
            glfwTerminate();
            throw std::runtime_error("failed to create the window");
        }

        {
            Renderer renderer(window, scene.objects());
            const auto started = std::chrono::steady_clock::now();
            while (glfwWindowShouldClose(window) == GLFW_FALSE) {
                glfwPollEvents();
                if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                    break;
                }
                const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                renderer.draw(scene.translations(elapsed / 8.0 * kOrbitSamples));
            }
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
