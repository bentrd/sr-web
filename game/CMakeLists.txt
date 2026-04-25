cmake_minimum_required(VERSION 3.15)
project(sr-game C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable pkg-config for library discovery
include(FindPkgConfig)

# Platform-specific configuration
if(EMSCRIPTEN)
    # Web (Emscripten) build
    # These flags configure WebGL 2.0 and necessary Emscripten ports
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sUSE_GLFW=3 -sFULL_ES3=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sUSE_ZLIB=1 -sALLOW_MEMORY_GROWTH=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sEXPORTED_FUNCTIONS='[\"_main\",\"_sr_set_local_identity\",\"_sr_load_map\",\"_sr_push_ghost\",\"_sr_set_ghost_identity\",\"_sr_remove_ghost\",\"_sr_get_local_snapshot\",\"_sr_get_player_screen_pos\"]'")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sEXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\",\"HEAPU8\",\"HEAPF32\",\"stringToUTF8\",\"UTF8ToString\"]'")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --preload-file game/assets/maps@/maps")
else()
    # Desktop (native) build - macOS and Windows support
    pkg_check_modules(GLFW REQUIRED glfw3)
    pkg_check_modules(GLEW REQUIRED glew)
    find_package(ZLIB REQUIRED)
endif()

# SR-cpp is vendored under game/src/ (was a submodule originally; we own
# the source now because Phase 4b will rewrite a chunk of it).
set(SR_CPP_ROOT "${CMAKE_SOURCE_DIR}/src/SR cpp")

# Collect all .cpp files recursively. We deliberately ignore the vendored
# zlib (.c files in emulation/zlib/) because:
#   - macOS / Linux ship a system zlib that's a perfect drop-in
#   - Emscripten's `-sUSE_ZLIB=1` provides one for the web build
#   - The vendored copy expects Windows-style POSIX headers that don't
#     compile cleanly on macOS without invasive patches
file(GLOB_RECURSE SR_SOURCES "${SR_CPP_ROOT}/*.cpp")
set(GAME_SOURCES ${SR_SOURCES})

# Create the executable target
add_executable(sr_desktop ${GAME_SOURCES})

# Include directories.
# We deliberately exclude `src/include/` (vendored Windows GLFW/GLEW headers) —
# brew-installed system copies are used instead.
target_include_directories(sr_desktop PRIVATE
    "${SR_CPP_ROOT}"
    "${SR_CPP_ROOT}/emulation"
    "${SR_CPP_ROOT}/emulation/utility"
    "${SR_CPP_ROOT}/drawing"
    "${SR_CPP_ROOT}/command"
    "${SR_CPP_ROOT}/utility"
)

# Add GLFW/GLEW include directories (from pkg-config) on non-Emscripten builds
if(NOT EMSCRIPTEN)
    if(APPLE)
        target_include_directories(sr_desktop PRIVATE ${GLFW_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})
    else()
        target_include_directories(sr_desktop PRIVATE ${GLFW_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})
    endif()
endif()

# Link libraries based on platform
if(EMSCRIPTEN)
    # Emscripten handles linking via compiler flags
    target_link_libraries(sr_desktop)
else()
    if(APPLE)
        # macOS: link to brew-installed GLFW, GLEW, and native OpenGL frameworks.
        # pkg_check_modules without IMPORTED_TARGET gives us names + dirs as
        # separate variables — both are needed for the linker.
        target_link_directories(sr_desktop PRIVATE
            ${GLFW_LIBRARY_DIRS}
            ${GLEW_LIBRARY_DIRS}
        )
        target_link_libraries(sr_desktop
            PRIVATE
            ${GLFW_LIBRARIES}
            ${GLEW_LIBRARIES}
            ZLIB::ZLIB
            "-framework OpenGL"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
    else()
        # Windows or other Unix-like systems
        target_link_libraries(sr_desktop
            PRIVATE
            ${GLFW_LIBRARIES}
            ${GLEW_LIBRARIES}
            ZLIB::ZLIB
            OpenGL::OpenGL
        )
    endif()
endif()

# Optional: verbose build output for debugging
# set(CMAKE_VERBOSE_MAKEFILE ON)
