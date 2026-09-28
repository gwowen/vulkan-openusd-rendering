#include "mesh.hpp"

#include <cmath>
#include <cstddef>

namespace {

struct Face {
    glm::vec3 origin;
    glm::vec3 axisU;
    glm::vec3 axisV;
    glm::vec3 normal;
};

void addGrid(MeshData& mesh, const Face& face, unsigned cells) {
    const int base = static_cast<int>(mesh.points.size());
    const int stride = static_cast<int>(cells) + 1;
    for (unsigned j = 0; j <= cells; ++j) {
        const float v = static_cast<float>(j) / static_cast<float>(cells);
        for (unsigned i = 0; i <= cells; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(cells);
            mesh.points.push_back(face.origin + face.axisU * u + face.axisV * v);
            mesh.normals.push_back(face.normal);
        }
    }
    for (unsigned j = 0; j < cells; ++j) {
        for (unsigned i = 0; i < cells; ++i) {
            const int i0 = base + static_cast<int>(j) * stride + static_cast<int>(i);
            mesh.pushQuad(i0, i0 + 1, i0 + stride + 1, i0 + stride, static_cast<int>((i + j) % 2));
        }
    }
}

glm::vec3 normalizeOrUp(glm::vec3 value) {
    const float length = glm::length(value);
    if (length == 0.0f) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return value / length;
}

constexpr float kTau = 6.28318530717958647692f;
constexpr float kPi = 3.14159265358979323846f;

}  // namespace

void MeshData::pushQuad(int a, int b, int c, int d, int color) {
    faceVertexCounts.push_back(4);
    faceVertexIndices.insert(faceVertexIndices.end(), {a, b, c, d});
    colorIndices.push_back(color);
}

MeshData checkerCube(unsigned cells, float size) {
    MeshData mesh;
    const float half = size * 0.5f;
    const Face faces[] = {
        {{-half, -half, half}, {size, 0.0f, 0.0f}, {0.0f, size, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{-half, half, -half}, {size, 0.0f, 0.0f}, {0.0f, -size, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{half, -half, half}, {0.0f, 0.0f, -size}, {0.0f, size, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{-half, -half, -half}, {0.0f, 0.0f, size}, {0.0f, size, 0.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-half, half, half}, {size, 0.0f, 0.0f}, {0.0f, 0.0f, -size}, {0.0f, 1.0f, 0.0f}},
        {{-half, -half, -half}, {size, 0.0f, 0.0f}, {0.0f, 0.0f, size}, {0.0f, -1.0f, 0.0f}},
    };
    for (const Face& face : faces) {
        addGrid(mesh, face, cells);
    }
    return mesh;
}

MeshData checkerSphere(unsigned stacks, unsigned slices, float radius) {
    MeshData mesh;
    for (unsigned stack = 0; stack <= stacks; ++stack) {
        const float phi = static_cast<float>(stack) / static_cast<float>(stacks) * kPi;
        for (unsigned slice = 0; slice < slices; ++slice) {
            const float theta = static_cast<float>(slice) / static_cast<float>(slices) * kTau;
            const glm::vec3 point(radius * std::sin(phi) * std::cos(theta), radius * std::cos(phi),
                                  radius * std::sin(phi) * std::sin(theta));
            mesh.normals.push_back(normalizeOrUp(point));
            mesh.points.push_back(point);
        }
    }
    for (unsigned stack = 0; stack < stacks; ++stack) {
        for (unsigned slice = 0; slice < slices; ++slice) {
            const unsigned next = (slice + 1) % slices;
            const int i0 = static_cast<int>(stack * slices + slice);
            const int i1 = static_cast<int>(stack * slices + next);
            const int i2 = static_cast<int>((stack + 1) * slices + next);
            const int i3 = static_cast<int>((stack + 1) * slices + slice);
            mesh.pushQuad(i0, i1, i2, i3, static_cast<int>((stack + slice) % 2));
        }
    }
    return mesh;
}

MeshData checkerTorus(unsigned majorSegments, unsigned minorSegments, float majorRadius, float minorRadius) {
    MeshData mesh;
    for (unsigned major = 0; major < majorSegments; ++major) {
        const float theta = static_cast<float>(major) / static_cast<float>(majorSegments) * kTau;
        const float ct = std::cos(theta);
        const float st = std::sin(theta);
        for (unsigned minor = 0; minor < minorSegments; ++minor) {
            const float phi = static_cast<float>(minor) / static_cast<float>(minorSegments) * kTau;
            const float cp = std::cos(phi);
            const float sp = std::sin(phi);
            const float ring = majorRadius + minorRadius * cp;
            mesh.points.push_back(glm::vec3(ring * ct, minorRadius * sp, ring * st));
            mesh.normals.push_back(glm::vec3(cp * ct, sp, cp * st));
        }
    }
    for (unsigned major = 0; major < majorSegments; ++major) {
        const unsigned majorNext = (major + 1) % majorSegments;
        for (unsigned minor = 0; minor < minorSegments; ++minor) {
            const unsigned minorNext = (minor + 1) % minorSegments;
            const int i0 = static_cast<int>(major * minorSegments + minor);
            const int i1 = static_cast<int>(major * minorSegments + minorNext);
            const int i2 = static_cast<int>(majorNext * minorSegments + minorNext);
            const int i3 = static_cast<int>(majorNext * minorSegments + minor);
            mesh.pushQuad(i0, i1, i2, i3, static_cast<int>((major + minor) % 2));
        }
    }
    return mesh;
}
