#pragma once

#include "glm_setup.hpp"
#include "mesh.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

inline constexpr double kOrbitSamples = 120.0;

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

static_assert(sizeof(Vertex) == 36, "Vertex must match the Rust layout");
static_assert(offsetof(Vertex, normal) == 12, "normal offset");
static_assert(offsetof(Vertex, color) == 24, "color offset");

struct SceneObject {
    std::vector<Vertex> vertices;
};

class Scene {
public:
    Scene();
    ~Scene();
    Scene(Scene&&) noexcept;
    Scene& operator=(Scene&&) noexcept;

    static Scene create();
    void exportUsda(const std::string& path) const;
    const std::vector<SceneObject>& objects() const;
    std::vector<glm::vec3> translations(double time) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
