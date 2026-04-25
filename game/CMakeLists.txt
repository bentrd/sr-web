cmake_minimum_required(VERSION 3.15)
project(sr-game CXX)

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
    if(APPLE)
        # macOS: use brew-installed GLFW and GLEW via pkg-config
        # Use pkg_check_modules without IMPORTED_TARGET to avoid -isystem flags
        # that interfere with C++ standard library header path ordering
        pkg_check_modules(GLFW REQUIRED glfw3)
        pkg_check_modules(GLEW REQUIRED glew)
    else()
        # Windows: could use system/vcpkg packages or bundled headers in upstream/include/
        # For now, assume GLFW/GLEW are available system-wide
        pkg_check_modules(GLFW REQUIRED glfw3)
        pkg_check_modules(GLEW REQUIRED glew)
    endif()
endif()

# Source directories - all .cpp files from the SR cpp directory
set(SR_CPP_ROOT "${CMAKE_SOURCE_DIR}/upstream/SR cpp")

# Collect all .cpp files recursively
file(GLOB_RECURSE SR_SOURCES
    "${SR_CPP_ROOT}/*.cpp"
)

# Exclude zlib .c files from the main source list (we'll handle them separately)
list(FILTER SR_SOURCES EXCLUDE REGEX "emulation/zlib/.*\\.c$")

# Vendored zlib sources
file(GLOB ZLIB_SOURCES "${SR_CPP_ROOT}/emulation/zlib/*.c")

# All C++ sources for the game
set(GAME_SOURCES ${SR_SOURCES} ${ZLIB_SOURCES})

# Create the executable target
add_executable(sr_desktop ${GAME_SOURCES})

# Include directories
# Note: we do NOT include "${CMAKE_SOURCE_DIR}/upstream/include" because that contains
# Windows-specific vendored GLFW/GLEW headers. On macOS, we use system GLFW/GLEW instead.
# Note: the emulation directory contains a custom math.h that conflicts with <cmath>.
# We use SYSTEM visibility so it's treated as a system include (lower priority).
target_include_directories(sr_desktop PRIVATE
    "${SR_CPP_ROOT}"
    "${SR_CPP_ROOT}/emulation"
    "${SR_CPP_ROOT}/emulation/utility"
    "${SR_CPP_ROOT}/drawing"
    "${SR_CPP_ROOT}/command"
    "${SR_CPP_ROOT}/utility"
)

# Mark emulation directory as system include to prevent it from shadowing C++ stdlib headers
set_target_properties(sr_desktop PROPERTIES
    CXX_SYSTEM_INCLUDE_DIRECTORIES "${SR_CPP_ROOT}/emulation"
)

# Add GLFW/GLEW include directories (from pkg-config) on non-Emscripten builds
if(NOT EMSCRIPTEN)
    if(APPLE)
        target_include_directories(sr_desktop PRIVATE ${GLFW_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})
    else()
        target_include_directories(sr_desktop PRIVATE ${GLFW_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})
    endif()
endif()

# zlib sources include their own headers relative to emulation/zlib directory
# We add it as system include only for the zlib compilation units
set_source_files_properties(${ZLIB_SOURCES} PROPERTIES
    COMPILE_FLAGS "-isystem${SR_CPP_ROOT}/emulation"
)

# Link libraries based on platform
if(EMSCRIPTEN)
    # Emscripten handles linking via compiler flags
    target_link_libraries(sr_desktop)
else()
    if(APPLE)
        # macOS: link to brew-installed GLFW, GLEW, and native OpenGL frameworks
        target_link_libraries(sr_desktop
            PRIVATE
            ${GLFW_LIBRARIES}
            ${GLEW_LIBRARIES}
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
            OpenGL::OpenGL
        )
    endif()
endif()

# Optional: verbose build output for debugging
# set(CMAKE_VERBOSE_MAKEFILE ON)
