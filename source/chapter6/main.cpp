
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

#include "external/cglm/struct/vec2.h"
#include "external/cglm/struct/mat2.h"
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
vec4s normalize_plane( vec4s plane ) {
    f32 len = glms_vec3_norm( { plane.x, plane.y, plane.z } );
    return glms_vec4_scale( plane, 1.0f / len );
}

f32 linearize_depth( f32 depth, f32 z_far, f32 z_near ) {
    return z_near * z_far / ( z_far + depth * ( z_near - z_far ) );
}

// CGLM converted method from:
// 2D Polyhedral Bounds of a Clipped, Perspective - Projected 3D Sphere
// By Michael Mara Morgan McGuire
void getBoundsForAxis( const vec3s& a, // Bounding axis (camera space)
                       const vec3s& C, // Sphere center (camera space)
                       float r, // Sphere radius
                       float nearZ, // Near clipping plane (negative)
                        vec3s& L, // Tangent point (camera space)
                        vec3s& U ) { // Tangent point (camera space)

    const vec2s c{ glms_vec3_dot( a, C ), C.z }; // C in the a-z frame
    vec2s bounds[ 2 ]; // In the a-z reference frame
    const float tSquared = glms_vec2_dot( c, c ) - ( r * r );
    const bool cameraInsideSphere = ( tSquared <= 0 );
    // (cos, sin) of angle theta between c and a tangent vector
    vec2s v = cameraInsideSphere ? vec2s{ 0.0f, 0.0f } : vec2s{ glms_vec2_divs( vec2s{ sqrt( tSquared ), r }, glms_vec2_norm( c ) ) };
    // Does the near plane intersect the sphere?
    const bool clipSphere = ( c.y + r >= nearZ );
    // Square root of the discriminant; NaN (and unused)
    // if the camera is in the sphere
    float k = sqrt( ( r * r ) - ( (nearZ - c.y) * (nearZ - c.y) ) );
    for ( int i = 0; i < 2; ++i ) {
        if ( !cameraInsideSphere ) {
            mat2s transform { v.x, -v.y,
                              v.y, v.x };

            bounds[ i ] = glms_mat2_mulv( transform, glms_vec2_scale(c, v.x) );
        }

        const bool clipBound = cameraInsideSphere || ( bounds[ i ].y > nearZ );

        if ( clipSphere && clipBound ) {
            bounds[ i ] = vec2s{ c.x + k, nearZ };
        }

        // Set up for the lower bound
        v.y = -v.y; k = -k;
    }
    // Transform back to camera space
    L = glms_vec3_scale( a, bounds[ 1 ].x );
    L.z = bounds[ 1 ].y;
    U = glms_vec3_scale( a, bounds[ 0 ].x );
    U.z = bounds[ 0 ].y;
}

vec3s project( const mat4s& P, const vec3s& Q ) {
    vec4s v = glms_mat4_mulv( P, vec4s{ Q.x, Q.y, Q.z, 1.0f } );
    v = glms_vec4_divs( v, v.w );
    //return v.xyz() / v.w;
    return vec3s{ v.x, v.y, v.z };
}

