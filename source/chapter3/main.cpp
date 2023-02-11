
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

#include "external/cglm/struct/mat3.h"
#include "external/cglm/struct/mat4.h"
#include "external/cglm/struct/cam.h"
#include "external/cglm/struct/affine.h"
#include "external/enkiTS/TaskScheduler.h"

#include "foundation/file.hpp"
#include "foundation/gltf.hpp"
#include "foundation/numerics.hpp"
#include "foundation/time.hpp"
#include "foundation/resource_manager.hpp"

#include "external/imgui/imgui.h"
#include "external/stb_image.h"
#include "external/tracy/tracy/Tracy.hpp"

#include <assimp/cimport.h>        // Plain-C interface
#include <assimp/scene.h>          // Output data structure
#include <assimp/postprocess.h>    // Post processing flags

#include <stdio.h>
#include <stdlib.h>

///////////////////////////////////////

struct glTFScene;

static const u16 INVALID_TEXTURE_INDEX = ~0u;

raptor::BufferHandle                    scene_cb;

struct MeshDraw {
    raptor::Material*       material;

    raptor::BufferHandle    index_buffer;
    raptor::BufferHandle    position_buffer;
    raptor::BufferHandle    tangent_buffer;
    raptor::BufferHandle    normal_buffer;
    raptor::BufferHandle    texcoord_buffer;
    raptor::BufferHandle    material_buffer;

    VkIndexType index_type;
    u32         index_offset;

    u32         position_offset;
    u32         tangent_offset;
    u32         normal_offset;
    u32         texcoord_offset;

    u32         primitive_count;

    // Indices used for bindless textures.
    u16         diffuse_texture_index;
    u16         roughness_texture_index;
    u16         normal_texture_index;
    u16         occlusion_texture_index;

    vec4s       base_color_factor;
    vec4s       metallic_roughness_occlusion_factor;
    vec3s       scale;

    f32         alpha_cutoff;
    u32         flags;

    raptor::DescriptorSetHandle descriptor_set;
}; // struct MeshDraw

//
//
enum DrawFlags {
    DrawFlags_AlphaMask     = 1 << 0,
}; // enum DrawFlags

//
//
struct UniformData {
    mat4s       vp;
    vec4s       eye;
    vec4s       light;
    float       light_range;
    float       light_intensity;
}; // struct UniformData

//
//
struct MeshData {
    mat4s       m;
    mat4s       inverseM;

    u32         textures[ 4 ]; // diffuse, roughness, normal, occlusion
    vec4s       base_color_factor;
    vec4s       metallic_roughness_occlusion_factor; // metallic, roughness, occlusion
    float       alpha_cutoff;
    float       padding_[3];
    u32         flags;
}; // struct MeshData

//
//
struct GpuEffect {

    raptor::PipelineHandle          pipeline_cull;
    raptor::PipelineHandle          pipeline_no_cull;

}; // struct GpuEffect

//
//
struct ObjMaterial {
    vec4s                                   diffuse;
    vec3s                                   ambient;
    vec3s                                   specular;
    f32                                     specular_exp;

    f32                                     transparency;

    u16                                     diffuse_texture_index = INVALID_TEXTURE_INDEX;
    u16                                     normal_texture_index  = INVALID_TEXTURE_INDEX;
};

struct ObjDraw {
    raptor::BufferHandle                    geometry_buffer_cpu;
    raptor::BufferHandle                    geometry_buffer_gpu;
    raptor::BufferHandle                    mesh_buffer;

    raptor::DescriptorSetHandle             descriptor_set;

    u32                                     index_offset;
    u32                                     position_offset;
    u32                                     tangent_offset;
    u32                                     normal_offset;
    u32                                     texcoord_offset;

    u32                                     primitive_count;

    vec4s                                   diffuse;
    vec3s                                   ambient;
    vec3s                                   specular;
    f32                                     specular_exp;
    f32                                     transparency;

    u16                                     diffuse_texture_index = INVALID_TEXTURE_INDEX;
    u16                                     normal_texture_index  = INVALID_TEXTURE_INDEX;

    u32                                     uploads_queued = 0;
    // TODO(marco): this should be an atomic value
    u32                                     uploads_completed = 0;

    raptor::Material*                       material;
};

struct ObjGpuData {
    mat4s                                   m;
    mat4s                                   inverseM;

    u32                                     textures[4];
    vec4s                                   diffuse;
    vec3s                                   specular;
    f32                                     specular_exp;
    vec3s                                   ambient;
};

//
//
struct FileLoadRequest {

    char                            path[ 512 ];
    raptor::TextureHandle           texture     = raptor::k_invalid_texture;
    raptor::BufferHandle            buffer      = raptor::k_invalid_buffer;
}; // struct FileLoadRequest

//
//
struct UploadRequest {

    void*                           data        = nullptr;
    u32*                            completed   = nullptr;
    raptor::TextureHandle           texture     = raptor::k_invalid_texture;
    raptor::BufferHandle            cpu_buffer  = raptor::k_invalid_buffer;
    raptor::BufferHandle            gpu_buffer  = raptor::k_invalid_buffer;
}; // struct UploadRequest

//
//
struct AsynchronousLoader {

    void                            init( raptor::Renderer* renderer, enki::TaskScheduler* task_scheduler, raptor::Allocator* resident_allocator );
    void                            update( raptor::Allocator* scratch_allocator );
    void                            shutdown();

    void                            request_texture_data( cstring filename, raptor::TextureHandle texture );
    void                            request_buffer_upload( void* data, raptor::BufferHandle buffer );
    void                            request_buffer_copy( raptor::BufferHandle src, raptor::BufferHandle dst, u32* completed );

    raptor::Allocator*              allocator       = nullptr;
    raptor::Renderer*               renderer        = nullptr;
    enki::TaskScheduler*            task_scheduler  = nullptr;

    raptor::Array<FileLoadRequest>  file_load_requests;
    raptor::Array<UploadRequest>    upload_requests;

    raptor::Buffer*                 staging_buffer  = nullptr;

    std::atomic_size_t              staging_buffer_offset;
    raptor::TextureHandle           texture_ready;
    raptor::BufferHandle            cpu_buffer_ready;
    raptor::BufferHandle            gpu_buffer_ready;
    u32*                            completed;

    VkCommandPool                   command_pools[ raptor::GpuDevice::k_max_frames ];
    raptor::CommandBuffer           command_buffers[ raptor::GpuDevice::k_max_frames ];
    VkSemaphore                     transfer_complete_semaphore;
    VkFence                         transfer_fence;

}; // struct AsynchonousLoader


static void         get_mesh_vertex_buffer( glTFScene& scene, i32 accessor_index, raptor::BufferHandle& out_buffer_handle, u32& out_buffer_offset );
static bool         get_mesh_material( raptor::Renderer& renderer, glTFScene& scene, raptor::glTF::Material& material, MeshDraw& mesh_draw );
static void         upload_material( MeshData& mesh_data, const MeshDraw& mesh_draw, const f32 global_scale );
static void         upload_material( ObjGpuData& mesh_data, const ObjDraw& mesh_draw, const f32 global_scale );
static void         input_os_messages_callback( void* os_event, void* user_data );
static int          gltf_mesh_material_compare( const void* a, const void* b );
static int          obj_mesh_material_compare( const void* a, const void* b );


// Input callback
static void input_os_messages_callback( void* os_event, void* user_data ) {
    raptor::InputService* input = ( raptor::InputService* )user_data;
    input->on_event( os_event );
}

//
//
static void upload_material( MeshData& mesh_data, const MeshDraw& mesh_draw, const f32 global_scale ) {
    mesh_data.textures[ 0 ] = mesh_draw.diffuse_texture_index;
    mesh_data.textures[ 1 ] = mesh_draw.roughness_texture_index;
    mesh_data.textures[ 2 ] = mesh_draw.normal_texture_index;
    mesh_data.textures[ 3 ] = mesh_draw.occlusion_texture_index;
    mesh_data.base_color_factor = mesh_draw.base_color_factor;
    mesh_data.metallic_roughness_occlusion_factor = mesh_draw.metallic_roughness_occlusion_factor;
    mesh_data.alpha_cutoff = mesh_draw.alpha_cutoff;
    mesh_data.flags = mesh_draw.flags;

    // NOTE: for left-handed systems (as defined in cglm) need to invert positive and negative Z.
    mat4s model = glms_scale_make( glms_vec3_mul( mesh_draw.scale, { global_scale, global_scale, -global_scale } ) );
    mesh_data.m = model;
    mesh_data.inverseM = glms_mat4_inv( glms_mat4_transpose( model ) );
}

static bool recreate_per_thread_descriptors = false;

//
//
static void upload_material( ObjGpuData& mesh_data, const ObjDraw& mesh_draw, const f32 global_scale ) {
    mesh_data.textures[ 0 ] = mesh_draw.diffuse_texture_index;
    mesh_data.textures[ 1 ] = mesh_draw.normal_texture_index;
    mesh_data.textures[ 2 ] = 0;
    mesh_data.textures[ 3 ] = 0;
    mesh_data.diffuse = mesh_draw.diffuse;
    mesh_data.specular = mesh_draw.specular;
    mesh_data.specular_exp = mesh_draw.specular_exp;
    mesh_data.ambient = mesh_draw.ambient;

    mat4s model = glms_scale_make( vec3s{ global_scale, global_scale, global_scale } );
    mesh_data.m = model;
    mesh_data.inverseM = glms_mat4_inv( glms_mat4_transpose( model ) );
}

