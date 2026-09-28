#include "scene.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    try {
        const Scene scene = Scene::create();
        const std::vector<glm::vec3> start = scene.translations(0.0);
        const std::vector<glm::vec3> later = scene.translations(30.0);
        if (start.size() != 3 || start == later) {
            std::cerr << "orbit samples did not move\n";
            return EXIT_FAILURE;
        }
        for (const glm::vec3& position : start) {
            const double radius = std::hypot(std::hypot(position.x, position.y), position.z);
            if (std::abs(radius - 3.0) >= 0.05) {
                std::cerr << "radius " << radius << " is outside the orbit\n";
                return EXIT_FAILURE;
            }
        }
        for (const glm::vec3& position : later) {
            const double radius = std::hypot(std::hypot(position.x, position.y), position.z);
            if (std::abs(radius - 3.0) >= 0.05) {
                std::cerr << "radius " << radius << " is outside the orbit\n";
                return EXIT_FAILURE;
            }
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
