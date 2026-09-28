#include "mesh.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

bool outward(const MeshData& mesh) {
    std::size_t cursor = 0;
    for (int count : mesh.faceVertexCounts) {
        const auto ids = &mesh.faceVertexIndices[cursor];
        cursor += static_cast<std::size_t>(count);
        const glm::vec3 a = mesh.points[static_cast<std::size_t>(ids[0])];
        const glm::vec3 b = mesh.points[static_cast<std::size_t>(ids[1])];
        const glm::vec3 c = mesh.points[static_cast<std::size_t>(ids[2])];
        const glm::vec3 edgeU = b - a;
        const glm::vec3 edgeV = c - a;
        const glm::vec3 normal = glm::cross(edgeU, edgeV);
        const float length = glm::dot(normal, normal);
        if (length < 1.0e-8f) {
            continue;
        }
        const glm::vec3 stored = mesh.normals[static_cast<std::size_t>(ids[0])];
        if (glm::dot(normal, stored) <= 0.0f) {
            return false;
        }
    }
    return true;
}

bool hasColor(const MeshData& mesh, int color) {
    for (int index : mesh.colorIndices) {
        if (index == color) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    const MeshData meshes[] = {
        checkerCube(8, 1.25f),
        checkerSphere(16, 24, 0.7f),
        checkerTorus(32, 16, 0.65f, 0.22f),
    };
    for (const MeshData& mesh : meshes) {
        if (!hasColor(mesh, 0) || !hasColor(mesh, 1) || mesh.colorIndices.size() != mesh.faceVertexCounts.size() ||
            !outward(mesh)) {
            std::cerr << "mesh winding or checkerboard check failed\n";
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