//
//
static void draw_mesh( raptor::Renderer& renderer, raptor::CommandBuffer* gpu_commands, MeshDraw& mesh_draw ) {

    gpu_commands->bind_vertex_buffer( mesh_draw.position_buffer, 0, mesh_draw.position_offset );
    gpu_commands->bind_vertex_buffer( mesh_draw.tangent_buffer, 1, mesh_draw.tangent_offset );
    gpu_commands->bind_vertex_buffer( mesh_draw.normal_buffer, 2, mesh_draw.normal_offset );
    gpu_commands->bind_vertex_buffer( mesh_draw.texcoord_buffer, 3, mesh_draw.texcoord_offset );
    gpu_commands->bind_index_buffer( mesh_draw.index_buffer, mesh_draw.index_offset, mesh_draw.index_type );

    if ( recreate_per_thread_descriptors ) {
        raptor::DescriptorSetCreation ds_creation{};
        ds_creation.buffer( scene_cb, 0 ).buffer( mesh_draw.material_buffer, 1 );
        raptor::DescriptorSetHandle descriptor_set = renderer.create_descriptor_set( gpu_commands, mesh_draw.material, ds_creation );

        gpu_commands->bind_local_descriptor_set( &descriptor_set, 1, nullptr, 0 );
    }
    else {
        gpu_commands->bind_descriptor_set( &mesh_draw.descriptor_set, 1, nullptr, 0 );
    }

    gpu_commands->draw_indexed( raptor::TopologyType::Triangle, mesh_draw.primitive_count, 1, 0, 0, 0 );
}

//
//
static void draw_mesh( raptor::Renderer& renderer, raptor::CommandBuffer* gpu_commands, ObjDraw& mesh_draw ) {
    ZoneScoped;

    gpu_commands->bind_vertex_buffer( mesh_draw.geometry_buffer_gpu, 0, mesh_draw.position_offset );
    gpu_commands->bind_vertex_buffer( mesh_draw.geometry_buffer_gpu, 1, mesh_draw.tangent_offset );
    gpu_commands->bind_vertex_buffer( mesh_draw.geometry_buffer_gpu, 2, mesh_draw.normal_offset );
    gpu_commands->bind_vertex_buffer( mesh_draw.geometry_buffer_gpu, 3, mesh_draw.texcoord_offset );
    gpu_commands->bind_index_buffer( mesh_draw.geometry_buffer_gpu, mesh_draw.index_offset, VK_INDEX_TYPE_UINT32 );

    if ( recreate_per_thread_descriptors ) {
        raptor::DescriptorSetCreation ds_creation{};
        ds_creation.buffer( scene_cb, 0 ).buffer( mesh_draw.geometry_buffer_gpu, 1 );
        raptor::DescriptorSetHandle descriptor_set = renderer.create_descriptor_set( gpu_commands, mesh_draw.material, ds_creation );

        gpu_commands->bind_local_descriptor_set( &descriptor_set, 1, nullptr, 0 );
    }
    else {
        gpu_commands->bind_descriptor_set( &mesh_draw.descriptor_set, 1, nullptr, 0 );
    }

    gpu_commands->draw_indexed( raptor::TopologyType::Triangle, mesh_draw.primitive_count, 1, 0, 0, 0 );
}

//
//
struct Scene {

    virtual void                            load( cstring filename, cstring path, raptor::Allocator* resident_allocator, raptor::StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) { };
    virtual void                            free_gpu_resources( raptor::Renderer* renderer ) { };
    virtual void                            unload( raptor::Renderer* renderer ) { };

    virtual void                            prepare_draws( raptor::Renderer* renderer, raptor::StackAllocator* scratch_allocator ) { };

    virtual void                            upload_materials( float model_scale ) { };
    virtual void                            submit_draw_task( raptor::ImGuiService* imgui, raptor::GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler ) { };
};

//
//
struct glTFScene : public Scene {

    void                                    load( cstring filename, cstring path, raptor::Allocator* resident_allocator, raptor::StackAllocator* temp_allocator, AsynchronousLoader* async_loader );
    void                                    free_gpu_resources( raptor::Renderer* renderer );
    void                                    unload( raptor::Renderer* renderer );

    void                                    prepare_draws( raptor::Renderer* renderer, raptor::StackAllocator* scratch_allocator );
    void                                    upload_materials( float model_scale );
    void                                    submit_draw_task( raptor::ImGuiService* imgui, raptor::GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler );

    raptor::Array<MeshDraw>                 mesh_draws;

    // All graphics resources used by the scene
    raptor::Array<raptor::TextureResource>  images;
    raptor::Array<raptor::SamplerResource>  samplers;
    raptor::Array<raptor::BufferResource>   buffers;

    raptor::glTF::glTF                      gltf_scene; // Source gltf scene

    raptor::Renderer*                       renderer;

}; // struct GltfScene

//
//
struct ObjScene :  public Scene {

    void                                    load( cstring filename, cstring path, raptor::Allocator* resident_allocator, raptor::StackAllocator* temp_allocator, AsynchronousLoader* async_loader_ );
    void                                    free_gpu_resources( raptor::Renderer* renderer );
    void                                    unload( raptor::Renderer* renderer );

    void                                    prepare_draws( raptor::Renderer* renderer, raptor::StackAllocator* scratch_allocator );
    void                                    upload_materials( float model_scale );
    void                                    submit_draw_task( raptor::ImGuiService* imgui, raptor::GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler );

    u32                                     load_texture( cstring texture_path, cstring path, raptor::StackAllocator* temp_allocator );

    raptor::Array<ObjDraw>                  mesh_draws;

    // All graphics resources used by the scene
    raptor::Array<ObjMaterial>              materials;
    raptor::Array<raptor::TextureResource>  images;
    raptor::SamplerResource*                sampler;

    AsynchronousLoader*                     async_loader;
    raptor::Renderer*                       renderer;

}; // struct ObjScene

// DrawTask ///////////////////////////////////////////////////////////////

//
//
struct glTFDrawTask : public enki::ITaskSet {

    raptor::GpuDevice*              gpu         = nullptr;
    raptor::Renderer*               renderer    = nullptr;
    raptor::ImGuiService*           imgui       = nullptr;
    raptor::GPUProfiler*            gpu_profiler = nullptr;
    glTFScene*                      scene       = nullptr;
    u32                             thread_id   = 0;

    void init( raptor::GpuDevice* gpu_, raptor::Renderer* renderer_,
               raptor::ImGuiService* imgui_, raptor::GPUProfiler* gpu_profiler_,
               glTFScene* scene_ ) {
        gpu = gpu_;
        renderer = renderer_;
        imgui = imgui_;
        gpu_profiler = gpu_profiler_;
        scene = scene_;
    }

    void ExecuteRange( enki::TaskSetPartition range_, uint32_t threadnum_ ) override {
        ZoneScoped;

        using namespace raptor;

        thread_id = threadnum_;

        //rprint( "Executing draw task from thread %u\n", threadnum_ );
        // TODO: improve getting a command buffer/pool
        raptor::CommandBuffer* gpu_commands = gpu->get_command_buffer( threadnum_, true );
        gpu_commands->push_marker( "Frame" );

        gpu_commands->clear( 0.3f, 0.3f, 0.3f, 1.f );
        gpu_commands->clear_depth_stencil( 1.0f, 0 );
        gpu_commands->bind_pass( gpu->get_swapchain_pass(), false );
        gpu_commands->set_scissor( nullptr );
        gpu_commands->set_viewport( nullptr );

        Material* last_material = nullptr;
        // TODO(marco): loop by material so that we can deal with multiple passes
        for ( u32 mesh_index = 0; mesh_index < scene->mesh_draws.size; ++mesh_index ) {
            MeshDraw& mesh_draw = scene->mesh_draws[ mesh_index ];

            if ( mesh_draw.material != last_material ) {
                PipelineHandle pipeline = renderer->get_pipeline( mesh_draw.material );

                gpu_commands->bind_pipeline( pipeline );

                last_material = mesh_draw.material;
            }

            draw_mesh( *renderer, gpu_commands, mesh_draw );
        }

        imgui->render( *gpu_commands, false );

        gpu_commands->pop_marker();

        gpu_profiler->update( *gpu );

        // Send commands to GPU
        gpu->queue_command_buffer( gpu_commands );
    }

}; // struct DrawTask

//
//
struct SecondaryDrawTask : public enki::ITaskSet {

    raptor::Renderer*               renderer    = nullptr;
    ObjScene*                       scene       = nullptr;
    raptor::CommandBuffer*          parent      = nullptr;
    raptor::CommandBuffer*          cb          = nullptr;
    u32                             start       = 0;
    u32                             end         = 0;

    void init ( ObjScene* scene_, raptor::Renderer* renderer_, raptor::CommandBuffer* parent_, u32 start_, u32 end_ ) {
        renderer = renderer_;
        scene = scene_;
        parent = parent_;
        start = start_;
        end = end_;
    }

    void ExecuteRange( enki::TaskSetPartition range_, uint32_t threadnum_ ) override {
        using namespace raptor;

        ZoneScoped;

        cb = renderer->gpu->get_secondary_command_buffer( threadnum_ );

        // TODO(marco): loop by material so that we can deal with multiple passes
        cb->begin_secondary( parent->current_render_pass );

        cb->set_scissor( nullptr );
        cb->set_viewport( nullptr );

        Material* last_material = nullptr;
        for ( u32 mesh_index = start; mesh_index < end; ++mesh_index ) {
            ObjDraw& mesh_draw = scene->mesh_draws[ mesh_index ];

            if ( mesh_draw.uploads_queued != mesh_draw.uploads_completed ) {
                continue;
            }

            if ( mesh_draw.material != last_material ) {
                PipelineHandle pipeline = renderer->get_pipeline( mesh_draw.material );

                cb->bind_pipeline( pipeline );

                last_material = mesh_draw.material;
            }

            draw_mesh( *renderer, cb, mesh_draw );
        }

        cb->end();
    }
};

//
//
struct ObjDrawTask : public enki::ITaskSet {

