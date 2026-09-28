#pragma once

#include "glm_setup.hpp"

#include <cstdint>
#include <vector>

struct MeshData {
    std::vector<glm::vec3> points;
    std::vector<glm::vec3> normals;
    std::vector<int> faceVertexCounts;
    std::vector<int> faceVertexIndices;
    /// Palette index per face: 0 is the dark square, 1 is the light square.
    std::vector<int> colorIndices;

    void pushQuad(int a, int b, int c, int d, int color);
};

inline constexpr glm::vec3 kPalette[] = {
    glm::vec3(0.08f, 0.08f, 0.08f),
    glm::vec3(0.93f, 0.93f, 0.93f),
};

MeshData checkerCube(unsigned cells, float size);
MeshData checkerSphere(unsigned stacks, unsigned slices, float radius);
MeshData checkerTorus(unsigned majorSegments, unsigned minorSegments, float majorRadius, float minorRadius);
