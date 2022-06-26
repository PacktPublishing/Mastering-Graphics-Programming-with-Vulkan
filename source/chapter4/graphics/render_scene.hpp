#pragma once

#include "foundation/array.hpp"
#include "foundation/platform.hpp"

#include "graphics/command_buffer.hpp"
#include "graphics/gpu_device.hpp"
#include "graphics/gpu_resources.hpp"

#include "external/cglm/types-struct.h"

#include <atomic>

namespace enki { class TaskScheduler; }

namespace raptor {

    struct Allocator;
    struct AsynchronousLoader;
    struct FrameGraph;
    struct GPUProfiler;
    struct ImGuiService;
    struct Renderer;
    struct SceneGraph;
    struct StackAllocator;

    static const u16 k_invalid_scene_texture_index      = u16_max;
    static const u32 k_material_descriptor_set_index    = 1;

    static bool recreate_per_thread_descriptors = false;
    static bool use_secondary_command_buffers   = false;

    //
    //
    enum DrawFlags {
        DrawFlags_AlphaMask     = 1 << 0,
        DrawFlags_DoubleSided   = 1 << 1,
        DrawFlags_Transparent   = 1 << 2,
    }; // enum DrawFlags

    //
    //
    struct GpuSceneData {
        mat4s                                   view_projection;
        vec4s                                   eye;
        vec4s                                   light_position;
        f32                                     light_range;
        f32                                     light_intensity;
        f32                                     padding[ 2 ];
    }; // struct GpuSceneData

    //
    //
    struct RenderScene {
        virtual                                 ~RenderScene() { };

        virtual void                            init( cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) { };
        virtual void                            shutdown( Renderer* renderer ) { };

        virtual void                            register_render_passes( FrameGraph* frame_graph ) { };
        virtual void                            prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph ) { };

        virtual void                            upload_materials() { };
        virtual void                            submit_draw_task( ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler ) { };

        SceneGraph*                             scene_graph;
        BufferHandle                            scene_cb;

        f32                                     global_scale = 1.f;
    }; // struct RenderScene

} // namespace raptor