    enki::TaskScheduler*            task_scheduler = nullptr;
    raptor::GpuDevice*              gpu         = nullptr;
    raptor::Renderer*               renderer    = nullptr;
    raptor::ImGuiService*           imgui       = nullptr;
    raptor::GPUProfiler*            gpu_profiler = nullptr;
    ObjScene*                       scene       = nullptr;
    u32                             thread_id   = 0;
    bool                            use_secondary = false;

    void init( enki::TaskScheduler* task_scheduler_, raptor::GpuDevice* gpu_, raptor::Renderer* renderer_,
               raptor::ImGuiService* imgui_, raptor::GPUProfiler* gpu_profiler_,
               ObjScene* scene_, bool use_secondary_ ) {
        task_scheduler = task_scheduler_;
        gpu = gpu_;
        renderer = renderer_;
        imgui = imgui_;
        gpu_profiler = gpu_profiler_;
        scene = scene_;
        use_secondary = use_secondary_;
    }

    void ExecuteRange( enki::TaskSetPartition range_, uint32_t threadnum_ ) override {
        ZoneScoped;

        using namespace raptor;

        thread_id = threadnum_;

        //rprint( "Executing draw task from thread %u\n", threadnum_ );
        // TODO: improve getting a command buffer/pool
        raptor::CommandBuffer* gpu_commands = gpu->get_command_buffer( threadnum_, true );
        gpu_commands->push_marker( "Frame" );

        gpu_commands->clear( 0.3f, 0.3f, 0.3f, 1.f );
        gpu_commands->clear_depth_stencil( 1.0f, 0 );
        gpu_commands->set_scissor( nullptr );
        gpu_commands->set_viewport( nullptr );
        gpu_commands->bind_pass( gpu->get_swapchain_pass(), use_secondary);

        if ( use_secondary ) {
            static const u32 parallel_recordings = 4;
            u32 draws_per_secondary = scene->mesh_draws.size / parallel_recordings;
            u32 offset = draws_per_secondary * parallel_recordings;

            SecondaryDrawTask secondary_tasks[ parallel_recordings ]{ };

            u32 start = 0;
            for ( u32 secondary_index = 0; secondary_index < parallel_recordings; ++secondary_index ) {
                SecondaryDrawTask& task = secondary_tasks[ secondary_index ];

                task.init( scene, renderer, gpu_commands, start, start + draws_per_secondary );
                start += draws_per_secondary;

                task_scheduler->AddTaskSetToPipe( &task );
            }

            CommandBuffer* cb = renderer->gpu->get_secondary_command_buffer( threadnum_ );

            cb->begin_secondary( gpu_commands->current_render_pass );

            cb->set_scissor( nullptr );
            cb->set_viewport( nullptr );

            Material* last_material = nullptr;
            // TODO(marco): loop by material so that we can deal with multiple passes
            for ( u32 mesh_index = offset; mesh_index < scene->mesh_draws.size; ++mesh_index ) {
                ObjDraw& mesh_draw = scene->mesh_draws[ mesh_index ];

                if ( mesh_draw.uploads_queued != mesh_draw.uploads_completed ) {
                    continue;
                }

                if ( mesh_draw.material != last_material ) {
                    PipelineHandle pipeline = renderer->get_pipeline( mesh_draw.material );

                    cb->bind_pipeline( pipeline );

                    last_material = mesh_draw.material;
                }

                draw_mesh( *renderer, cb, mesh_draw );
            }


            for ( u32 secondary_index = 0; secondary_index < parallel_recordings; ++secondary_index ) {
                SecondaryDrawTask& task = secondary_tasks[ secondary_index ];
                task_scheduler->WaitforTask( &task );

                vkCmdExecuteCommands( gpu_commands->vk_command_buffer, 1, &task.cb->vk_command_buffer );
            }

            // NOTE(marco): ImGui also has to use a secondary command buffer, vkCmdExecuteCommands is
            // the only allowed command. We don't need this if we use a different render pass above
            imgui->render( *cb, true );

            cb->end();

            vkCmdExecuteCommands( gpu_commands->vk_command_buffer, 1, &cb->vk_command_buffer );

            gpu_commands->end_current_render_pass();
        } else {
            Material* last_material = nullptr;
            // TODO(marco): loop by material so that we can deal with multiple passes
            for ( u32 mesh_index = 0; mesh_index < scene->mesh_draws.size; ++mesh_index ) {
                ObjDraw& mesh_draw = scene->mesh_draws[ mesh_index ];

                if ( mesh_draw.uploads_queued != mesh_draw.uploads_completed ) {
                    continue;
                }

                if ( mesh_draw.material != last_material ) {
                    PipelineHandle pipeline = renderer->get_pipeline( mesh_draw.material );

                    gpu_commands->bind_pipeline( pipeline );

                    last_material = mesh_draw.material;
                }

                draw_mesh( *renderer, gpu_commands, mesh_draw );
            }

            imgui->render( *gpu_commands, false );
        }

        gpu_commands->pop_marker();

        gpu_profiler->update( *gpu );

        // Send commands to GPU
        gpu->queue_command_buffer( gpu_commands );
    }

}; // struct DrawTask

void glTFScene::load( cstring filename, cstring path, raptor::Allocator* resident_allocator, raptor::StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) {

    using namespace raptor;

    renderer = async_loader->renderer;
    enki::TaskScheduler* task_scheduler = async_loader->task_scheduler;
    sizet temp_allocator_initial_marker = temp_allocator->get_marker();

    // Time statistics
    i64 start_scene_loading = time_now();

    gltf_scene = gltf_load_file( filename );

    i64 end_loading_file = time_now();

    // Load all textures
    images.init( resident_allocator, gltf_scene.images_count );

    Array<TextureCreation> tcs;
    tcs.init( temp_allocator, gltf_scene.images_count, gltf_scene.images_count );

    StringBuffer name_buffer;
    name_buffer.init( 4096, temp_allocator );

    for ( u32 image_index = 0; image_index < gltf_scene.images_count; ++image_index ) {
        raptor::glTF::Image& image = gltf_scene.images[ image_index ];

        int comp, width, height;

        stbi_info( image.uri.data, &width, &height, &comp );

        u32 mip_levels = 1;
        if ( true ) {
            u32 w = width;
            u32 h = height;

            while ( w > 1 && h > 1 ) {
                w /= 2;
                h /= 2;

                ++mip_levels;
            }
        }

        raptor::TextureCreation tc;
        tc.set_data( nullptr ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, raptor::TextureType::Texture2D ).set_flags( mip_levels, 0 ).set_size( ( u16 )width, ( u16 )height, 1 ).set_name( image.uri.data );
        TextureResource* tr = renderer->create_texture( tc );
        RASSERT( tr != nullptr );

        images.push( *tr );

        // Reconstruct file path
        char* full_filename = name_buffer.append_use_f( "%s%s", path, image.uri.data );
        async_loader->request_texture_data( full_filename, tr->handle );
        // Reset name buffer
        name_buffer.clear();
    }

    i64 end_loading_textures_files = time_now();

    i64 end_creating_textures = time_now();

    // Load all samplers
    samplers.init( resident_allocator, gltf_scene.samplers_count );

    for ( u32 sampler_index = 0; sampler_index < gltf_scene.samplers_count; ++sampler_index ) {
        glTF::Sampler& sampler = gltf_scene.samplers[ sampler_index ];

        char* sampler_name = name_buffer.append_use_f( "sampler_%u", sampler_index );

        SamplerCreation creation;
        switch ( sampler.min_filter ) {
            case glTF::Sampler::NEAREST:
                creation.min_filter = VK_FILTER_NEAREST;
                break;
            case glTF::Sampler::LINEAR:
                creation.min_filter = VK_FILTER_LINEAR;
                break;
            case glTF::Sampler::LINEAR_MIPMAP_NEAREST:
                creation.min_filter = VK_FILTER_LINEAR;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                break;
            case glTF::Sampler::LINEAR_MIPMAP_LINEAR:
                creation.min_filter = VK_FILTER_LINEAR;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                break;
            case glTF::Sampler::NEAREST_MIPMAP_NEAREST:
                creation.min_filter = VK_FILTER_NEAREST;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                break;
            case glTF::Sampler::NEAREST_MIPMAP_LINEAR:
                creation.min_filter = VK_FILTER_NEAREST;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                break;
        }

        creation.mag_filter = sampler.mag_filter == glTF::Sampler::Filter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

        switch ( sampler.wrap_s ) {
            case glTF::Sampler::CLAMP_TO_EDGE:
                creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                break;
            case glTF::Sampler::MIRRORED_REPEAT:
                creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                break;
            case glTF::Sampler::REPEAT:
                creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                break;
        }

        switch ( sampler.wrap_t ) {
            case glTF::Sampler::CLAMP_TO_EDGE:
                creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                break;
            case glTF::Sampler::MIRRORED_REPEAT:
                creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                break;
            case glTF::Sampler::REPEAT:
                creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                break;
        }

        creation.name = sampler_name;

        SamplerResource* sr = renderer->create_sampler( creation );
        RASSERT( sr != nullptr );

        samplers.push( *sr );
    }

    i64 end_creating_samplers = time_now();

    // Temporary array of buffer data
    raptor::Array<void*> buffers_data;
    buffers_data.init( resident_allocator, gltf_scene.buffers_count );

    for ( u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count; ++buffer_index ) {
        glTF::Buffer& buffer = gltf_scene.buffers[ buffer_index ];

        FileReadResult buffer_data = file_read_binary( buffer.uri.data, resident_allocator );
        buffers_data.push( buffer_data.data );
    }

    i64 end_reading_buffers_data = time_now();

    // Load all buffers and initialize them with buffer data
    buffers.init( resident_allocator, gltf_scene.buffer_views_count );

    for ( u32 buffer_index = 0; buffer_index < gltf_scene.buffer_views_count; ++buffer_index ) {
        glTF::BufferView& buffer = gltf_scene.buffer_views[ buffer_index ];

        i32 offset = buffer.byte_offset;
        if ( offset == glTF::INVALID_INT_VALUE ) {
            offset = 0;
        }

        u8* buffer_data = ( u8* )buffers_data[ buffer.buffer ] + offset;

        // NOTE(marco): the target attribute of a BufferView is not mandatory, so we prepare for both uses
        VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        char* buffer_name = buffer.name.data;
        if ( buffer_name == nullptr ) {
            buffer_name = name_buffer.append_use_f( "buffer_%u", buffer_index );
        }

        BufferResource* br = renderer->create_buffer( flags, ResourceUsageType::Immutable, buffer.byte_length, buffer_data, buffer_name );
        RASSERT( br != nullptr );

        buffers.push( *br );
    }

    for ( u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count; ++buffer_index ) {
        void* buffer = buffers_data[ buffer_index ];
        resident_allocator->deallocate( buffer );
    }
    buffers_data.shutdown();

    i64 end_creating_buffers = time_now();

    // This is not needed anymore, free all temp memory after.
    //resource_name_buffer.shutdown();
    temp_allocator->free_marker( temp_allocator_initial_marker );

    // Init runtime meshes
    mesh_draws.init( resident_allocator, gltf_scene.meshes_count );

    i64 end_loading = time_now();

    rprint( "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating %f seconds\n\tCreating Samplers %f seconds\n\tReading Buffers Data %f seconds\n\tCreating Buffers %f seconds\n", filename,
            time_delta_seconds( start_scene_loading, end_loading ), time_delta_seconds( start_scene_loading, end_loading_file ), time_delta_seconds( end_loading_file, end_creating_textures ),
            time_delta_seconds( end_creating_textures, end_creating_samplers ),
            time_delta_seconds( end_creating_samplers, end_reading_buffers_data ), time_delta_seconds( end_reading_buffers_data, end_creating_buffers ) );
}

