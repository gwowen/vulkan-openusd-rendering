cmake_minimum_required(VERSION 3.26)
project(vulkan-openusd-rendering LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

if(NOT pxr_DIR AND DEFINED ENV{pxr_DIR})
    set(pxr_DIR "$ENV{pxr_DIR}")
endif()
if(NOT pxr_DIR)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BootstrapOpenUsd.cmake")
endif()
find_package(pxr REQUIRED CONFIG)
find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SLANGC "")
if(DEFINED ENV{SLANG_DIR} AND EXISTS "$ENV{SLANG_DIR}/bin/slangc")
    set(SLANGC "$ENV{SLANG_DIR}/bin/slangc")
else()
    find_program(SLANGC_PROGRAM slangc)
    if(SLANGC_PROGRAM)
        set(SLANGC "${SLANGC_PROGRAM}")
    elseif(EXISTS "/opt/slang/bin/slangc")
        set(SLANGC "/opt/slang/bin/slangc")
    else()
        message(FATAL_ERROR "slangc not found; set SLANG_DIR or put slangc on PATH")
    endif()
endif()

set(SHADER_HEADER "${CMAKE_BINARY_DIR}/generated/shader_spv.hpp")
set(SHADER_VERT "${CMAKE_BINARY_DIR}/generated/shader.vert.spv")
set(SHADER_FRAG "${CMAKE_BINARY_DIR}/generated/shader.frag.spv")
add_custom_command(
    OUTPUT "${SHADER_VERT}" "${SHADER_FRAG}" "${SHADER_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
    COMMAND "${SLANGC}" "${CMAKE_SOURCE_DIR}/src/shader.slang"
        -target spirv
        -profile spirv_1_4
        -matrix-layout-column-major
        -fvk-use-entrypoint-name
        -entry vertexMain
        -stage vertex
        -o "${SHADER_VERT}"
    COMMAND "${SLANGC}" "${CMAKE_SOURCE_DIR}/src/shader.slang"
        -target spirv
        -profile spirv_1_4
        -matrix-layout-column-major
        -fvk-use-entrypoint-name
        -entry fragmentMain
        -stage fragment
        -o "${SHADER_FRAG}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/cmake/embed_spirv.py"
        "${SHADER_VERT}" "${SHADER_FRAG}" "${SHADER_HEADER}"
    DEPENDS "${CMAKE_SOURCE_DIR}/src/shader.slang" "${CMAKE_SOURCE_DIR}/cmake/embed_spirv.py"
    COMMENT "Compiling shader.slang to SPIR-V"
    VERBATIM
)

if(NOT TARGET usdGeom)
    message(FATAL_ERROR "OpenUSD config did not export the usdGeom target")
endif()

set(USD_RUNTIME_DIRS "")
foreach(lib usdGeom tf)
    if(DEFINED PXR_${lib}_LIBRARY)
        get_filename_component(lib_dir "${PXR_${lib}_LIBRARY}" DIRECTORY)
        list(APPEND USD_RUNTIME_DIRS "${lib_dir}")
    endif()
endforeach()
if(EXISTS "${CMAKE_BINARY_DIR}/tbb-install/lib")
    list(APPEND USD_RUNTIME_DIRS "${CMAKE_BINARY_DIR}/tbb-install/lib")
endif()
list(REMOVE_DUPLICATES USD_RUNTIME_DIRS)

function(link_usd target)
    target_link_libraries(${target} PUBLIC usdGeom)
    target_include_directories(${target} PUBLIC ${PXR_INCLUDE_DIRS})
    target_compile_definitions(${target} PUBLIC USD_PLUGIN_PATH="${pxr_DIR}/lib/usd")
    if(USD_RUNTIME_DIRS)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "${USD_RUNTIME_DIRS}")
    endif()
endfunction()

add_library(scene STATIC src/mesh.cpp src/scene.cpp)
target_include_directories(scene PUBLIC src)
target_link_libraries(scene PUBLIC glm::glm)
link_usd(scene)

add_executable(mesh_tests src/mesh_tests.cpp src/mesh.cpp)
target_include_directories(mesh_tests PRIVATE src)
target_link_libraries(mesh_tests PRIVATE glm::glm)

add_executable(scene_tests src/scene_tests.cpp)
target_link_libraries(scene_tests PRIVATE scene)

add_executable(vulkan-openusd-rendering
    src/main.cpp
    src/renderer.cpp
    "${SHADER_HEADER}"
)
target_include_directories(vulkan-openusd-rendering PRIVATE src "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(vulkan-openusd-rendering PRIVATE scene Vulkan::Vulkan PkgConfig::GLFW)
if(USD_RUNTIME_DIRS)
    set_target_properties(vulkan-openusd-rendering PROPERTIES BUILD_RPATH "${USD_RUNTIME_DIRS}")
endif()