//
//
int main( int argc, char** argv ) {

    if ( argc < 2 ) {
        printf( "Usage: chapter6 [path to glTF model]\n");
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
    WindowConfiguration wconf{ 1280, 800, "Raptor Chapter 6", &MemoryService::instance()->system_allocator};
    raptor::Window window;
    window.init( &wconf );

    InputService input;
    input.init( allocator );

    // Callback register: input needs to react to OS messages.
    window.register_os_messages_callback( input_os_messages_callback, &input );

    // graphics
    GpuDeviceCreation dc;
    dc.set_window( window.width, window.height, window.platform_handle ).set_allocator( &MemoryService::instance()->system_allocator )
      .set_num_threads( task_scheduler.GetNumTaskThreads() ).set_linear_allocator( &scratch_allocator );
    GpuDevice gpu;
    gpu.init( dc );

    ResourceManager rm;
    rm.init( allocator, nullptr );

    GpuVisualProfiler gpu_profiler;
    gpu_profiler.init( allocator, 100, dc.gpu_time_queries_per_frame );

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
    TextureResource* dither_texture = nullptr;

    sizet scratch_marker = scratch_allocator.get_marker();

    StringBuffer temporary_name_buffer;
    temporary_name_buffer.init( 1024, &scratch_allocator );

    // Create binaries folders
    cstring shader_binaries_folder = temporary_name_buffer.append_use_f( "%s/shaders /", RAPTOR_DATA_FOLDER );
    if ( !directory_exists(shader_binaries_folder) ) {
        if ( directory_create( shader_binaries_folder ) ) {
            rprint( "Created folder %s\n", shader_binaries_folder );
        }
        else {
            rprint( "Cannot create folder %s\n" );
        }
    }
    strcpy( renderer.resource_cache.binary_data_folder, shader_binaries_folder );
    temporary_name_buffer.clear();

    // Load frame graph and parse gpu techniques
    {
        cstring frame_graph_path = temporary_name_buffer.append_use_f( "%s/%s", RAPTOR_WORKING_FOLDER, "graph.json" );

        frame_graph.parse( frame_graph_path, &scratch_allocator );
        frame_graph.compile();

        render_resources_loader.init( &renderer, &scratch_allocator, &frame_graph );

        // TODO: add this to render graph itself.
        // Add utility textures (dithering, ...)
        temporary_name_buffer.clear();
        cstring dither_texture_path = temporary_name_buffer.append_use_f( "%s/BayerDither4x4.png", RAPTOR_DATA_FOLDER );
        dither_texture = render_resources_loader.load_texture( dither_texture_path, false );

        // Parse techniques
        GpuTechniqueCreation gtc;
        const bool use_shader_cache = true;
        auto parse_technique = [ & ]( cstring technique_name ) {
            temporary_name_buffer.clear();
            cstring path = temporary_name_buffer.append_use_f( "%s/%s", RAPTOR_SHADER_FOLDER, technique_name );
            render_resources_loader.load_gpu_technique( path, use_shader_cache );
        };

        cstring techniques[] = { "meshlet.json", "fullscreen.json", "main.json",
                                 "pbr_lighting.json", "dof.json", "cloth.json", "debug.json",
                                 "culling.json" };

        const sizet num_techniques = ArraySize( techniques );
        for ( sizet t = 0; t < num_techniques; ++t ) {
            parse_technique( techniques[ t ] );
        }
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

    scene->use_meshlets = gpu.mesh_shaders_extension_present;
    scene->init( file_name, file_base_path, allocator, &scratch_allocator, &async_loader );

    // NOTE(marco): restore working directory
    directory_change( cwd.path );

    FrameRenderer frame_renderer;
    frame_renderer.init( allocator, &renderer, &frame_graph, &scene_graph, scene );
    frame_renderer.prepare_draws( &scratch_allocator );

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

    f32 light_radius = 20.0f;
    f32 light_intensity = 80.0f;

    f32 spring_stiffness = 10000.0f;
    f32 spring_damping = 5000.0f;
    f32 air_density = 2.0f;
    bool reset_simulation = false;
    vec3s wind_direction{ -2.0f, 0.0f, 0.0f };

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
            renderer.resize_swapchain( window.width, window.height );
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

        static f32 animation_speed_multiplier = 0.05f;
        static bool enable_frustum_cull_meshes = true;
        static bool enable_frustum_cull_meshlets = true;
        static bool enable_occlusion_cull_meshes = true;
        static bool enable_occlusion_cull_meshlets = true;
        static bool freeze_occlusion_camera = false;
        static mat4s projection_transpose{ };

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
                ImGui::InputFloat3( "Wind direction", wind_direction.raw );
                ImGui::InputFloat( "Air density", &air_density );
                ImGui::InputFloat( "Spring stiffness", &spring_stiffness );
                ImGui::InputFloat( "Spring damping", &spring_damping );
                ImGui::Checkbox( "Reset simulation", &reset_simulation );
                ImGui::Separator();

                static bool enable_meshlets = false;
                enable_meshlets = scene->use_meshlets && gpu.mesh_shaders_extension_present;
                ImGui::Checkbox( "Use meshlets", &enable_meshlets );
                scene->use_meshlets = enable_meshlets;
                ImGui::Checkbox( "Use frustum cull for meshes", &enable_frustum_cull_meshes );
                ImGui::Checkbox( "Use frustum cull for meshlets", &enable_frustum_cull_meshlets );
                ImGui::Checkbox( "Use occlusion cull for meshes", &enable_occlusion_cull_meshes );
                ImGui::Checkbox( "Use occlusion cull for meshlets", &enable_occlusion_cull_meshlets );
                ImGui::Checkbox( "Freeze occlusion camera", &freeze_occlusion_camera );
                ImGui::Checkbox( "Show Debug GPU Draws", &scene->show_debug_gpu_draws );

                ImGui::Checkbox( "Dynamically recreate descriptor sets", &recreate_per_thread_descriptors );
                ImGui::Checkbox( "Use secondary command buffers", &use_secondary_command_buffers );

                ImGui::SliderFloat( "Animation Speed Multiplier", &animation_speed_multiplier, 0.0f, 10.0f );

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

            if ( ImGui::Begin( "Scene" ) ) {

                static u32 selected_node = u32_max;

                ImGui::Text( "Selected node %u", selected_node );
                if ( selected_node < scene_graph.nodes_hierarchy.size ) {

                    mat4s& local_transform = scene_graph.local_matrices[ selected_node ];
                    f32 position[ 3 ]{ local_transform.m30, local_transform.m31, local_transform.m32 };

                    if ( ImGui::SliderFloat3( "Node Position", position, -100.0f, 100.0f ) ) {
                        local_transform.m30 = position[ 0 ];
                        local_transform.m31 = position[ 1 ];
                        local_transform.m32 = position[ 2 ];

                        scene_graph.set_local_matrix( selected_node, local_transform );
                    }
                    ImGui::Separator();
                }

                for ( u32 n = 0; n < scene_graph.nodes_hierarchy.size; ++n ) {
                    const SceneGraphNodeDebugData& node_debug_data = scene_graph.nodes_debug_data[ n ];
                    if ( ImGui::Selectable( node_debug_data.name ? node_debug_data.name : "-", n == selected_node) ) {
                        selected_node = n;
                    }
                }
            }
            ImGui::End();

            if ( ImGui::Begin( "GPU" ) ) {
                renderer.imgui_draw();

                ImGui::Separator();
                ImGui::Text( "Cpu Time %fms", delta_time * 1000.f );
                gpu_profiler.imgui_draw();

            }
            ImGui::End();

            if ( ImGui::Begin( "Textures Debug" ) ) {
                const ImVec2 window_size = ImGui::GetWindowSize();

                FrameGraphResource* resource = frame_graph.get_resource( "depth" );

                ImGui::Image( ( ImTextureID )&resource->resource_info.texture.handle, window_size );

            }
            ImGui::End();

        }
        {
            ZoneScopedN( "AnimationsUpdate" );
            scene->update_animations( delta_time * animation_speed_multiplier );
        }
        {
            ZoneScopedN( "SceneGraphUpdate" );
            scene_graph.update_matrices();
        }
        {
            ZoneScopedN( "JointsUpdate" );
            scene->update_joints();
        }

        {
            ZoneScopedN( "Gpu Buffers Update" );

            GpuSceneData& scene_data = scene->scene_data;
            scene_data.previous_view_projection = scene_data.view_projection;   // Cache previous view projection
            scene_data.view_projection = game_camera.camera.view_projection;
            scene_data.inverse_view_projection = glms_mat4_inv( game_camera.camera.view_projection );
            scene_data.world_to_camera = game_camera.camera.view;
            scene_data.eye = vec4s{ game_camera.camera.position.x, game_camera.camera.position.y, game_camera.camera.position.z, 1.0f };
            scene_data.light_position = vec4s{ light_position.x, light_position.y, light_position.z, 1.0f };
            scene_data.light_range = light_radius;
            scene_data.light_intensity = light_intensity;
            scene_data.dither_texture_index = dither_texture ? dither_texture->handle.index : 0;

            scene_data.z_near = game_camera.camera.near_plane;
            scene_data.z_far = game_camera.camera.far_plane;
            scene_data.projection_00 = game_camera.camera.projection.m00;
            scene_data.projection_11 = game_camera.camera.projection.m11;
            scene_data.frustum_cull_meshes = enable_frustum_cull_meshes ? 1 : 0;
            scene_data.frustum_cull_meshlets = enable_frustum_cull_meshlets ? 1 : 0;
            scene_data.occlusion_cull_meshes = enable_occlusion_cull_meshes ? 1 : 0;
            scene_data.occlusion_cull_meshlets = enable_occlusion_cull_meshlets ? 1 : 0;
            scene_data.freeze_occlusion_camera = freeze_occlusion_camera ? 1 : 0;

            scene_data.resolution_x = gpu.swapchain_width * 1.f;
            scene_data.resolution_y = gpu.swapchain_height * 1.f;
            scene_data.aspect_ratio = gpu.swapchain_width * 1.f / gpu.swapchain_height;

            // Frustum computations
            if ( !freeze_occlusion_camera ) {
                scene_data.eye_debug = scene_data.eye;
                scene_data.world_to_camera_debug = scene_data.world_to_camera;
                scene_data.view_projection_debug = scene_data.view_projection;
                projection_transpose = glms_mat4_transpose( game_camera.camera.projection );
            }

            scene_data.frustum_planes[ 0 ] = normalize_plane( glms_vec4_add( projection_transpose.col[ 3 ], projection_transpose.col[ 0 ] ) ); // x + w  < 0;
            scene_data.frustum_planes[ 1 ] = normalize_plane( glms_vec4_sub( projection_transpose.col[ 3 ], projection_transpose.col[ 0 ] ) ); // x - w  < 0;
            scene_data.frustum_planes[ 2 ] = normalize_plane( glms_vec4_add( projection_transpose.col[ 3 ], projection_transpose.col[ 1 ] ) ); // y + w  < 0;
            scene_data.frustum_planes[ 3 ] = normalize_plane( glms_vec4_sub( projection_transpose.col[ 3 ], projection_transpose.col[ 1 ] ) ); // y - w  < 0;
            scene_data.frustum_planes[ 4 ] = normalize_plane( glms_vec4_add( projection_transpose.col[ 3 ], projection_transpose.col[ 2 ] ) ); // z + w  < 0;
            scene_data.frustum_planes[ 5 ] = normalize_plane( glms_vec4_sub( projection_transpose.col[ 3 ], projection_transpose.col[ 2 ] ) ); // z - w  < 0;

            // Update scene constant buffer
            MapBufferParameters scene_cb_map = { scene->scene_cb, 0, 0 };
            GpuSceneData* gpu_scene_data = ( GpuSceneData* )gpu.map_buffer( scene_cb_map );
            if ( gpu_scene_data ) {
                memcpy( gpu_scene_data, &scene->scene_data, sizeof( GpuSceneData ) );

                gpu.unmap_buffer( scene_cb_map );
            }

            // Test math to check correctness.
            if( false )
            {
                vec4s pos{ -14.5f, 1.28f, 0.f, 1.f };
                f32 radius = 0.5f;
                vec4s view_space_pos = glms_mat4_mulv( game_camera.camera.view, pos );
                bool camera_visible = view_space_pos.z < radius + game_camera.camera.near_plane;

                // X is positive, then it returns the same values as the longer method.
                vec2s cx{ view_space_pos.x, -view_space_pos.z };
                vec2s vx{ sqrtf( glms_vec2_dot( cx, cx ) - ( radius * radius )), radius };
                mat2s xtransf_min{ vx.x, vx.y, -vx.y, vx.x };
                vec2s minx = glms_mat2_mulv( xtransf_min, cx );
                mat2s xtransf_max{ vx.x, -vx.y, vx.y, vx.x };
                vec2s maxx = glms_mat2_mulv( xtransf_max, cx );

                vec2s cy{ -view_space_pos.y, -view_space_pos.z };
                vec2s vy{ sqrtf( glms_vec2_dot( cy, cy ) - ( radius * radius ) ), radius };
                mat2s ytransf_min{ vy.x, vy.y, -vy.y, vy.x };
                vec2s miny = glms_mat2_mulv( ytransf_min, cy );
                mat2s ytransf_max{ vy.x, -vy.y, vy.y, vy.x };
                vec2s maxy = glms_mat2_mulv( ytransf_max, cy );

                vec4s aabb{minx.x / minx.y * game_camera.camera.projection.m00, miny.x / miny.y * game_camera.camera.projection.m11,
                           maxx.x / maxx.y * game_camera.camera.projection.m00, maxy.x / maxy.y * game_camera.camera.projection.m11};
                vec4s aabb2{ aabb.x * 0.5f + 0.5f, aabb.w * -0.5f + 0.5f, aabb.z * 0.5f + 0.5f, aabb.y * -0.5f + 0.5f };

                vec3s left, right, top, bottom;
                getBoundsForAxis( vec3s{ 1,0,0 }, { view_space_pos.x, view_space_pos.y, view_space_pos.z }, radius, game_camera.camera.near_plane, left, right );
                getBoundsForAxis( vec3s{ 0,1,0 }, { view_space_pos.x, view_space_pos.y, view_space_pos.z }, radius, game_camera.camera.near_plane, top, bottom );

                left = project( game_camera.camera.projection, left );
                right = project( game_camera.camera.projection, right );
                top = project( game_camera.camera.projection, top );
                bottom = project( game_camera.camera.projection, bottom );

                vec4s clip_space_pos = glms_mat4_mulv( game_camera.camera.projection, view_space_pos );

                // left,right,bottom and top are in clip space (-1,1). Convert to 0..1 for UV, as used from the optimized version to read the depth pyramid.
                rprint( "Camera visible %u, x %f, %f, widh %f --- %f,%f width %f\n", camera_visible ? 1 : 0, aabb2.x, aabb2.z, aabb2.z - aabb2.x, left.x * 0.5 + 0.5, right.x * 0.5 + 0.5, (left.x - right.x) * 0.5 );
                rprint( "y %f, %f, height %f --- %f,%f height %f\n", aabb2.y, aabb2.w, aabb2.w - aabb2.y, top.y * 0.5 + 0.5, bottom.y * 0.5 + 0.5, ( top.y - bottom.y ) * 0.5 );
            }

            frame_renderer.upload_gpu_data();
        }

        if ( !window.minimized ) {
            DrawTask draw_task;
            draw_task.init( renderer.gpu, &frame_graph, &renderer, imgui, &gpu_profiler, scene, &frame_renderer );
            task_scheduler.AddTaskSetToPipe( &draw_task );

            CommandBuffer* async_compute_command_buffer = nullptr;
            {
                ZoneScopedN( "PhysicsUpdate" );
                async_compute_command_buffer = scene->update_physics( delta_time, air_density, spring_stiffness, spring_damping, wind_direction, reset_simulation );
                reset_simulation = false;
            }

            task_scheduler.WaitforTaskSet( &draw_task );

            // Avoid using the same command buffer
            renderer.add_texture_update_commands( ( draw_task.thread_id + 1 ) % task_scheduler.GetNumTaskThreads() );
            gpu.present( async_compute_command_buffer );
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
    frame_renderer.shutdown();

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