void glTFScene::free_gpu_resources( raptor::Renderer* renderer ) {
    raptor::GpuDevice& gpu = *renderer->gpu;

    for ( u32 mesh_index = 0; mesh_index < mesh_draws.size; ++mesh_index ) {
        MeshDraw& mesh_draw = mesh_draws[ mesh_index ];
        gpu.destroy_buffer( mesh_draw.material_buffer );

        gpu.destroy_descriptor_set( mesh_draw.descriptor_set );
    }

    mesh_draws.shutdown();
}

void glTFScene::unload( raptor::Renderer* renderer ) {
    raptor::GpuDevice& gpu = *renderer->gpu;

    // Free scene buffers
    samplers.shutdown();
    images.shutdown();
    buffers.shutdown();

    // NOTE(marco): we can't destroy this sooner as textures and buffers
    // hold a pointer to the names stored here
    raptor::gltf_free( gltf_scene );
}

void glTFScene::prepare_draws( raptor::Renderer* renderer, raptor::StackAllocator* scratch_allocator ) {

    using namespace raptor;

    // Create pipeline state
    PipelineCreation pipeline_creation;

    sizet cached_scratch_size = scratch_allocator->get_marker();

    StringBuffer path_buffer;
    path_buffer.init( 1024, scratch_allocator );

    const char* vert_file = "main.vert";
    char* vert_path = path_buffer.append_use_f( "%s%s", RAPTOR_SHADER_FOLDER, vert_file );
    FileReadResult vert_code = file_read_text( vert_path, scratch_allocator );

    const char* frag_file = "main.frag";
    char* frag_path = path_buffer.append_use_f( "%s%s", RAPTOR_SHADER_FOLDER, frag_file );
    FileReadResult frag_code = file_read_text( frag_path, scratch_allocator );

    // Vertex input
    // TODO(marco): could these be inferred from SPIR-V?
    pipeline_creation.vertex_input.add_vertex_attribute( { 0, 0, 0, VertexComponentFormat::Float3 } ); // position
    pipeline_creation.vertex_input.add_vertex_stream( { 0, 12, VertexInputRate::PerVertex } );

    pipeline_creation.vertex_input.add_vertex_attribute( { 1, 1, 0, VertexComponentFormat::Float4 } ); // tangent
    pipeline_creation.vertex_input.add_vertex_stream( { 1, 16, VertexInputRate::PerVertex } );

    pipeline_creation.vertex_input.add_vertex_attribute( { 2, 2, 0, VertexComponentFormat::Float3 } ); // normal
    pipeline_creation.vertex_input.add_vertex_stream( { 2, 12, VertexInputRate::PerVertex } );

    pipeline_creation.vertex_input.add_vertex_attribute( { 3, 3, 0, VertexComponentFormat::Float2 } ); // texcoord
    pipeline_creation.vertex_input.add_vertex_stream( { 3, 8, VertexInputRate::PerVertex } );

    // Render pass
    pipeline_creation.render_pass = renderer->gpu->get_swapchain_output();
    // Depth
    pipeline_creation.depth_stencil.set_depth( true, VK_COMPARE_OP_LESS_OR_EQUAL );

    // Blend
    pipeline_creation.blend_state.add_blend_state().set_color( VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD );

    pipeline_creation.shaders.set_name( "main" ).add_stage( vert_code.data, vert_code.size, VK_SHADER_STAGE_VERTEX_BIT ).add_stage( frag_code.data, frag_code.size, VK_SHADER_STAGE_FRAGMENT_BIT );

    // Constant buffer
    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( UniformData ) ).set_name( "scene_cb" );
    scene_cb = renderer->gpu->create_buffer( buffer_creation );

    pipeline_creation.name = "main_no_cull";
    Program* program_no_cull = renderer->create_program( { pipeline_creation } );

    pipeline_creation.rasterization.cull_mode = VK_CULL_MODE_BACK_BIT;

    pipeline_creation.name = "main_cull";
    Program* program_cull = renderer->create_program( { pipeline_creation } );

    MaterialCreation material_creation;

    material_creation.set_name( "material_no_cull_opaque" ).set_program( program_no_cull ).set_render_index( 0 );
    Material* material_no_cull_opaque = renderer->create_material( material_creation );

    material_creation.set_name( "material_cull_opaque" ).set_program( program_cull ).set_render_index( 1 );
    Material* material_cull_opaque = renderer->create_material( material_creation );

    material_creation.set_name( "material_no_cull_transparent" ).set_program( program_no_cull ).set_render_index( 2 );
    Material* material_no_cull_transparent = renderer->create_material( material_creation );

    material_creation.set_name( "material_cull_transparent" ).set_program( program_cull ).set_render_index( 3 );
    Material* material_cull_transparent = renderer->create_material( material_creation );

    scratch_allocator->free_marker( cached_scratch_size );

    glTF::Scene& root_gltf_scene = gltf_scene.scenes[ gltf_scene.scene ];

    for ( u32 node_index = 0; node_index < root_gltf_scene.nodes_count; ++node_index ) {
        glTF::Node& node = gltf_scene.nodes[ root_gltf_scene.nodes[ node_index ] ];

        if ( node.mesh == glTF::INVALID_INT_VALUE ) {
            continue;
        }

        // TODO(marco): children

        glTF::Mesh& mesh = gltf_scene.meshes[ node.mesh ];

        vec3s node_scale{ 1.0f, 1.0f, 1.0f };
        if ( node.scale_count != 0 ) {
            RASSERT( node.scale_count == 3 );
            node_scale = vec3s{ node.scale[ 0 ], node.scale[ 1 ], node.scale[ 2 ] };
        }

        // Gltf primitives are conceptually submeshes.
        for ( u32 primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index ) {
            MeshDraw mesh_draw{ };

            mesh_draw.scale = node_scale;

            glTF::MeshPrimitive& mesh_primitive = mesh.primitives[ primitive_index ];

            const i32 position_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "POSITION" );
            const i32 tangent_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "TANGENT" );
            const i32 normal_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "NORMAL" );
            const i32 texcoord_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "TEXCOORD_0" );

            get_mesh_vertex_buffer( *this, position_accessor_index, mesh_draw.position_buffer, mesh_draw.position_offset );
            get_mesh_vertex_buffer( *this, tangent_accessor_index, mesh_draw.tangent_buffer, mesh_draw.tangent_offset );
            get_mesh_vertex_buffer( *this, normal_accessor_index, mesh_draw.normal_buffer, mesh_draw.normal_offset );
            get_mesh_vertex_buffer( *this, texcoord_accessor_index, mesh_draw.texcoord_buffer, mesh_draw.texcoord_offset );

            // Create index buffer
            glTF::Accessor& indices_accessor = gltf_scene.accessors[ mesh_primitive.indices ];
            RASSERT( indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_SHORT || indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_INT );
            mesh_draw.index_type = ( indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_SHORT ) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

            glTF::BufferView& indices_buffer_view = gltf_scene.buffer_views[ indices_accessor.buffer_view ];
            BufferResource& indices_buffer_gpu = buffers[ indices_accessor.buffer_view ];
            mesh_draw.index_buffer = indices_buffer_gpu.handle;
            mesh_draw.index_offset = indices_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : indices_accessor.byte_offset;
            mesh_draw.primitive_count = indices_accessor.count;

            // Create material
            glTF::Material& material = gltf_scene.materials[ mesh_primitive.material ];

            bool transparent = get_mesh_material( *renderer, *this, material, mesh_draw );

            raptor::DescriptorSetCreation ds_creation{};
            DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( program_cull->passes[ 0 ].pipeline, 0 );
            ds_creation.buffer( scene_cb, 0 ).buffer( mesh_draw.material_buffer, 1 ).set_layout( layout );
            mesh_draw.descriptor_set = renderer->gpu->create_descriptor_set( ds_creation );

            if ( transparent ) {
                if ( material.double_sided ) {
                    mesh_draw.material = material_no_cull_transparent;
                } else {
                    mesh_draw.material = material_cull_transparent;
                }
            } else {
                if ( material.double_sided ) {
                    mesh_draw.material = material_no_cull_opaque;
                } else {
                    mesh_draw.material = material_cull_opaque;
                }
            }

            mesh_draws.push(mesh_draw);
        }
    }

    qsort( mesh_draws.data, mesh_draws.size, sizeof( MeshDraw ), gltf_mesh_material_compare );
}

