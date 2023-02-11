
#include "application/window.hpp"
#include "application/input.hpp"
#include "application/keys.hpp"
#include "application/game_camera.hpp"

#include "graphics/gpu_device.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/spirv_parser.hpp"
#include "graphics/gpu_profiler.hpp"
#include "graphics/raptor_imgui.hpp"
#include "graphics/renderer.hpp"
#include "graphics/render_scene.hpp"
#include "graphics/gltf_scene.hpp"
#include "graphics/obj_scene.hpp"
#include "graphics/frame_graph.hpp"
#include "graphics/asynchronous_loader.hpp"
#include "graphics/scene_graph.hpp"
#include "graphics/render_resources_loader.hpp"

#include "external/cglm/struct/mat3.h"
#include "external/cglm/struct/mat4.h"
#include "external/cglm/struct/cam.h"
#include "external/cglm/struct/affine.h"
#include "external/enkiTS/TaskScheduler.h"
#include "external/json.hpp"

#include "foundation/file.hpp"
#include "foundation/numerics.hpp"
#include "foundation/time.hpp"
#include "foundation/resource_manager.hpp"

#include "external/imgui/imgui.h"
#include "external/stb_image.h"
#include "external/tracy/tracy/Tracy.hpp"

#include <stdio.h>
#include <stdlib.h>

///////////////////////////////////////

// Input callback
static void input_os_messages_callback( void* os_event, void* user_data ) {
    raptor::InputService* input = ( raptor::InputService* )user_data;
    input->on_event( os_event );
}

// IOTasks ////////////////////////////////////////////////////////////////
//
//
struct RunPinnedTaskLoopTask : enki::IPinnedTask {

    void Execute() override {
        while ( task_scheduler->GetIsRunning() && execute ) {
            task_scheduler->WaitForNewPinnedTasks(); // this thread will 'sleep' until there are new pinned tasks
            task_scheduler->RunPinnedTasks();
        }
    }

    enki::TaskScheduler*    task_scheduler;
    bool                    execute         = true;
}; // struct RunPinnedTaskLoopTask

//
//
struct AsynchronousLoadTask : enki::IPinnedTask {

    void Execute() override {
        // Do file IO
        while ( execute ) {
            async_loader->update( nullptr );
        }
    }

    raptor::AsynchronousLoader* async_loader;
    enki::TaskScheduler*        task_scheduler;
    bool                        execute         = true;
}; // struct AsynchronousLoadTask

