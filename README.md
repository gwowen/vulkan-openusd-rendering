# vulkan-openusd-rendering

A C++ port of rust-openusd-vulkan. It builds an OpenUSD stage of a checkerboard cube, torus, and sphere, then draws them with Vulkan as they orbit a shared center on a sky-blue background.

The scene is authored in memory with the C++ OpenUSD library. Each shape is a `Mesh` with a uniform black-and-white `primvars:displayColor` checkerboard. Motion is time-sampled `xformOp:translate`: three circular paths of radius 3, 120 degrees apart, on planes tilted 0, +55, and -55 degrees. One revolution takes about eight seconds. On startup the stage is also written to `scene.usda` in the working directory.

The window is GLFW. Rendering uses Vulkan through Vulkan-Hpp. Matrices are GLM. Shaders are [Slang](https://shader-slang.org/), compiled to SPIR-V by `slangc` when the project builds.

## Requirements

- A C++17 compiler and CMake 3.26 or newer
- GLFW and a Vulkan 1.2 loader, plus the Vulkan-Hpp headers
- `slangc` 2026.18 or compatible
- Git, for the first configure

`slangc` is resolved from `SLANG_DIR/bin`, then `PATH`, then `/opt/slang/bin/slangc`.

The first configure builds oneTBB 2022.1 and OpenUSD 26.05 (Python, imaging, and tools disabled) into the build tree when `pxr_DIR` is not set. Distro oneTBB 2021.5 is too old for that OpenUSD release. Later configures reuse the install. Set `pxr_DIR` to an existing OpenUSD package config to skip the bootstrap.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/vulkan-openusd-rendering
```

Close the window or press Escape to quit.

`VK_LAYER_KHRONOS_validation` is enabled automatically when that layer is installed.

`mesh_tests` checks checkerboard winding. `scene_tests` checks that the authored orbit moves and stays on radius 3.

## License

MIT. See [LICENSE](LICENSE).