void glTFScene::upload_materials( float model_scale ) {
    // Update per mesh material buffer
    for ( u32 mesh_index = 0; mesh_index < mesh_draws.size; ++mesh_index ) {
        MeshDraw& mesh_draw = mesh_draws[ mesh_index ];

        raptor::MapBufferParameters cb_map = { mesh_draw.material_buffer, 0, 0 };
        MeshData* mesh_data = ( MeshData* )renderer->gpu->map_buffer( cb_map );
        if ( mesh_data ) {
            upload_material( *mesh_data, mesh_draw, model_scale );

            renderer->gpu->unmap_buffer( cb_map );
        }
    }
}

void glTFScene::submit_draw_task( raptor::ImGuiService* imgui, raptor::GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler ) {
    glTFDrawTask draw_task;
    draw_task.init( renderer->gpu, renderer, imgui, gpu_profiler, this );
    task_scheduler->AddTaskSetToPipe( &draw_task );
    task_scheduler->WaitforTaskSet( &draw_task );

    // Avoid using the same command buffer
    renderer->add_texture_update_commands( ( draw_task.thread_id + 1 ) % task_scheduler->GetNumTaskThreads() );
}

int gltf_mesh_material_compare( const void* a, const void* b ) {
    const MeshDraw* mesh_a = ( const MeshDraw* )a;
    const MeshDraw* mesh_b = ( const MeshDraw* )b;

    if ( mesh_a->material->render_index < mesh_b->material->render_index ) return -1;
    if ( mesh_a->material->render_index > mesh_b->material->render_index ) return  1;
    return 0;
}

int obj_mesh_material_compare( const void* a, const void* b ) {
    const ObjDraw* mesh_a = ( const ObjDraw* )a;
    const ObjDraw* mesh_b = ( const ObjDraw* )b;

    if ( mesh_a->material->render_index < mesh_b->material->render_index ) return -1;
    if ( mesh_a->material->render_index > mesh_b->material->render_index ) return  1;
    return 0;
}

void get_mesh_vertex_buffer( glTFScene& scene, i32 accessor_index, raptor::BufferHandle& out_buffer_handle, u32& out_buffer_offset ) {
    using namespace raptor;

    if ( accessor_index != -1 ) {
        glTF::Accessor& buffer_accessor = scene.gltf_scene.accessors[ accessor_index ];
        glTF::BufferView& buffer_view = scene.gltf_scene.buffer_views[ buffer_accessor.buffer_view ];
        BufferResource& buffer_gpu = scene.buffers[ buffer_accessor.buffer_view ];

        out_buffer_handle = buffer_gpu.handle;
        out_buffer_offset = buffer_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : buffer_accessor.byte_offset;
    }
}

bool get_mesh_material( raptor::Renderer& renderer, glTFScene& scene, raptor::glTF::Material& material, MeshDraw& mesh_draw ) {
    using namespace raptor;

    bool transparent = false;
    GpuDevice& gpu = *renderer.gpu;

    if ( material.pbr_metallic_roughness != nullptr ) {
        if ( material.pbr_metallic_roughness->base_color_factor_count != 0 ) {
            RASSERT( material.pbr_metallic_roughness->base_color_factor_count == 4 );

            mesh_draw.base_color_factor = {
                material.pbr_metallic_roughness->base_color_factor[ 0 ],
                material.pbr_metallic_roughness->base_color_factor[ 1 ],
                material.pbr_metallic_roughness->base_color_factor[ 2 ],
                material.pbr_metallic_roughness->base_color_factor[ 3 ],
            };
        } else {
            mesh_draw.base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
        }

        if ( material.pbr_metallic_roughness->roughness_factor != glTF::INVALID_FLOAT_VALUE ) {
            mesh_draw.metallic_roughness_occlusion_factor.x = material.pbr_metallic_roughness->roughness_factor;
        } else {
            mesh_draw.metallic_roughness_occlusion_factor.x = 1.0f;
        }

        if ( material.alpha_mode.data != nullptr && strcmp( material.alpha_mode.data, "MASK" ) == 0 ) {
            mesh_draw.flags |= DrawFlags_AlphaMask;
            transparent = true;
        }

        if ( material.alpha_cutoff != glTF::INVALID_FLOAT_VALUE ) {
            mesh_draw.alpha_cutoff = material.alpha_cutoff;
        }

        if ( material.pbr_metallic_roughness->metallic_factor != glTF::INVALID_FLOAT_VALUE ) {
            mesh_draw.metallic_roughness_occlusion_factor.y = material.pbr_metallic_roughness->metallic_factor;
        } else {
            mesh_draw.metallic_roughness_occlusion_factor.y = 1.0f;
        }

        if ( material.pbr_metallic_roughness->base_color_texture != nullptr ) {
            glTF::Texture& diffuse_texture = scene.gltf_scene.textures[ material.pbr_metallic_roughness->base_color_texture->index ];
            TextureResource& diffuse_texture_gpu = scene.images[ diffuse_texture.source ];
            SamplerResource& diffuse_sampler_gpu = scene.samplers[ diffuse_texture.sampler ];

            mesh_draw.diffuse_texture_index = diffuse_texture_gpu.handle.index;

            gpu.link_texture_sampler( diffuse_texture_gpu.handle, diffuse_sampler_gpu.handle );
        } else {
            mesh_draw.diffuse_texture_index = INVALID_TEXTURE_INDEX;
        }

        if ( material.pbr_metallic_roughness->metallic_roughness_texture != nullptr ) {
            glTF::Texture& roughness_texture = scene.gltf_scene.textures[ material.pbr_metallic_roughness->metallic_roughness_texture->index ];
            TextureResource& roughness_texture_gpu = scene.images[ roughness_texture.source ];
            SamplerResource& roughness_sampler_gpu = scene.samplers[ roughness_texture.sampler ];

            mesh_draw.roughness_texture_index = roughness_texture_gpu.handle.index;

            gpu.link_texture_sampler( roughness_texture_gpu.handle, roughness_sampler_gpu.handle );
        } else {
            mesh_draw.roughness_texture_index = INVALID_TEXTURE_INDEX;
        }
    }

    if ( material.occlusion_texture != nullptr ) {
        glTF::Texture& occlusion_texture = scene.gltf_scene.textures[ material.occlusion_texture->index ];

        TextureResource& occlusion_texture_gpu = scene.images[ occlusion_texture.source ];
        SamplerResource& occlusion_sampler_gpu = scene.samplers[ occlusion_texture.sampler ];

        mesh_draw.occlusion_texture_index = occlusion_texture_gpu.handle.index;

        if ( material.occlusion_texture->strength != glTF::INVALID_FLOAT_VALUE ) {
            mesh_draw.metallic_roughness_occlusion_factor.z = material.occlusion_texture->strength;
        } else {
            mesh_draw.metallic_roughness_occlusion_factor.z = 1.0f;
        }

        gpu.link_texture_sampler( occlusion_texture_gpu.handle, occlusion_sampler_gpu.handle );
    } else {
        mesh_draw.occlusion_texture_index = INVALID_TEXTURE_INDEX;
    }

    if ( material.normal_texture != nullptr ) {
        glTF::Texture& normal_texture = scene.gltf_scene.textures[ material.normal_texture->index ];
        TextureResource& normal_texture_gpu = scene.images[ normal_texture.source ];
        SamplerResource& normal_sampler_gpu = scene.samplers[ normal_texture.sampler ];

        gpu.link_texture_sampler( normal_texture_gpu.handle, normal_sampler_gpu.handle );

        mesh_draw.normal_texture_index = normal_texture_gpu.handle.index;
    } else {
        mesh_draw.normal_texture_index = INVALID_TEXTURE_INDEX;
    }

    // Create material buffer
    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( MeshData ) ).set_name( "mesh_data" );
    mesh_draw.material_buffer = gpu.create_buffer( buffer_creation );

    return transparent;
}

