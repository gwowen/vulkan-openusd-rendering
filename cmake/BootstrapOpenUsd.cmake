# Builds oneTBB 2022.1 and a Python-free OpenUSD core when pxr is not already installed.
# Distro oneTBB 2021.5 does not provide the task_group(context) constructor OpenUSD 26.05 uses.

set(TBB_INSTALL_DIR "${CMAKE_BINARY_DIR}/tbb-install")
set(OPENUSD_INSTALL_DIR "${CMAKE_BINARY_DIR}/openusd-install")

function(run_step)
    execute_process(COMMAND ${ARGV} COMMAND_ERROR_IS_FATAL ANY)
endfunction()

if(NOT EXISTS "${TBB_INSTALL_DIR}/lib/cmake/TBB/TBBConfig.cmake")
    set(TBB_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/oneTBB")
    if(NOT EXISTS "${TBB_SOURCE_DIR}/CMakeLists.txt")
        run_step(git clone --depth 1 --branch v2022.1.0 https://github.com/uxlfoundation/oneTBB.git "${TBB_SOURCE_DIR}")
    endif()
    run_step(cmake -S "${TBB_SOURCE_DIR}" -B "${CMAKE_BINARY_DIR}/tbb-build"
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${TBB_INSTALL_DIR}
        -DTBB_TEST=OFF
        -DTBB_STRICT=OFF)
    run_step(cmake --build "${CMAKE_BINARY_DIR}/tbb-build" -j)
    run_step(cmake --install "${CMAKE_BINARY_DIR}/tbb-build")
endif()

if(NOT EXISTS "${OPENUSD_INSTALL_DIR}/pxrConfig.cmake")
    set(USD_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/OpenUSD")
    if(NOT EXISTS "${USD_SOURCE_DIR}/CMakeLists.txt")
        run_step(git clone --depth 1 --branch v26.05 https://github.com/PixarAnimationStudios/OpenUSD.git "${USD_SOURCE_DIR}")
    endif()
    run_step(cmake -S "${USD_SOURCE_DIR}" -B "${CMAKE_BINARY_DIR}/openusd-build"
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${OPENUSD_INSTALL_DIR}
        -DCMAKE_PREFIX_PATH=${TBB_INSTALL_DIR}
        -DTBB_DIR=${TBB_INSTALL_DIR}/lib/cmake/TBB
        "-DCMAKE_INSTALL_RPATH=\$ORIGIN;${TBB_INSTALL_DIR}/lib"
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
        -DPXR_ENABLE_PYTHON_SUPPORT=OFF
        -DPXR_BUILD_IMAGING=OFF
        -DPXR_BUILD_USD_IMAGING=OFF
        -DPXR_BUILD_TESTS=OFF
        -DPXR_BUILD_EXAMPLES=OFF
        -DPXR_BUILD_TUTORIALS=OFF
        -DPXR_BUILD_USD_TOOLS=OFF
        -DPXR_BUILD_DOCUMENTATION=OFF
        -DPXR_BUILD_USD_VALIDATION=OFF
        -DPXR_BUILD_EXEC=OFF
        -DPXR_ENABLE_HDF5_SUPPORT=OFF
        -DPXR_ENABLE_MATERIALX_SUPPORT=OFF
        -DPXR_ENABLE_PTEX_SUPPORT=OFF
        -DPXR_ENABLE_GL_SUPPORT=OFF
        -DBUILD_SHARED_LIBS=ON)
    run_step(cmake --build "${CMAKE_BINARY_DIR}/openusd-build" -j)
    run_step(cmake --install "${CMAKE_BINARY_DIR}/openusd-build")
endif()

set(pxr_DIR "${OPENUSD_INSTALL_DIR}" CACHE PATH "OpenUSD package config directory" FORCE)
set(TBB_DIR "${TBB_INSTALL_DIR}/lib/cmake/TBB" CACHE PATH "oneTBB package config directory" FORCE)
