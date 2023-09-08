add_executable(Chapter2
    graphics/command_buffer.cpp
    graphics/command_buffer.hpp
    graphics/gpu_device.cpp
    graphics/gpu_device.hpp
    graphics/gpu_enum.hpp
    graphics/gpu_profiler.cpp
    graphics/gpu_profiler.hpp
    graphics/gpu_resources.cpp
    graphics/gpu_resources.hpp
    graphics/renderer.cpp
    graphics/renderer.hpp
    graphics/spirv_parser.cpp
    graphics/spirv_parser.hpp

    graphics/raptor_imgui.cpp
    graphics/raptor_imgui.hpp

    main.cpp
)

set_property(TARGET Chapter2 PROPERTY CXX_STANDARD 17)

if (WIN32)
    target_compile_definitions(Chapter2 PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
endif()

target_compile_definitions(Chapter2 PRIVATE
    RAPTOR_SHADER_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/shaders/"
)

target_compile_definitions(Chapter2 PRIVATE
    TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_NO_SYSTEM_TRACING
)

target_include_directories(Chapter2 PRIVATE
    .
    ..
    ../raptor
    ${Vulkan_INCLUDE_DIRS}
)

if (WIN32)
    target_include_directories(Chapter2 PRIVATE
        ../../binaries/SDL2-2.0.18/include)

    target_link_directories(Chapter2 PRIVATE
        ../../binaries/SDL2-2.0.18/lib/x64
    )
else()
    target_include_directories(Chapter2 PRIVATE
        ${SDL2_INCLUDE_DIRS})
endif()

target_link_libraries(Chapter2 PRIVATE
    RaptorFoundation
    RaptorExternal
    RaptorApp
    ${Vulkan_LIBRARIES}
)

if (WIN32)
    target_link_libraries(Chapter2 PRIVATE
        SDL2)
else()
    target_link_libraries(Chapter2 PRIVATE
        dl
        pthread
        SDL2::SDL2)
endif()

if (WIN32)
    set(DLLS_TO_COPY
        ${CMAKE_CURRENT_SOURCE_DIR}/../../binaries/SDL2-2.0.18/lib/x64/SDL2.dll
    )

    foreach(DLL ${DLLS_TO_COPY})
        add_custom_command(TARGET Chapter2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${DLL} "$(OutDir)"
            VERBATIM
        )
    endforeach()
endif()

file(GLOB GLSL_SHADERS
    shaders/*.vert
    shaders/*.frag)

add_custom_target(Chapter2SpvShaders
    SOURCES ${GLSL_SHADERS}
)

if (WIN32)
    file(TO_NATIVE_PATH "$ENV{VULKAN_SDK}/bin/glslc.exe" GLSL_COMPILER)
else()
    file(TO_NATIVE_PATH "$ENV{VULKAN_SDK}/bin/glslc" GLSL_COMPILER)
endif()

if (NOT EXISTS ${GLSL_COMPILER})
    message(FATAL_ERROR "${GLSL_COMPILER} is not a valid path. Please make sure the VULKAN_SDK environment variable is set.")
endif()

foreach (GLSL_SHADER ${GLSL_SHADERS})
    add_custom_command(TARGET Chapter2SpvShaders
        COMMAND ${GLSL_COMPILER} ${GLSL_SHADER}
        DEPENDS ${GLSL_SHADER}
    )
endforeach()

add_dependencies(Chapter2
    Chapter2SpvShaders
)
