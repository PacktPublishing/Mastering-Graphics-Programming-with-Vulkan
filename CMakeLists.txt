cmake_minimum_required(VERSION 3.0.0)

project(RaptorEngine VERSION 0.1.0)

find_package(Vulkan REQUIRED)

if (UNIX)
find_package(SDL2 REQUIRED)
endif()

add_library(RaptorFoundation STATIC
    source/raptor/foundation/array.hpp
    source/raptor/foundation/assert.cpp
    source/raptor/foundation/assert.hpp
    source/raptor/foundation/bit.cpp
    source/raptor/foundation/bit.hpp
    source/raptor/foundation/blob_serialization.cpp
    source/raptor/foundation/blob_serialization.hpp
    source/raptor/foundation/blob.hpp
    source/raptor/foundation/camera.cpp
    source/raptor/foundation/camera.hpp
    source/raptor/foundation/color.cpp
    source/raptor/foundation/color.hpp
    source/raptor/foundation/data_structures.cpp
    source/raptor/foundation/data_structures.hpp
    source/raptor/foundation/file.cpp
    source/raptor/foundation/file.hpp
    source/raptor/foundation/gltf.cpp
    source/raptor/foundation/gltf.hpp
    source/raptor/foundation/hash_map.hpp
    source/raptor/foundation/log.cpp
    source/raptor/foundation/log.hpp
    source/raptor/foundation/memory_utils.hpp
    source/raptor/foundation/memory.cpp
    source/raptor/foundation/memory.hpp
    source/raptor/foundation/numerics.cpp
    source/raptor/foundation/numerics.hpp
    source/raptor/foundation/platform.hpp
    source/raptor/foundation/process.cpp
    source/raptor/foundation/process.hpp
    source/raptor/foundation/relative_data_structures.hpp
    source/raptor/foundation/resource_manager.cpp
    source/raptor/foundation/resource_manager.hpp
    source/raptor/foundation/serialization.cpp
    source/raptor/foundation/serialization.hpp
    source/raptor/foundation/service_manager.cpp
    source/raptor/foundation/service_manager.hpp
    source/raptor/foundation/service.cpp
    source/raptor/foundation/service.hpp
    source/raptor/foundation/string.cpp
    source/raptor/foundation/string.hpp
    source/raptor/foundation/time.cpp
    source/raptor/foundation/time.hpp
)

set_property(TARGET RaptorFoundation PROPERTY CXX_STANDARD 17)

target_compile_definitions(RaptorFoundation PRIVATE
    _CRT_SECURE_NO_WARNINGS

    TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_NO_SYSTEM_TRACING
)

target_include_directories(RaptorFoundation PRIVATE
    source
    source/raptor
)

add_library(RaptorApp STATIC
    source/raptor/application/application.cpp
    source/raptor/application/application.hpp
    source/raptor/application/game_camera.cpp
    source/raptor/application/game_camera.hpp
    source/raptor/application/game_application.cpp
    source/raptor/application/game_application.hpp
    source/raptor/application/input.cpp
    source/raptor/application/input.hpp
    source/raptor/application/keys.cpp
    source/raptor/application/keys.hpp
    source/raptor/application/window.cpp
    source/raptor/application/window.hpp
)

set_property(TARGET RaptorApp PROPERTY CXX_STANDARD 17)

target_compile_definitions(RaptorApp PRIVATE
    _CRT_SECURE_NO_WARNINGS

    TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_NO_SYSTEM_TRACING
)

target_include_directories(RaptorApp PRIVATE
    source
    source/raptor
    )

if (WIN32)
    target_include_directories(RaptorApp PRIVATE
        binaries/SDL2-2.0.18/include)
else()
    target_include_directories(RaptorApp PRIVATE
        ${SDL2_INCLUDE_DIRS})
endif()

add_library(RaptorExternal STATIC
    source/external/tlsf.c

    source/external/json.hpp

    source/external/imgui/imconfig.h
    source/external/imgui/imgui_draw.cpp
    source/external/imgui/imgui_impl_sdl.cpp
    source/external/imgui/imgui_impl_sdl.h
    source/external/imgui/imgui_internal.h
    source/external/imgui/imgui_memory_editor.h
    source/external/imgui/imgui_tables.cpp
    source/external/imgui/imgui_widgets.cpp
    source/external/imgui/imgui.cpp
    source/external/imgui/imgui.h
    source/external/imgui/imstb_rectpack.h
    source/external/imgui/imstb_textedit.h
    source/external/imgui/imstb_truetype.h
    source/external/imgui/stb_image.h
    source/external/imgui/TextEditor.cpp
    source/external/imgui/TextEditor.h

    source/external/tracy/tracy/Tracy.hpp
    source/external/tracy/tracy/TracyVulkan.hpp
    source/external/tracy/TracyClient.cpp

    source/external/enkiTS/LockLessMultiReadPipe.h
    source/external/enkiTS/TaskScheduler.cpp
    source/external/enkiTS/TaskScheduler.h

    source/external/meshoptimizer/allocator.cpp
    source/external/meshoptimizer/clusterizer.cpp
    source/external/meshoptimizer/indexcodec.cpp
    source/external/meshoptimizer/indexgenerator.cpp
    source/external/meshoptimizer/meshoptimizer.h
    source/external/meshoptimizer/overdrawanalyzer.cpp
    source/external/meshoptimizer/overdrawoptimizer.cpp
    source/external/meshoptimizer/simplifier.cpp
    source/external/meshoptimizer/spatialorder.cpp
    source/external/meshoptimizer/stripifier.cpp
    source/external/meshoptimizer/vcacheanalyzer.cpp
    source/external/meshoptimizer/vcacheoptimizer.cpp
    source/external/meshoptimizer/vertexcodec.cpp
    source/external/meshoptimizer/vertexfilter.cpp
    source/external/meshoptimizer/vfetchanalyzer.cpp
    source/external/meshoptimizer/vfetchoptimizer.cpp
)

if (WIN32)
    target_sources(RaptorExternal PRIVATE
        source/external/StackWalker.cpp)
endif()

target_compile_definitions(RaptorExternal PRIVATE
    TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_NO_SYSTEM_TRACING
)

if (WIN32)
    target_include_directories(RaptorExternal PRIVATE
        binaries/SDL2-2.0.18/include
        source/raptor
    )
else()
    target_link_libraries(RaptorExternal PRIVATE
        dl
    )
    target_include_directories(RaptorExternal PRIVATE
        ${SDL2_INCLUDE_DIRS})
endif()

set_property(TARGET RaptorExternal PROPERTY CXX_STANDARD 17)

add_subdirectory(source/chapter1)
add_subdirectory(source/chapter2)
add_subdirectory(source/chapter3)
add_subdirectory(source/chapter4)
add_subdirectory(source/chapter5)
add_subdirectory(source/chapter6)
add_subdirectory(source/chapter7)
add_subdirectory(source/chapter8)
add_subdirectory(source/chapter9)
add_subdirectory(source/chapter10)
add_subdirectory(source/chapter11)
add_subdirectory(source/chapter12)
add_subdirectory(source/chapter13)
add_subdirectory(source/chapter14)
add_subdirectory(source/chapter15)
