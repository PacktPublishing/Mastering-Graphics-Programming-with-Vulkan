add_executable(Chapter14
    graphics/asynchronous_loader.cpp
    graphics/asynchronous_loader.hpp
    graphics/command_buffer.cpp
    graphics/command_buffer.hpp
    graphics/frame_graph.cpp
    graphics/frame_graph.hpp
    graphics/gltf_scene.cpp
    graphics/gltf_scene.hpp
    graphics/gpu_device.cpp
    graphics/gpu_device.hpp
    graphics/gpu_enum.hpp
    graphics/gpu_profiler.cpp
    graphics/gpu_profiler.hpp
    graphics/gpu_resources.cpp
    graphics/gpu_resources.hpp
    graphics/obj_scene.cpp
    graphics/obj_scene.hpp
    graphics/render_resources_loader.cpp
    graphics/render_resources_loader.hpp
    graphics/render_scene.cpp
    graphics/render_scene.hpp
    graphics/renderer.cpp
    graphics/renderer.hpp
    graphics/scene_graph.cpp
    graphics/scene_graph.hpp
    graphics/spirv_parser.cpp
    graphics/spirv_parser.hpp

    graphics/raptor_imgui.cpp
    graphics/raptor_imgui.hpp

    main.cpp
)

set_property(TARGET Chapter14 PROPERTY CXX_STANDARD 17)

if (WIN32)
    target_compile_definitions(Chapter14 PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
endif()

target_compile_definitions(Chapter14 PRIVATE
    RAPTOR_WORKING_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}"
    RAPTOR_SHADER_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/shaders/"
    RAPTOR_DATA_FOLDER="${CMAKE_SOURCE_DIR}/binaries/data"
)

target_compile_definitions(Chapter14 PRIVATE
    TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_NO_SYSTEM_TRACING
)

target_include_directories(Chapter14 PRIVATE
    .
    ..
    ../raptor
    ../../binaries/assimp/include
    ${Vulkan_INCLUDE_DIRS}
)

if (WIN32)
    target_link_directories(Chapter14 PRIVATE
        ../../binaries/assimp/windows/bin
        ../../binaries/assimp/windows/lib
        ../../binaries/SDL2-2.0.18/lib/x64
    )

    target_include_directories(Chapter14 PRIVATE
        ../../binaries/SDL2-2.0.18/include)
else()
    target_link_directories(Chapter14 PRIVATE
        ../../binaries/assimp/linux/lib)

    target_include_directories(Chapter14 PRIVATE
        ${SDL2_INCLUDE_DIRS})
endif()

if (WIN32)
    target_link_libraries(Chapter14 PRIVATE
        assimp-vc142-mt
        SDL2)
else()
    target_link_libraries(Chapter14 PRIVATE
        dl
        pthread
        assimp
        SDL2::SDL2)
endif()

target_link_libraries(Chapter14 PRIVATE
    RaptorFoundation
    RaptorExternal
    RaptorApp
    ${Vulkan_LIBRARIES}
)

if (WIN32)
    set(DLLS_TO_COPY
        ${CMAKE_CURRENT_SOURCE_DIR}/../../binaries/SDL2-2.0.18/lib/x64/SDL2.dll
        ${CMAKE_CURRENT_SOURCE_DIR}/../../binaries/assimp/windows/bin/assimp-vc142-mt.dll
    )

    foreach(DLL ${DLLS_TO_COPY})
        add_custom_command(TARGET Chapter14 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${DLL} "$(OutDir)"
            VERBATIM
        )
    endforeach()
endif()