void ObjScene::load( cstring filename, cstring path, raptor::Allocator* resident_allocator, raptor::StackAllocator* temp_allocator, AsynchronousLoader* async_loader_ )
{
    using namespace raptor;

    async_loader = async_loader_;
    renderer     = async_loader->renderer;

    enki::TaskScheduler* task_scheduler = async_loader->task_scheduler;
    sizet temp_allocator_initial_marker = temp_allocator->get_marker();

    // Time statistics
    i64 start_scene_loading = time_now();

    const struct aiScene* scene = aiImportFile( filename,
        aiProcess_CalcTangentSpace       |
        aiProcess_GenNormals             |
        aiProcess_Triangulate            |
        aiProcess_JoinIdenticalVertices  |
        aiProcess_SortByPType);

    i64 end_loading_file = time_now();

    // If the import failed, report it
    if( scene == nullptr ) {
        RASSERT(false);
        return;
    }

    SamplerCreation sampler_creation{ };
    sampler_creation.set_address_mode_uv( VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT ).set_min_mag_mip( VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    sampler = renderer->create_sampler( sampler_creation );

    images.init( resident_allocator, 1024 );

    materials.init( resident_allocator, scene->mNumMaterials );

    for ( u32 material_index = 0; material_index < scene->mNumMaterials; ++material_index ) {
        aiMaterial* material = scene->mMaterials[ material_index ];

        ObjMaterial raptor_material{ };

        aiString texture_file;

        if( aiGetMaterialString( material, AI_MATKEY_TEXTURE( aiTextureType_DIFFUSE, 0 ), &texture_file ) == AI_SUCCESS ) {
            raptor_material.diffuse_texture_index = load_texture( texture_file.C_Str(), path, temp_allocator );
        }

        if( aiGetMaterialString( material, AI_MATKEY_TEXTURE( aiTextureType_NORMALS, 0 ), &texture_file ) == AI_SUCCESS )
        {
            raptor_material.normal_texture_index = load_texture( texture_file.C_Str(), path, temp_allocator );
        }

        aiColor4D color;
        if ( aiGetMaterialColor( material, AI_MATKEY_COLOR_DIFFUSE, &color ) == AI_SUCCESS ) {
            raptor_material.diffuse = { color.r, color.g, color.b, 1.0f };
        }

        if ( aiGetMaterialColor( material, AI_MATKEY_COLOR_AMBIENT, &color ) == AI_SUCCESS ) {
            raptor_material.ambient = { color.r, color.g, color.b };
        }

        if ( aiGetMaterialColor( material, AI_MATKEY_COLOR_SPECULAR, &color ) == AI_SUCCESS ) {
            raptor_material.specular = { color.r, color.g, color.b };
        }

        float f_value;
        if ( aiGetMaterialFloat( material, AI_MATKEY_SHININESS, &f_value ) == AI_SUCCESS ) {
            raptor_material.specular_exp = f_value;
        }

        if ( aiGetMaterialFloat( material, AI_MATKEY_OPACITY, &f_value ) == AI_SUCCESS ) {
            raptor_material.transparency = f_value;
            raptor_material.diffuse.w = f_value;
        }

        materials.push( raptor_material );
    }

    i64 end_loading_textures_files = time_now();

    i64 end_creating_textures = time_now();

    // Init runtime meshes
    mesh_draws.init( resident_allocator, scene->mNumMeshes );

    for ( u32 mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index ) {
        aiMesh* mesh = scene->mMeshes[ mesh_index ];

        RASSERT( ( mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE ) != 0 );

        Array<vec3s> positions;
        positions.init( resident_allocator, mesh->mNumVertices );

        Array<vec4s> tangents;
        tangents.init( resident_allocator, mesh->mNumVertices );

        Array<vec3s> normals;
        normals.init( resident_allocator, mesh->mNumVertices );

        Array<vec2s> uv_coords;
        uv_coords.init( resident_allocator, mesh->mNumVertices );

        for ( u32 vertex_index = 0; vertex_index < mesh->mNumVertices; ++vertex_index ) {
            positions.push( vec3s{
                mesh->mVertices[ vertex_index ].x,
                mesh->mVertices[ vertex_index ].y,
                mesh->mVertices[ vertex_index ].z
            } );

            tangents.push( vec4s{
                mesh->mTangents[ vertex_index ].x,
                mesh->mTangents[ vertex_index ].y,
                mesh->mTangents[ vertex_index ].z,
                1.0f
            } );

            uv_coords.push( vec2s{
                mesh->mTextureCoords[ 0 ][ vertex_index ].x,
                mesh->mTextureCoords[ 0 ][ vertex_index ].y,
            } );

            normals.push( vec3s{
                mesh->mNormals[ vertex_index ].x,
                mesh->mNormals[ vertex_index ].y,
                mesh->mNormals[ vertex_index ].z
            } );
        }

        Array<u32> indices;
        indices.init( resident_allocator, mesh->mNumFaces * 3 );

        for ( u32 face_index = 0; face_index < mesh->mNumFaces; ++face_index ) {
            RASSERT( mesh->mFaces[ face_index ].mNumIndices == 3 );

            indices.push( mesh->mFaces[ face_index ].mIndices[ 0 ] );
            indices.push( mesh->mFaces[ face_index ].mIndices[ 1 ] );
            indices.push( mesh->mFaces[ face_index ].mIndices[ 2 ] );
        }

        sizet buffer_size = ( indices.size   * sizeof( u32 ) ) +
                            ( positions.size * sizeof( vec3s ) ) +
                            ( normals.size   * sizeof( vec3s ) ) +
                            ( tangents.size  * sizeof( vec4s ) ) +
                            ( uv_coords.size * sizeof( vec2s ) );

        // NOTE(marco): the target attribute of a BufferView is not mandatory, so we prepare for both uses
        VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        BufferCreation creation{ };
        creation.set( flags, ResourceUsageType::Immutable, buffer_size ).set_persistent( true ).set_name( nullptr );

        BufferHandle br = renderer->gpu->create_buffer( creation );

        Buffer* buffer = renderer->gpu->access_buffer( br );

        ObjDraw& raptor_mesh = mesh_draws.push_use();
        memset( &raptor_mesh, 0, sizeof( ObjDraw ) );

        raptor_mesh.geometry_buffer_cpu = br;

        sizet offset = 0;

        memcpy( buffer->mapped_data + offset, indices.data, indices.size * sizeof( u32 ) );
        raptor_mesh.index_offset = offset;
        offset += indices.size * sizeof( u32 );

        memcpy( buffer->mapped_data + offset, positions.data, positions.size * sizeof( vec3s ) );
        raptor_mesh.position_offset = offset;
        offset += positions.size * sizeof( vec3s );

        memcpy( buffer->mapped_data + offset, tangents.data, tangents.size * sizeof( vec4s ) );
        raptor_mesh.tangent_offset = offset;
        offset += tangents.size * sizeof( vec4s );

        memcpy( buffer->mapped_data + offset, normals.data, normals.size * sizeof( vec3s ) );
        raptor_mesh.normal_offset = offset;
        offset += normals.size * sizeof( vec3s );

        memcpy( buffer->mapped_data + offset, uv_coords.data, uv_coords.size * sizeof( vec2s ) );
        raptor_mesh.texcoord_offset = offset;

        creation.reset().set( flags, ResourceUsageType::Immutable, buffer_size ).set_device_only( true ).set_name( nullptr );
        br = renderer->gpu->create_buffer( creation );
        raptor_mesh.geometry_buffer_gpu = br;

        // TODO(marco): ideally the CPU buffer would be using staging memory and
        // freed after it has been copied!
        async_loader->request_buffer_copy( raptor_mesh.geometry_buffer_cpu, raptor_mesh.geometry_buffer_gpu, &raptor_mesh.uploads_completed );
        raptor_mesh.uploads_queued++;

        raptor_mesh.primitive_count = mesh->mNumFaces * 3;

        ObjMaterial& material = materials[ mesh->mMaterialIndex ];

        raptor_mesh.diffuse = material.diffuse;
        raptor_mesh.ambient = material.ambient;
        raptor_mesh.specular = material.ambient;
        raptor_mesh.specular_exp = material.specular_exp;

        raptor_mesh.diffuse_texture_index = material.diffuse_texture_index;
        raptor_mesh.normal_texture_index = material.normal_texture_index;

        raptor_mesh.transparency = material.transparency;

        creation.reset();
        creation.set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( ObjGpuData ) ).set_name( "mesh_data" );

        raptor_mesh.mesh_buffer = renderer->gpu->create_buffer( creation );

        positions.shutdown();
        normals.shutdown();
        uv_coords.shutdown();
        tangents.shutdown();
        indices.shutdown();
    }

    temp_allocator->free_marker( temp_allocator_initial_marker );

    i64 end_reading_buffers_data = time_now();

    i64 end_creating_buffers = time_now();

    i64 end_loading = time_now();

    rprint( "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating %f seconds\n\tReading Buffers Data %f seconds\n\tCreating Buffers %f seconds\n", filename,
            time_delta_seconds( start_scene_loading, end_loading ), time_delta_seconds( start_scene_loading, end_loading_file ), time_delta_seconds( end_loading_file, end_creating_textures ),
            time_delta_seconds( end_creating_textures, end_reading_buffers_data ), time_delta_seconds( end_reading_buffers_data, end_creating_buffers ) );

    // We're done. Release all resources associated with this import
    aiReleaseImport( scene);
}

u32 ObjScene::load_texture( cstring texture_path, cstring path, raptor::StackAllocator* temp_allocator ) {
    using namespace raptor;

    int comp, width, height;

    stbi_info( texture_path, &width, &height, &comp );

    u32 mip_levels = 1;
    if ( true ) {
        u32 w = width;
        u32 h = height;

        while ( w > 1 && h > 1 ) {
            w /= 2;
            h /= 2;

            ++mip_levels;
        }
    }

    TextureCreation tc;
    tc.set_data( nullptr ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D ).set_flags( mip_levels, 0 ).set_size( ( u16 )width, ( u16 )height, 1 ).set_name( nullptr );
    TextureResource* tr = renderer->create_texture( tc );
    RASSERT( tr != nullptr );

    images.push( *tr );

    renderer->gpu->link_texture_sampler( tr->handle, sampler->handle );

    StringBuffer name_buffer;
    name_buffer.init( 4096, temp_allocator );

    // Reconstruct file path
    char* full_filename = name_buffer.append_use_f( "%s%s", path, texture_path );
    async_loader->request_texture_data( full_filename, tr->handle );
    // Reset name buffer
    name_buffer.clear();

    return tr->handle.index;
}

void ObjScene::free_gpu_resources( raptor::Renderer* renderer ) {
    raptor::GpuDevice& gpu = *renderer->gpu;

    for ( u32 mesh_index = 0; mesh_index < mesh_draws.size; ++mesh_index ) {
        ObjDraw& mesh_draw = mesh_draws[ mesh_index ];
        gpu.destroy_buffer( mesh_draw.geometry_buffer_cpu );
        gpu.destroy_buffer( mesh_draw.geometry_buffer_gpu );
        gpu.destroy_buffer( mesh_draw.mesh_buffer );

        gpu.destroy_descriptor_set( mesh_draw.descriptor_set );
    }

    for ( u32 texture_index = 0; texture_index < images.size; ++texture_index ) {
        renderer->destroy_texture( images.data + texture_index );
    }

    renderer->destroy_sampler( sampler );

    mesh_draws.shutdown();
}

