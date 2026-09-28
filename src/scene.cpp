#include "scene.hpp"

#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>

#include <cmath>
#include <cstdlib>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = kPi * 2.0;

struct Body {
    const char* path;
    double phase;
    double inclination;
    MeshData mesh;
};

void ensureUsdPlugins() {
    static const bool ready = [] {
        if (std::getenv("PXR_PLUGINPATH_NAME") == nullptr) {
            if (setenv("PXR_PLUGINPATH_NAME", USD_PLUGIN_PATH, 0) != 0) {
                throw std::runtime_error("failed to set PXR_PLUGINPATH_NAME");
            }
        }
        return true;
    }();
    (void)ready;
}

pxr::GfVec3f toVec3f(glm::vec3 value) {
    return pxr::GfVec3f(value.x, value.y, value.z);
}

pxr::VtArray<pxr::GfVec3f> toVec3fArray(const std::vector<glm::vec3>& values) {
    pxr::VtArray<pxr::GfVec3f> array(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        array[index] = toVec3f(values[index]);
    }
    return array;
}

pxr::VtArray<int> toIntArray(const std::vector<int>& values) {
    return pxr::VtArray<int>(values.begin(), values.end());
}

void authorMesh(const pxr::UsdStagePtr& stage, const char* path, const MeshData& mesh) {
    pxr::UsdGeomMesh prim = pxr::UsdGeomMesh::Define(stage, pxr::SdfPath(path));
    if (!prim) {
        throw std::runtime_error(std::string("failed to define ") + path);
    }
    prim.CreatePointsAttr().Set(toVec3fArray(mesh.points));
    prim.CreateNormalsAttr().Set(toVec3fArray(mesh.normals));
    prim.SetNormalsInterpolation(pxr::UsdGeomTokens->vertex);
    prim.CreateFaceVertexCountsAttr().Set(toIntArray(mesh.faceVertexCounts));
    prim.CreateFaceVertexIndicesAttr().Set(toIntArray(mesh.faceVertexIndices));

    pxr::VtArray<pxr::GfVec3f> colors(2);
    colors[0] = toVec3f(kPalette[0]);
    colors[1] = toVec3f(kPalette[1]);
    pxr::UsdGeomPrimvar color = prim.CreateDisplayColorPrimvar(pxr::UsdGeomTokens->uniform);
    color.Set(colors);
    color.SetIndices(toIntArray(mesh.colorIndices));
    prim.CreateSubdivisionSchemeAttr().Set(pxr::UsdGeomTokens->none);
}

pxr::GfVec3d orbitPosition(double sample, double phase, double inclination) {
    const double theta = sample / kOrbitSamples * kTau + phase;
    const double x = 3.0 * std::cos(theta);
    const double flatZ = 3.0 * std::sin(theta);
    const double y = -flatZ * std::sin(inclination);
    const double z = flatZ * std::cos(inclination);
    return pxr::GfVec3d(x, y, z);
}

void authorOrbit(const pxr::UsdStagePtr& stage, const char* path, double phase, double inclination) {
    pxr::UsdGeomXformable xformable(stage->GetPrimAtPath(pxr::SdfPath(path)));
    pxr::UsdGeomXformOp translate = xformable.AddTranslateOp(pxr::UsdGeomXformOp::PrecisionDouble);
    for (int sample = 0; sample <= static_cast<int>(kOrbitSamples); ++sample) {
        translate.Set(orbitPosition(sample, phase, inclination), pxr::UsdTimeCode(sample));
    }
}

template <typename T>
T required(const pxr::UsdAttribute& attribute, const std::string& path) {
    T value;
    if (!attribute || !attribute.Get(&value)) {
        throw std::runtime_error("missing " + path);
    }
    return value;
}

