cmake_minimum_required(VERSION 3.15)
project(sr-game C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable pkg-config for library discovery (desktop only)
if(NOT EMSCRIPTEN)
    include(FindPkgConfig)
    pkg_check_modules(GLFW REQUIRED glfw3)
    pkg_check_modules(GLEW REQUIRED glew)
    find_package(ZLIB REQUIRED)
endif()

# SR-cpp is vendored under game/src/. We collect every .cpp recursively
# but deliberately ignore the vendored zlib `.c` files — system zlib
# (desktop) and `-sUSE_ZLIB=1` (Emscripten) cover both targets.
set(SR_CPP_ROOT "${CMAKE_SOURCE_DIR}/src/SR cpp")
file(GLOB_RECURSE SR_SOURCES "${SR_CPP_ROOT}/*.cpp")

# Strip the desktop entry point on web builds, and vice versa, so we
# don't collide on `int main()` from main.cpp + web_main.cpp.
set(WEB_MAIN "${CMAKE_SOURCE_DIR}/platform/web_main.cpp")
list(REMOVE_ITEM SR_SOURCES "${WEB_MAIN}")

if(EMSCRIPTEN)
    list(REMOVE_ITEM SR_SOURCES "${SR_CPP_ROOT}/main.cpp")
    list(APPEND SR_SOURCES "${WEB_MAIN}")

    add_executable(sr ${SR_SOURCES})

    # Required ports + WebGL2.
    target_compile_options(sr PRIVATE
        -sUSE_GLFW=3
        -sFULL_ES3=1
        -sUSE_ZLIB=1
        -fexceptions
    )

    # All linker-time -s flags. Emscripten requires the full -s flag list
    # at link time; compile-time only matters for headers.
    target_link_options(sr PRIVATE
        -sUSE_GLFW=3
        -sFULL_ES3=1
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sUSE_ZLIB=1
        -sALLOW_MEMORY_GROWTH=1
        -fexceptions
        -sMODULARIZE=1
        -sEXPORT_NAME=createSrModule
        -sENVIRONMENT=web
        -sEXPORTED_FUNCTIONS=['_main','_sr_set_local_identity','_sr_load_map','_sr_push_ghost','_sr_set_ghost_identity','_sr_remove_ghost','_sr_get_local_snapshot','_sr_get_player_screen_pos','_sr_set_binding','_sr_teleport_local','_sr_reset_local','_sr_set_target_fps','_malloc','_free']
        -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAPF32','stringToUTF8','UTF8ToString']
        --preload-file ${CMAKE_SOURCE_DIR}/assets/maps@/maps
    )

    # Drop the bundle straight into apps/web/public/ so Vite serves it.
    set_target_properties(sr PROPERTIES
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../apps/web/public"
    )
else()
    add_executable(sr_desktop ${SR_SOURCES})

    target_include_directories(sr_desktop PRIVATE
        "${SR_CPP_ROOT}"
        "${SR_CPP_ROOT}/emulation"
        "${SR_CPP_ROOT}/emulation/utility"
        "${SR_CPP_ROOT}/drawing"
        "${SR_CPP_ROOT}/command"
        "${SR_CPP_ROOT}/utility"
        ${GLFW_INCLUDE_DIRS}
        ${GLEW_INCLUDE_DIRS}
    )

    if(APPLE)
        # pkg_check_modules without IMPORTED_TARGET returns lib name + dir
        # as separate vars — both are needed for the linker.
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
        target_link_libraries(sr_desktop
            PRIVATE
            ${GLFW_LIBRARIES}
            ${GLEW_LIBRARIES}
            ZLIB::ZLIB
            OpenGL::OpenGL
        )
    endif()
endif()

# Common include dirs for the web build (skipped above to keep that
# branch terse).
if(EMSCRIPTEN)
    target_include_directories(sr PRIVATE
        "${SR_CPP_ROOT}"
        "${SR_CPP_ROOT}/emulation"
        "${SR_CPP_ROOT}/emulation/utility"
        "${SR_CPP_ROOT}/drawing"
        "${SR_CPP_ROOT}/command"
        "${SR_CPP_ROOT}/utility"
    )
endif()