void ObjScene::unload( raptor::Renderer* renderer ) {
    // Free scene buffers
    images.shutdown();
}

void ObjScene::upload_materials( float model_scale ) {
    // Update per mesh material buffer
    for ( u32 mesh_index = 0; mesh_index < mesh_draws.size; ++mesh_index ) {
        ObjDraw& mesh_draw = mesh_draws[ mesh_index ];

        raptor::MapBufferParameters cb_map = { mesh_draw.mesh_buffer, 0, 0 };
        ObjGpuData* mesh_data = ( ObjGpuData* )renderer->gpu->map_buffer( cb_map );
        if ( mesh_data ) {
            upload_material( *mesh_data, mesh_draw, model_scale );

            renderer->gpu->unmap_buffer( cb_map );
        }
    }
}

static bool use_secondary_command_buffers = false;

void ObjScene::submit_draw_task( raptor::ImGuiService* imgui, raptor::GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler ) {
    ObjDrawTask draw_task;
    draw_task.init( task_scheduler, renderer->gpu, renderer, imgui, gpu_profiler, this, use_secondary_command_buffers );
    task_scheduler->AddTaskSetToPipe( &draw_task );
    task_scheduler->WaitforTaskSet( &draw_task );

    // Avoid using the same command buffer
    renderer->add_texture_update_commands( ( draw_task.thread_id + 1 ) % task_scheduler->GetNumTaskThreads() );
}

void ObjScene::prepare_draws( raptor::Renderer* renderer, raptor::StackAllocator* scratch_allocator ) {
    ZoneScoped;

    using namespace raptor;

    // Create pipeline state
    PipelineCreation pipeline_creation;

    sizet cached_scratch_size = scratch_allocator->get_marker();

    StringBuffer path_buffer;
    path_buffer.init( 1024, scratch_allocator );

    const char* vert_file = "phong.vert";
    char* vert_path = path_buffer.append_use_f( "%s%s", RAPTOR_SHADER_FOLDER, vert_file );
    FileReadResult vert_code = file_read_text( vert_path, scratch_allocator );

    const char* frag_file = "phong.frag";
    char* frag_path = path_buffer.append_use_f( "%s%s", RAPTOR_SHADER_FOLDER, frag_file );
    FileReadResult frag_code = file_read_text( frag_path, scratch_allocator );

    // Vertex input
    // TODO(marco): could these be inferred from SPIR-V?
    pipeline_creation.vertex_input.add_vertex_attribute( { 0, 0, 0, VertexComponentFormat::Float3 } ); // position
    pipeline_creation.vertex_input.add_vertex_stream( { 0, 12, VertexInputRate::PerVertex } );

    pipeline_creation.vertex_input.add_vertex_attribute( { 1, 1, 0, VertexComponentFormat::Float4 } ); // tangent
    pipeline_creation.vertex_input.add_vertex_stream( { 1, 16, VertexInputRate::PerVertex } );

    pipeline_creation.vertex_input.add_vertex_attribute( { 2, 2, 0, VertexComponentFormat::Float3 } ); // normal
    pipeline_creation.vertex_input.add_vertex_stream( { 2, 12, VertexInputRate::PerVertex } );

    pipeline_creation.vertex_input.add_vertex_attribute( { 3, 3, 0, VertexComponentFormat::Float2 } ); // texcoord
    pipeline_creation.vertex_input.add_vertex_stream( { 3, 8, VertexInputRate::PerVertex } );

    // Render pass
    pipeline_creation.render_pass = renderer->gpu->get_swapchain_output();
    // Depth
    pipeline_creation.depth_stencil.set_depth( true, VK_COMPARE_OP_LESS_OR_EQUAL );

    pipeline_creation.shaders.set_name( "main" ).add_stage( vert_code.data, vert_code.size, VK_SHADER_STAGE_VERTEX_BIT ).add_stage( frag_code.data, frag_code.size, VK_SHADER_STAGE_FRAGMENT_BIT );

    pipeline_creation.rasterization.cull_mode = VK_CULL_MODE_BACK_BIT;

    // Constant buffer
    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( UniformData ) ).set_name( "scene_cb" );
    scene_cb = renderer->gpu->create_buffer( buffer_creation );

    pipeline_creation.name = "phong_opaque";
    Program* program_opqaue = renderer->create_program( { pipeline_creation } );

    // Blend
    pipeline_creation.name = "phong_transparent";
    pipeline_creation.blend_state.add_blend_state().set_color( VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD );
    Program* program_transparent = renderer->create_program( { pipeline_creation } );

    MaterialCreation material_creation;

    material_creation.set_name( "material_phong_opaque" ).set_program( program_opqaue ).set_render_index( 0 );
    Material* phong_material_opaque = renderer->create_material( material_creation );

    material_creation.set_name( "material_phong_transparent" ).set_program( program_transparent ).set_render_index( 1 );
    Material* phong_material_tranparent = renderer->create_material( material_creation );

    for ( u32 mesh_index = 0; mesh_index < mesh_draws.size; ++mesh_index ) {
        ObjDraw& mesh_draw = mesh_draws[ mesh_index ];

        if ( mesh_draw.transparency == 1.0f ) {
            mesh_draw.material = phong_material_opaque;
        } else {
            mesh_draw.material = phong_material_tranparent;
        }

        // Descriptor Set
        raptor::DescriptorSetCreation ds_creation{};
        ds_creation.set_layout( mesh_draw.material->program->passes[ 0 ].descriptor_set_layout );
        ds_creation.buffer( scene_cb, 0 ).buffer( mesh_draw.mesh_buffer, 1 );
        mesh_draw.descriptor_set = renderer->gpu->create_descriptor_set( ds_creation );
    }

    qsort( mesh_draws.data, mesh_draws.size, sizeof( ObjDraw ), obj_mesh_material_compare );
 }

// AsynchonousLoader //////////////////////////////////////////////////////