std::vector<Vertex> readMesh(const pxr::UsdStagePtr& stage, const char* path) {
    pxr::UsdGeomMesh mesh(stage->GetPrimAtPath(pxr::SdfPath(path)));
    if (!mesh) {
        throw std::runtime_error(std::string(path) + " is not a mesh");
    }
    const std::string prefix(path);
    const auto points = required<pxr::VtArray<pxr::GfVec3f>>(mesh.GetPointsAttr(), prefix + ".points");
    const auto normals = required<pxr::VtArray<pxr::GfVec3f>>(mesh.GetNormalsAttr(), prefix + ".normals");
    const auto counts = required<pxr::VtArray<int>>(mesh.GetFaceVertexCountsAttr(), prefix + ".faceVertexCounts");
    const auto indices = required<pxr::VtArray<int>>(mesh.GetFaceVertexIndicesAttr(), prefix + ".faceVertexIndices");

    pxr::UsdGeomPrimvar colorPrimvar = mesh.GetDisplayColorPrimvar();
    pxr::VtArray<pxr::GfVec3f> colors;
    if (!colorPrimvar || !colorPrimvar.Get(&colors)) {
        throw std::runtime_error(prefix + " is missing displayColor");
    }
    pxr::VtIntArray colorIndices;
    if (!colorPrimvar.GetIndices(&colorIndices)) {
        throw std::runtime_error(prefix + " is missing displayColor indices");
    }
    if (colors.size() != std::size(kPalette)) {
        throw std::runtime_error(prefix + " displayColor palette has an unexpected size");
    }
    if (colorIndices.size() != counts.size()) {
        throw std::runtime_error(prefix + " color indices do not match face count");
    }

    std::vector<Vertex> vertices;
    std::size_t cursor = 0;
    for (std::size_t face = 0; face < counts.size(); ++face) {
        const int count = counts[face];
        if (count < 0 || cursor + static_cast<std::size_t>(count) > indices.size()) {
            throw std::runtime_error(prefix + " face is missing indices");
        }
        if (count < 3) {
            cursor += static_cast<std::size_t>(count);
            continue;
        }
        const int colorIndex = colorIndices[face];
        if (colorIndex < 0 || static_cast<std::size_t>(colorIndex) >= colors.size()) {
            throw std::runtime_error(prefix + " color index is out of range");
        }
        const pxr::GfVec3f& color = colors[static_cast<std::size_t>(colorIndex)];
        const glm::vec3 colorValue(color[0], color[1], color[2]);
        for (int corner = 1; corner < count - 1; ++corner) {
            const int ids[] = {indices[cursor], indices[cursor + static_cast<std::size_t>(corner)],
                               indices[cursor + static_cast<std::size_t>(corner) + 1]};
            for (int id : ids) {
                if (id < 0 || static_cast<std::size_t>(id) >= points.size() ||
                    static_cast<std::size_t>(id) >= normals.size()) {
                    throw std::runtime_error(prefix + " point index is out of range");
                }
                const pxr::GfVec3f& point = points[static_cast<std::size_t>(id)];
                const pxr::GfVec3f& normal = normals[static_cast<std::size_t>(id)];
                vertices.push_back(Vertex{
                    glm::vec3(point[0], point[1], point[2]),
                    glm::vec3(normal[0], normal[1], normal[2]),
                    colorValue,
                });
            }
        }
        cursor += static_cast<std::size_t>(count);
    }
    return vertices;
}

double wrapTime(double time) {
    double wrapped = std::fmod(time, kOrbitSamples);
    if (wrapped < 0.0) {
        wrapped += kOrbitSamples;
    }
    return wrapped;
}

}  // namespace

struct Scene::Impl {
    pxr::UsdStageRefPtr stage;
    std::vector<SceneObject> objects;
    std::vector<std::string> paths;
};

Scene::Scene() = default;
Scene::~Scene() = default;
Scene::Scene(Scene&&) noexcept = default;
Scene& Scene::operator=(Scene&&) noexcept = default;

Scene Scene::create() {
    ensureUsdPlugins();
    pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("scene.usda");
    if (!stage) {
        throw std::runtime_error("failed to create the in-memory USD stage");
    }
    pxr::UsdPrim world = stage->DefinePrim(pxr::SdfPath("/World"), pxr::TfToken("Xform"));
    if (!world) {
        throw std::runtime_error("failed to define /World");
    }
    stage->SetDefaultPrim(world);
    stage->SetStartTimeCode(0.0);
    stage->SetEndTimeCode(kOrbitSamples);

    const Body bodies[] = {
        {"/World/Cube", 0.0, 0.0, checkerCube(8, 1.25f)},
        {"/World/Torus", kPi * 2.0 / 3.0, 55.0 * kPi / 180.0, checkerTorus(40, 20, 0.65f, 0.22f)},
        {"/World/Sphere", kPi * 4.0 / 3.0, -55.0 * kPi / 180.0, checkerSphere(24, 32, 0.7f)},
    };

    Scene scene;
    scene.impl_ = std::make_unique<Impl>();
    scene.impl_->stage = stage;
    for (const Body& body : bodies) {
        authorMesh(stage, body.path, body.mesh);
        authorOrbit(stage, body.path, body.phase, body.inclination);
        scene.impl_->objects.push_back(SceneObject{readMesh(stage, body.path)});
        scene.impl_->paths.emplace_back(body.path);
    }
    return scene;
}

void Scene::exportUsda(const std::string& path) const {
    if (!impl_->stage->GetRootLayer()->Export(path)) {
        throw std::runtime_error("failed to export " + path);
    }
}

const std::vector<SceneObject>& Scene::objects() const {
    return impl_->objects;
}

std::vector<glm::vec3> Scene::translations(double time) const {
    const pxr::UsdTimeCode timeCode(wrapTime(time));
    std::vector<glm::vec3> translations;
    translations.reserve(impl_->paths.size());
    for (const std::string& path : impl_->paths) {
        pxr::UsdGeomXformable xformable(impl_->stage->GetPrimAtPath(pxr::SdfPath(path)));
        bool resetsXformStack = false;
        const std::vector<pxr::UsdGeomXformOp> ops = xformable.GetOrderedXformOps(&resetsXformStack);
        if (ops.empty()) {
            throw std::runtime_error(path + " has no xform ops");
        }
        pxr::GfVec3d value;
        if (!ops.front().Get(&value, timeCode)) {
            throw std::runtime_error("missing " + path + ".xformOp:translate");
        }
        translations.emplace_back(static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2]));
    }
    return translations;
}
