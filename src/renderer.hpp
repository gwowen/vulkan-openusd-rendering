#pragma once

#include "scene.hpp"

#include <memory>
#include <vector>

struct GLFWwindow;

class Renderer {
public:
    Renderer(GLFWwindow* window, const std::vector<SceneObject>& objects);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void noteResize();
    void draw(const std::vector<glm::vec3>& translations);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