void AsynchronousLoader::init( raptor::Renderer* renderer_, enki::TaskScheduler* task_scheduler_, raptor::Allocator* resident_allocator ) {
    renderer = renderer_;
    task_scheduler = task_scheduler_;
    allocator = resident_allocator;

    file_load_requests.init( allocator, 16 );
    upload_requests.init( allocator, 16 );

    texture_ready.index = raptor::k_invalid_texture.index;
    cpu_buffer_ready.index = raptor::k_invalid_buffer.index;
    gpu_buffer_ready.index = raptor::k_invalid_buffer.index;
    completed = nullptr;

    using namespace raptor;

    // Create a persistently-mapped staging buffer
    BufferCreation bc;
    bc.reset().set( VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::Stream, rmega( 64 ) ).set_name( "staging_buffer" ).set_persistent( true );
    BufferHandle staging_buffer_handle = renderer->gpu->create_buffer( bc );

    staging_buffer = renderer->gpu->access_buffer( staging_buffer_handle );

    staging_buffer_offset = 0;

    for ( u32 i = 0; i < GpuDevice::k_max_frames; ++i) {
        VkCommandPoolCreateInfo cmd_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
        cmd_pool_info.queueFamilyIndex = renderer->gpu->vulkan_transfer_queue_family;
        cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        vkCreateCommandPool( renderer->gpu->vulkan_device, &cmd_pool_info, renderer->gpu->vulkan_allocation_callbacks, &command_pools[i]);

        VkCommandBufferAllocateInfo cmd = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
        cmd.commandPool = command_pools[i];
        cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd.commandBufferCount = 1;

        vkAllocateCommandBuffers( renderer->gpu->vulkan_device, &cmd, &command_buffers[i].vk_command_buffer );

        command_buffers[ i ].is_recording = false;
        command_buffers[ i ].device = ( renderer->gpu );
    }

    VkSemaphoreCreateInfo semaphore_info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    vkCreateSemaphore( renderer->gpu->vulkan_device, &semaphore_info, renderer->gpu->vulkan_allocation_callbacks, &transfer_complete_semaphore );

    VkFenceCreateInfo fence_info{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence( renderer->gpu->vulkan_device, &fence_info, renderer->gpu->vulkan_allocation_callbacks, &transfer_fence );
}

void AsynchronousLoader::shutdown() {

    renderer->gpu->destroy_buffer( staging_buffer->handle );

    file_load_requests.shutdown();
    upload_requests.shutdown();

    for ( u32 i = 0; i < raptor::GpuDevice::k_max_frames; ++i ) {
        vkDestroyCommandPool( renderer->gpu->vulkan_device, command_pools[ i ], renderer->gpu->vulkan_allocation_callbacks );
        // Command buffers are destroyed with the pool associated.
    }

    vkDestroySemaphore( renderer->gpu->vulkan_device, transfer_complete_semaphore, renderer->gpu->vulkan_allocation_callbacks );
    vkDestroyFence( renderer->gpu->vulkan_device, transfer_fence, renderer->gpu->vulkan_allocation_callbacks );
}

void AsynchronousLoader::update( raptor::Allocator* scratch_allocator ) {
    using namespace raptor;

    // If a texture was processed in the previous commands, signal the renderer
    if ( texture_ready.index != k_invalid_texture.index ) {
        // Add update request.
        // This method is multithreaded_safe
        renderer->add_texture_to_update( texture_ready );
    }

    if ( cpu_buffer_ready.index != k_invalid_buffer.index && cpu_buffer_ready.index != k_invalid_buffer.index ) {
        RASSERT( completed != nullptr );
        (*completed)++;

        // TODO(marco): free cpu buffer

        gpu_buffer_ready.index = k_invalid_buffer.index;
        cpu_buffer_ready.index = k_invalid_buffer.index;
        completed = nullptr;
    }

    texture_ready.index = k_invalid_texture.index;

    // Process upload requests
    if ( upload_requests.size ) {
        ZoneScoped;

        // Wait for transfer fence to be finished
        if ( vkGetFenceStatus( renderer->gpu->vulkan_device, transfer_fence ) != VK_SUCCESS ) {
            return;
        }
        // Reset if file requests are present.
        vkResetFences( renderer->gpu->vulkan_device, 1, &transfer_fence );

        // Get last request
        UploadRequest request = upload_requests.back();
        upload_requests.pop();

        CommandBuffer* cb = &command_buffers[ renderer->gpu->current_frame ];
        cb->begin();

        if ( request.texture.index != k_invalid_texture.index ) {
            Texture* texture = renderer->gpu->access_texture( request.texture );
            const u32 k_texture_channels = 4;
            const u32 k_texture_alignment = 4;
            const sizet aligned_image_size = memory_align( texture->width * texture->height * k_texture_channels, k_texture_alignment );
            // Request place in buffer
            const sizet current_offset = std::atomic_fetch_add( &staging_buffer_offset, aligned_image_size );

            cb->upload_texture_data( texture->handle, request.data, staging_buffer->handle, current_offset );

            free( request.data );
        }
        else if ( request.cpu_buffer.index != k_invalid_buffer.index && request.gpu_buffer.index != k_invalid_buffer.index ) {
            Buffer* src = renderer->gpu->access_buffer( request.cpu_buffer );
            Buffer* dst = renderer->gpu->access_buffer( request.gpu_buffer );

            cb->upload_buffer_data( src->handle, dst->handle );
        }
        else if ( request.cpu_buffer.index != k_invalid_buffer.index ) {
            Buffer* buffer = renderer->gpu->access_buffer( request.cpu_buffer );
            // TODO: proper alignment
            const sizet aligned_image_size = memory_align( buffer->size, 64 );
            const sizet current_offset = std::atomic_fetch_add( &staging_buffer_offset, aligned_image_size );
            cb->upload_buffer_data( buffer->handle, request.data, staging_buffer->handle, current_offset );

            free( request.data );
        }

        cb->end();

        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cb->vk_command_buffer;
        VkPipelineStageFlags wait_flag[] { VK_PIPELINE_STAGE_TRANSFER_BIT };
        VkSemaphore wait_semaphore[] { transfer_complete_semaphore };
        submitInfo.pWaitSemaphores = wait_semaphore;
        submitInfo.pWaitDstStageMask = wait_flag;

        VkQueue used_queue = renderer->gpu->vulkan_transfer_queue;
        vkQueueSubmit( used_queue, 1, &submitInfo, transfer_fence );

        // TODO(marco): better management for state machine. We need to account for file -> buffer,
        // buffer -> texture and buffer -> buffer. One the CPU buffer has been used it should be freed.
        if ( request.texture.index != k_invalid_index ) {
            RASSERT( texture_ready.index == k_invalid_texture.index );
            texture_ready = request.texture;
        }
        else if ( request.cpu_buffer.index != k_invalid_buffer.index && request.gpu_buffer.index != k_invalid_buffer.index ) {
            RASSERT( cpu_buffer_ready.index == k_invalid_index );
            RASSERT( gpu_buffer_ready.index == k_invalid_index );
            RASSERT( completed == nullptr );
            cpu_buffer_ready = request.cpu_buffer;
            gpu_buffer_ready = request.gpu_buffer;
            completed = request.completed;
        }
        else if ( request.cpu_buffer.index != k_invalid_index ) {
            RASSERT( cpu_buffer_ready.index == k_invalid_index );
            cpu_buffer_ready = request.cpu_buffer;
        }
    }

    // Process a file request
    if ( file_load_requests.size ) {
        FileLoadRequest load_request = file_load_requests.back();
        file_load_requests.pop();

        i64 start_reading_file = time_now();
        // Process request
        int x, y, comp;
        u8* texture_data = stbi_load( load_request.path, &x, &y, &comp, 4 );

        if ( texture_data ) {
            rprint( "File %s read in %f ms\n", load_request.path, time_from_milliseconds( start_reading_file ) );

            UploadRequest& upload_request = upload_requests.push_use();
            upload_request.data = texture_data;
            upload_request.texture = load_request.texture;
            upload_request.cpu_buffer = k_invalid_buffer;
        }
        else {
            rprint( "Error reading file %s\n", load_request.path );
        }
    }

    staging_buffer_offset = 0;
}

void AsynchronousLoader::request_texture_data( cstring filename, raptor::TextureHandle texture ) {

    FileLoadRequest& request = file_load_requests.push_use();
    strcpy( request.path, filename );
    request.texture = texture;
    request.buffer = raptor::k_invalid_buffer;
}

void AsynchronousLoader::request_buffer_upload( void* data, raptor::BufferHandle buffer ) {

    UploadRequest& upload_request = upload_requests.push_use();
    upload_request.data = data;
    upload_request.cpu_buffer = buffer;
    upload_request.texture = raptor::k_invalid_texture;
}

void AsynchronousLoader::request_buffer_copy( raptor::BufferHandle src, raptor::BufferHandle dst, u32* completed ) {

    UploadRequest& upload_request = upload_requests.push_use();
    upload_request.completed = completed;
    upload_request.data = nullptr;
    upload_request.cpu_buffer = src;
    upload_request.gpu_buffer = dst;
    upload_request.texture = raptor::k_invalid_texture;
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

    AsynchronousLoader*     async_loader;
    enki::TaskScheduler*    task_scheduler;
    bool                    execute         = true;
}; // struct AsynchronousLoadTask

int main( int argc, char** argv ) {

    if ( argc < 2 ) {
        printf( "Usage: chapter3 [path to glTF model]\n");
        InjectDefault3DModel();
    }

    using namespace raptor;
    // Init services
    MemoryServiceConfiguration memory_configuration;
    memory_configuration.maximum_dynamic_size = rmega( 500 );

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
    WindowConfiguration wconf{ 1280, 800, "Raptor Chapter 3", &MemoryService::instance()->system_allocator};
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
    game_camera.camera.init_perpective( 0.1f, 4000.f, 60.f, wconf.width * 1.f / wconf.height );
    game_camera.init( true, 20.f, 6.f, 0.1f );

    time_service_init();

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

    Scene* scene = nullptr;

    char* file_extension = file_extension_from_path( file_name );

    if ( strcmp( file_extension, "gltf" ) == 0 ) {
        scene = new glTFScene;
    } else if ( strcmp( file_extension, "obj" ) == 0 ) {
        scene = new ObjScene;
    }

    scene->load( file_name, file_base_path, allocator, &scratch_allocator, &async_loader );

    // NOTE(marco): restore working directory
    directory_change( cwd.path );

    scene->prepare_draws( &renderer, &scratch_allocator );

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

    vec3s light = vec3s{ 0.0f, 4.0f, 0.0f };

    float model_scale = 1.0f;
    float light_range = 20.0f;
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

            game_camera.camera.set_aspect_ratio( window.width * 1.f / window.height );
        }
        // This MUST be AFTER os messages!
        imgui->new_frame();

        const i64 current_tick = time_now();
        f32 delta_time = ( f32 )time_delta_seconds( begin_frame_tick, current_tick );
        begin_frame_tick = current_tick;

        input.update( delta_time );
        game_camera.update( &input, window.width * 1.f, window.height * 1.f, delta_time );
        window.center_mouse( game_camera.mouse_dragging );

        {
            ZoneScopedN("ImGui Recording");

            if ( ImGui::Begin( "Raptor ImGui" ) ) {
                ImGui::InputFloat( "Model scale", &model_scale, 0.001f );
                ImGui::SliderFloat3( "Light position", light.raw, -30.0f, 30.0f );
                ImGui::InputFloat( "Light range", &light_range );
                ImGui::InputFloat( "Light intensity", &light_intensity );
                ImGui::InputFloat3( "Camera position", game_camera.camera.position.raw );
                ImGui::InputFloat3( "Camera target movement", game_camera.target_movement.raw );
                ImGui::Separator();
                ImGui::Checkbox( "Dynamically recreate descriptor sets", &recreate_per_thread_descriptors );
                ImGui::Checkbox( "Use secondary command buffers", &use_secondary_command_buffers );
            }
            ImGui::End();

            if ( ImGui::Begin( "GPU" ) ) {
                renderer.imgui_draw();

                ImGui::Separator();
                gpu_profiler.imgui_draw();

            }
            ImGui::End();

            //MemoryService::instance()->imgui_draw();
        }

        {
            ZoneScopedN("UniformBufferUpdate");
            // Update common constant buffer
            MapBufferParameters cb_map = { scene_cb, 0, 0 };
            float* cb_data = ( float* )gpu.map_buffer( cb_map );
            if ( cb_data ) {

                UniformData uniform_data{ };
                uniform_data.vp = game_camera.camera.view_projection;
                uniform_data.eye = vec4s{ game_camera.camera.position.x, game_camera.camera.position.y, game_camera.camera.position.z, 1.0f };
                uniform_data.light = vec4s{ light.x, light.y, light.z, 1.0f };
                uniform_data.light_range = light_range;
                uniform_data.light_intensity = light_intensity;

                memcpy( cb_data, &uniform_data, sizeof( UniformData ) );

                gpu.unmap_buffer( cb_map );
            }

            scene->upload_materials( model_scale );
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

    gpu.destroy_buffer( scene_cb );

    imgui->shutdown();

    gpu_profiler.shutdown();

    scene->free_gpu_resources( &renderer );

    rm.shutdown();
    renderer.shutdown();

    scene->unload( &renderer );

    delete scene;

    input.shutdown();
    window.unregister_os_messages_callback( input_os_messages_callback );
    window.shutdown();

    scratch_allocator.shutdown();
    MemoryService::instance()->shutdown();

    return 0;
}