//
//
int main( int argc, char** argv ) {

    if ( argc < 2 ) {
        printf( "Usage: chapter4 [path to glTF model]\n");
        InjectDefault3DModel();
    }

    using namespace raptor;
    // Init services
    MemoryServiceConfiguration memory_configuration;
    memory_configuration.maximum_dynamic_size = rgiga( 2ull );

    MemoryService::instance()->init( &memory_configuration );
    Allocator* allocator = &MemoryService::instance()->system_allocator;

    StackAllocator scratch_allocator;
    scratch_allocator.init( rmega( 8 ) );

    enki::TaskSchedulerConfig config;
    // In this example we create more threads than the hardware can run,
    // because the IO thread will spend most of it's time idle or blocked
    // and therefore not scheduled for CPU time by the OS
    config.numTaskThreadsToCreate += 1;
    enki::TaskScheduler task_scheduler;

    task_scheduler.Initialize( config );

    // window
    WindowConfiguration wconf{ 1280, 800, "Raptor Chapter 4", &MemoryService::instance()->system_allocator};
    raptor::Window window;
    window.init( &wconf );

    InputService input;
    input.init( allocator );

    // Callback register: input needs to react to OS messages.
    window.register_os_messages_callback( input_os_messages_callback, &input );

    // graphics
    DeviceCreation dc;
    dc.set_window( window.width, window.height, window.platform_handle ).set_allocator( &MemoryService::instance()->system_allocator )
      .set_num_threads( task_scheduler.GetNumTaskThreads() ).set_linear_allocator( &scratch_allocator );
    GpuDevice gpu;
    gpu.init( dc );

    ResourceManager rm;
    rm.init( allocator, nullptr );

    GPUProfiler gpu_profiler;
    gpu_profiler.init( allocator, 100 );

    Renderer renderer;
    renderer.init( { &gpu, allocator } );
    renderer.set_loaders( &rm );

    ImGuiService* imgui = ImGuiService::instance();
    ImGuiServiceConfiguration imgui_config{ &gpu, window.platform_handle };
    imgui->init( &imgui_config );

    GameCamera game_camera;
    game_camera.camera.init_perpective( 0.1f, 1000.f, 60.f, wconf.width * 1.f / wconf.height );
    game_camera.init( true, 20.f, 6.f, 0.1f );

    time_service_init();

    FrameGraphBuilder frame_graph_builder;
    frame_graph_builder.init( &gpu );

    FrameGraph frame_graph;
    frame_graph.init( &frame_graph_builder );

    RenderResourcesLoader render_resources_loader;

    // Load frame graph and parse gpu techniques
    {
        sizet scratch_marker = scratch_allocator.get_marker();

        StringBuffer temporary_name_buffer;
        temporary_name_buffer.init( 1024, &scratch_allocator );
        cstring frame_graph_path = temporary_name_buffer.append_use_f( "%s/%s", RAPTOR_WORKING_FOLDER, "graph.json" );

        frame_graph.parse( frame_graph_path, &scratch_allocator );
        frame_graph.compile();

        render_resources_loader.init( &renderer, &scratch_allocator, &frame_graph );

        // Parse techniques
        GpuTechniqueCreation gtc;
        temporary_name_buffer.clear();
        cstring full_screen_pipeline_path = temporary_name_buffer.append_use_f( "%s/%s", RAPTOR_SHADER_FOLDER, "fullscreen.json" );
        render_resources_loader.load_gpu_technique( full_screen_pipeline_path );

        temporary_name_buffer.clear();
        cstring main_pipeline_path = temporary_name_buffer.append_use_f( "%s/%s", RAPTOR_SHADER_FOLDER, "main.json" );
        render_resources_loader.load_gpu_technique( main_pipeline_path );

        temporary_name_buffer.clear();
        cstring pbr_pipeline_path = temporary_name_buffer.append_use_f( "%s/%s", RAPTOR_SHADER_FOLDER, "pbr_lighting.json" );
        render_resources_loader.load_gpu_technique( pbr_pipeline_path );

        temporary_name_buffer.clear();
        cstring dof_pipeline_path = temporary_name_buffer.append_use_f( "%s/%s", RAPTOR_SHADER_FOLDER, "dof.json" );
        render_resources_loader.load_gpu_technique( dof_pipeline_path );

        scratch_allocator.free_marker( scratch_marker );
    }

    SceneGraph scene_graph;
    scene_graph.init( allocator, 4 );

    // [TAG: Multithreading]
    AsynchronousLoader async_loader;
    async_loader.init( &renderer, &task_scheduler, allocator );

    Directory cwd{ };
    directory_current(&cwd);

    char file_base_path[512]{ };
    memcpy( file_base_path, argv[ 1 ], strlen( argv[ 1] ) );
    file_directory_from_path( file_base_path );

    directory_change( file_base_path );

    char file_name[512]{ };
    memcpy( file_name, argv[ 1 ], strlen( argv[ 1] ) );
    file_name_from_path( file_name );

    RenderScene* scene = nullptr;

    char* file_extension = file_extension_from_path( file_name );

    if ( strcmp( file_extension, "gltf" ) == 0 ) {
        scene = new glTFScene;
    } else if ( strcmp( file_extension, "obj" ) == 0 ) {
        scene = new ObjScene;
    }

    scene->init( file_name, file_base_path, allocator, &scratch_allocator, &async_loader );

    // NOTE(marco): restore working directory
    directory_change( cwd.path );

    scene->register_render_passes( &frame_graph );
    scene->prepare_draws( &renderer, &scratch_allocator, &scene_graph );

    // Start multithreading IO
    // Create IO threads at the end
    RunPinnedTaskLoopTask run_pinned_task;
    run_pinned_task.threadNum = task_scheduler.GetNumTaskThreads() - 1;
    run_pinned_task.task_scheduler = &task_scheduler;
    task_scheduler.AddPinnedTask( &run_pinned_task );

    // Send async load task to external thread FILE_IO
    AsynchronousLoadTask async_load_task;
    async_load_task.threadNum = run_pinned_task.threadNum;
    async_load_task.task_scheduler = &task_scheduler;
    async_load_task.async_loader = &async_loader;
    task_scheduler.AddPinnedTask( &async_load_task );

    i64 begin_frame_tick = time_now();
    i64 absolute_begin_frame_tick = begin_frame_tick;

    vec3s light_position = vec3s{ 0.0f, 4.0f, 0.0f };

    float light_radius = 20.0f;
    float light_intensity = 80.0f;

    while ( !window.requested_exit ) {
        ZoneScopedN("RenderLoop");

        // New frame
        if ( !window.minimized ) {
            gpu.new_frame();

            static bool checksz = true;
            if ( async_loader.file_load_requests.size == 0 && checksz ) {
                checksz = false;
                rprint( "Finished uploading textures in %f seconds\n", time_from_seconds( absolute_begin_frame_tick ) );
            }
        }

        window.handle_os_messages();
        input.new_frame();

        if ( window.resized ) {
            gpu.resize( window.width, window.height );
            window.resized = false;
            frame_graph.on_resize( gpu, window.width, window.height );

            game_camera.camera.set_aspect_ratio( window.width * 1.f / window.height );
        }
        // This MUST be AFTER os messages!
        imgui->new_frame();

        const i64 current_tick = time_now();
        f32 delta_time = ( f32 )time_delta_seconds( begin_frame_tick, current_tick );
        begin_frame_tick = current_tick;

        input.update( delta_time );
        game_camera.update( &input, window.width, window.height, delta_time );
        window.center_mouse( game_camera.mouse_dragging );

        {
            ZoneScopedN( "ImGui Recording" );

            if ( ImGui::Begin( "Raptor ImGui" ) ) {
                ImGui::InputFloat( "Scene global scale", &scene->global_scale, 0.001f );
                ImGui::SliderFloat3( "Light position", light_position.raw, -30.0f, 30.0f );
                ImGui::InputFloat( "Light radius", &light_radius );
                ImGui::InputFloat( "Light intensity", &light_intensity );
                ImGui::InputFloat3( "Camera position", game_camera.camera.position.raw );
                ImGui::InputFloat3( "Camera target movement", game_camera.target_movement.raw );
                ImGui::Separator();
                ImGui::Checkbox( "Dynamically recreate descriptor sets", &recreate_per_thread_descriptors );
                ImGui::Checkbox( "Use secondary command buffers", &use_secondary_command_buffers );

                static bool fullscreen = false;
                if ( ImGui::Checkbox( "Fullscreen", &fullscreen ) ) {
                    window.set_fullscreen( fullscreen );
                }

                static i32 present_mode = renderer.gpu->present_mode;
                if ( ImGui::Combo( "Present Mode", &present_mode, raptor::PresentMode::s_value_names, raptor::PresentMode::Count ) ) {
                    renderer.set_presentation_mode( ( raptor::PresentMode::Enum )present_mode );
                }

                frame_graph.add_ui();
            }
            ImGui::End();

            if ( ImGui::Begin( "GPU" ) ) {
                renderer.imgui_draw();

                ImGui::Separator();
                gpu_profiler.imgui_draw();

            }
            ImGui::End();

        }
        {
            ZoneScopedN( "SceneGraphUpdate" );
            scene_graph.update_matrices();
        }

        {
            ZoneScopedN( "UniformBufferUpdate" );

            // Update scene constant buffer
            MapBufferParameters cb_map = { scene->scene_cb, 0, 0 };
            GpuSceneData* uniform_data = ( GpuSceneData* )gpu.map_buffer( cb_map );
            if ( uniform_data ) {
                uniform_data->view_projection = game_camera.camera.view_projection;
                uniform_data->eye = vec4s{ game_camera.camera.position.x, game_camera.camera.position.y, game_camera.camera.position.z, 1.0f };
                uniform_data->light_position = vec4s{ light_position.x, light_position.y, light_position.z, 1.0f };
                uniform_data->light_range = light_radius;
                uniform_data->light_intensity = light_intensity;

                gpu.unmap_buffer( cb_map );
            }

            scene->upload_materials(/* model_scale */);
        }

        if ( !window.minimized ) {

            scene->submit_draw_task( imgui, &gpu_profiler, &task_scheduler );

            gpu.present();
        } else {
            ImGui::Render();
        }

        FrameMark;
    }

    run_pinned_task.execute = false;
    async_load_task.execute = false;

    task_scheduler.WaitforAllAndShutdown();

    vkDeviceWaitIdle( gpu.vulkan_device );

    async_loader.shutdown();

    imgui->shutdown();

    gpu_profiler.shutdown();

    scene_graph.shutdown();

    frame_graph.shutdown();
    frame_graph_builder.shutdown();

    scene->shutdown( &renderer );

    rm.shutdown();
    renderer.shutdown();

    delete scene;

    input.shutdown();
    window.unregister_os_messages_callback( input_os_messages_callback );
    window.shutdown();

    scratch_allocator.shutdown();
    MemoryService::instance()->shutdown();

    return 0;
}
