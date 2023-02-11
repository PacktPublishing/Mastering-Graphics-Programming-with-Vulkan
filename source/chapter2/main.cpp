
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

#include "foundation/file.hpp"
#include "foundation/gltf.hpp"
#include "foundation/numerics.hpp"
#include "foundation/time.hpp"
#include "foundation/resource_manager.hpp"

#include "external/imgui/imgui.h"

#include "external/tracy/tracy/Tracy.hpp"

#include <stdio.h>
#include <stdlib.h>

///////////////////////////////////////

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

//
//
static void draw_mesh( raptor::Renderer& renderer, raptor::CommandBuffer* gpu_commands, MeshDraw& mesh_draw ) {
    // Descriptor Set
    raptor::DescriptorSetCreation ds_creation{};
    ds_creation.buffer( scene_cb, 0 ).buffer( mesh_draw.material_buffer, 1 );
    raptor::DescriptorSetHandle descriptor_set = renderer.create_descriptor_set( gpu_commands, mesh_draw.material, ds_creation );

    gpu_commands->bind_vertex_buffer( mesh_draw.position_buffer, 0, mesh_draw.position_offset );
    gpu_commands->bind_vertex_buffer( mesh_draw.tangent_buffer, 1, mesh_draw.tangent_offset );
    gpu_commands->bind_vertex_buffer( mesh_draw.normal_buffer, 2, mesh_draw.normal_offset );
    gpu_commands->bind_vertex_buffer( mesh_draw.texcoord_buffer, 3, mesh_draw.texcoord_offset );
    gpu_commands->bind_index_buffer( mesh_draw.index_buffer, mesh_draw.index_offset );
    gpu_commands->bind_local_descriptor_set( &descriptor_set, 1, nullptr, 0 );

    gpu_commands->draw_indexed( raptor::TopologyType::Triangle, mesh_draw.primitive_count, 1, 0, 0, 0 );
}

//
//
struct Scene {

    raptor::Array<MeshDraw>                 mesh_draws;

    // All graphics resources used by the scene
    raptor::Array<raptor::TextureResource>  images;
    raptor::Array<raptor::SamplerResource>  samplers;
    raptor::Array<raptor::BufferResource>   buffers;

    raptor::glTF::glTF                      gltf_scene; // Source gltf scene

}; // struct GltfScene

static void scene_load_from_gltf( cstring filename, raptor::Renderer& renderer, raptor::Allocator* allocator, Scene& scene ) {

    using namespace raptor;

    scene.gltf_scene = gltf_load_file( filename );

    // Load all textures
    scene.images.init( allocator, scene.gltf_scene.images_count );

    for ( u32 image_index = 0; image_index < scene.gltf_scene.images_count; ++image_index ) {
        glTF::Image& image = scene.gltf_scene.images[ image_index ];
        TextureResource* tr = renderer.create_texture( image.uri.data, image.uri.data, true );
        RASSERT( tr != nullptr );

        scene.images.push( *tr );
    }

    StringBuffer resource_name_buffer;
    resource_name_buffer.init( 4096, allocator );

    // Load all samplers
    scene.samplers.init( allocator, scene.gltf_scene.samplers_count );

    for ( u32 sampler_index = 0; sampler_index < scene.gltf_scene.samplers_count; ++sampler_index ) {
        glTF::Sampler& sampler = scene.gltf_scene.samplers[ sampler_index ];

        char* sampler_name = resource_name_buffer.append_use_f( "sampler_%u", sampler_index );

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

        SamplerResource* sr = renderer.create_sampler( creation );
        RASSERT( sr != nullptr );

        scene.samplers.push( *sr );
    }

    // Temporary array of buffer data
    raptor::Array<void*> buffers_data;
    buffers_data.init( allocator, scene.gltf_scene.buffers_count );

    for ( u32 buffer_index = 0; buffer_index < scene.gltf_scene.buffers_count; ++buffer_index ) {
        glTF::Buffer& buffer = scene.gltf_scene.buffers[ buffer_index ];

        FileReadResult buffer_data = file_read_binary( buffer.uri.data, allocator );
        buffers_data.push( buffer_data.data );
    }

    // Load all buffers and initialize them with buffer data
    scene.buffers.init( allocator, scene.gltf_scene.buffer_views_count );

    for ( u32 buffer_index = 0; buffer_index < scene.gltf_scene.buffer_views_count; ++buffer_index ) {
        glTF::BufferView& buffer = scene.gltf_scene.buffer_views[ buffer_index ];

        i32 offset = buffer.byte_offset;
        if ( offset == glTF::INVALID_INT_VALUE ) {
            offset = 0;
        }

        u8* data = ( u8* )buffers_data[ buffer.buffer ] + offset;

        // NOTE(marco): the target attribute of a BufferView is not mandatory, so we prepare for both uses
        VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        char* buffer_name = buffer.name.data;
        if ( buffer_name == nullptr ) {
            buffer_name = resource_name_buffer.append_use_f( "buffer_%u", buffer_index );
        }

        BufferResource* br = renderer.create_buffer( flags, ResourceUsageType::Immutable, buffer.byte_length, data, buffer_name );
        RASSERT( br != nullptr );

        scene.buffers.push( *br );
    }

    for ( u32 buffer_index = 0; buffer_index < scene.gltf_scene.buffers_count; ++buffer_index ) {
        void* buffer = buffers_data[ buffer_index ];
        allocator->deallocate( buffer );
    }
    buffers_data.shutdown();

    resource_name_buffer.shutdown();

    // Init runtime meshes
    scene.mesh_draws.init( allocator, scene.gltf_scene.meshes_count );
}

static void scene_free_gpu_resources( Scene& scene, raptor::Renderer& renderer ) {
    raptor::GpuDevice& gpu = *renderer.gpu;

    for ( u32 mesh_index = 0; mesh_index < scene.mesh_draws.size; ++mesh_index ) {
        MeshDraw& mesh_draw = scene.mesh_draws[ mesh_index ];
        gpu.destroy_buffer( mesh_draw.material_buffer );
    }

    scene.mesh_draws.shutdown();
}

static void scene_unload( Scene& scene, raptor::Renderer& renderer ) {

    raptor::GpuDevice& gpu = *renderer.gpu;

    // Free scene buffers
    scene.samplers.shutdown();
    scene.images.shutdown();
    scene.buffers.shutdown();

    // NOTE(marco): we can't destroy this sooner as textures and buffers
    // hold a pointer to the names stored here
    raptor::gltf_free( scene.gltf_scene );
}

static int mesh_material_compare( const void* a, const void* b ) {
    const MeshDraw* mesh_a = ( const MeshDraw* )a;
    const MeshDraw* mesh_b = ( const MeshDraw* )b;

    if ( mesh_a->material->render_index < mesh_b->material->render_index ) return -1;
    if ( mesh_a->material->render_index > mesh_b->material->render_index ) return  1;
    return 0;
}

static void get_mesh_vertex_buffer( Scene& scene, i32 accessor_index, raptor::BufferHandle& out_buffer_handle, u32& out_buffer_offset ) {
    using namespace raptor;

    if ( accessor_index != -1 ) {
        glTF::Accessor& buffer_accessor = scene.gltf_scene.accessors[ accessor_index ];
        glTF::BufferView& buffer_view = scene.gltf_scene.buffer_views[ buffer_accessor.buffer_view ];
        BufferResource& buffer_gpu = scene.buffers[ buffer_accessor.buffer_view ];

        out_buffer_handle = buffer_gpu.handle;
        out_buffer_offset = buffer_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : buffer_accessor.byte_offset;
    }
}

static bool get_mesh_material( raptor::Renderer& renderer, Scene& scene, raptor::glTF::Material& material, MeshDraw& mesh_draw ) {
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

int main( int argc, char** argv ) {

    if ( argc < 2 ) {
        printf( "Usage: chapter2 [path to glTF model]\n");
        InjectDefault3DModel();
    }

    using namespace raptor;
    // Init services
    MemoryService::instance()->init( nullptr );
    Allocator* allocator = &MemoryService::instance()->system_allocator;

    StackAllocator scratch_allocator;
    scratch_allocator.init( rmega( 8 ) );

    // window
    WindowConfiguration wconf{ 1280, 800, "Raptor Chapter 2", &MemoryService::instance()->system_allocator};
    raptor::Window window;
    window.init( &wconf );

    InputService input;
    input.init( allocator );

    // Callback register: input needs to react to OS messages.
    window.register_os_messages_callback( input_os_messages_callback, &input );

    // graphics
    DeviceCreation dc;
    dc.set_window( window.width, window.height, window.platform_handle ).set_allocator( allocator ).set_linear_allocator( &scratch_allocator );
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

    Directory cwd{ };
    directory_current(&cwd);

    char gltf_base_path[512]{ };
    memcpy( gltf_base_path, argv[ 1 ], strlen( argv[ 1] ) );
    file_directory_from_path( gltf_base_path );

    directory_change( gltf_base_path );

    char gltf_file[512]{ };
    memcpy( gltf_file, argv[ 1 ], strlen( argv[ 1] ) );
    file_name_from_path( gltf_file );

    Scene scene;
    scene_load_from_gltf( gltf_file, renderer, allocator, scene );

    // NOTE(marco): restore working directory
    directory_change( cwd.path );

    {
        // Create pipeline state
        PipelineCreation pipeline_creation;

        StringBuffer path_buffer;
        path_buffer.init( 1024, allocator );

        const char* vert_file = "main.vert";
        char* vert_path = path_buffer.append_use_f("%s%s", RAPTOR_SHADER_FOLDER, vert_file );
        FileReadResult vert_code = file_read_text( vert_path, allocator );

        const char* frag_file = "main.frag";
        char* frag_path = path_buffer.append_use_f("%s%s", RAPTOR_SHADER_FOLDER, frag_file );
        FileReadResult frag_code = file_read_text( frag_path, allocator );

        // Vertex input
        // TODO(marco): could these be inferred from SPIR-V?
        pipeline_creation.vertex_input.add_vertex_attribute( { 0, 0, 0, VertexComponentFormat::Float3 } ); // position
        pipeline_creation.vertex_input.add_vertex_stream( { 0, 12, VertexInputRate::PerVertex } );

        pipeline_creation.vertex_input.add_vertex_attribute( { 1, 1, 0, VertexComponentFormat::Float4 } ); // tangent
        pipeline_creation.vertex_input.add_vertex_stream( { 1, 16, VertexInputRate::PerVertex} );

        pipeline_creation.vertex_input.add_vertex_attribute( { 2, 2, 0, VertexComponentFormat::Float3 } ); // normal
        pipeline_creation.vertex_input.add_vertex_stream( { 2, 12, VertexInputRate::PerVertex } );

        pipeline_creation.vertex_input.add_vertex_attribute( { 3, 3, 0, VertexComponentFormat::Float2 } ); // texcoord
        pipeline_creation.vertex_input.add_vertex_stream( { 3, 8, VertexInputRate::PerVertex} );

        // Render pass
        pipeline_creation.render_pass = gpu.get_swapchain_output();
        // Depth
        pipeline_creation.depth_stencil.set_depth( true, VK_COMPARE_OP_LESS_OR_EQUAL );

        // Blend
        pipeline_creation.blend_state.add_blend_state().set_color( VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD );

        pipeline_creation.shaders.set_name( "main" ).add_stage( vert_code.data, vert_code.size, VK_SHADER_STAGE_VERTEX_BIT ).add_stage( frag_code.data, frag_code.size, VK_SHADER_STAGE_FRAGMENT_BIT );

        // Constant buffer
        BufferCreation buffer_creation;
        buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( UniformData ) ).set_name( "scene_cb" );
        scene_cb = gpu.create_buffer( buffer_creation );

        pipeline_creation.name = "main_no_cull";
        Program* program_no_cull = renderer.create_program( { pipeline_creation } );

        pipeline_creation.rasterization.cull_mode = VK_CULL_MODE_BACK_BIT;

        pipeline_creation.name = "main_cull";
        Program* program_cull = renderer.create_program( { pipeline_creation } );

        MaterialCreation material_creation;

        material_creation.set_name( "material_no_cull_opaque" ).set_program( program_no_cull ).set_render_index( 0 );
        Material* material_no_cull_opaque = renderer.create_material( material_creation );

        material_creation.set_name( "material_cull_opaque" ).set_program( program_cull ).set_render_index( 1 );
        Material* material_cull_opaque = renderer.create_material( material_creation );

        material_creation.set_name( "material_no_cull_transparent" ).set_program( program_no_cull ).set_render_index( 2 );
        Material* material_no_cull_transparent = renderer.create_material( material_creation );

        material_creation.set_name( "material_cull_transparent" ).set_program( program_cull ).set_render_index( 3 );
        Material* material_cull_transparent = renderer.create_material( material_creation );

        path_buffer.shutdown();
        allocator->deallocate( vert_code.data );
        allocator->deallocate( frag_code.data );

        glTF::Scene& root_gltf_scene = scene.gltf_scene.scenes[ scene.gltf_scene.scene ];

        for ( u32 node_index = 0; node_index < root_gltf_scene.nodes_count; ++node_index )
        {
            glTF::Node& node = scene.gltf_scene.nodes[ root_gltf_scene.nodes[ node_index ] ];

            if ( node.mesh == glTF::INVALID_INT_VALUE ) {
                continue;
            }

            // TODO(marco): children

            glTF::Mesh& mesh = scene.gltf_scene.meshes[ node.mesh ];

            vec3s node_scale{ 1.0f, 1.0f, 1.0f };
            if ( node.scale_count != 0 ) {
                RASSERT( node.scale_count == 3 );
                node_scale = vec3s{ node.scale[0], node.scale[1], node.scale[2] };
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

                get_mesh_vertex_buffer( scene, position_accessor_index, mesh_draw.position_buffer, mesh_draw.position_offset );
                get_mesh_vertex_buffer( scene, tangent_accessor_index, mesh_draw.tangent_buffer, mesh_draw.tangent_offset );
                get_mesh_vertex_buffer( scene, normal_accessor_index, mesh_draw.normal_buffer, mesh_draw.normal_offset );
                get_mesh_vertex_buffer( scene, texcoord_accessor_index, mesh_draw.texcoord_buffer, mesh_draw.texcoord_offset );

                // Create index buffer
                glTF::Accessor& indices_accessor = scene.gltf_scene.accessors[ mesh_primitive.indices ];
                glTF::BufferView& indices_buffer_view = scene.gltf_scene.buffer_views[ indices_accessor.buffer_view ];
                BufferResource& indices_buffer_gpu = scene.buffers[ indices_accessor.buffer_view ];
                mesh_draw.index_buffer = indices_buffer_gpu.handle;
                mesh_draw.index_offset = indices_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : indices_accessor.byte_offset;
                mesh_draw.primitive_count = indices_accessor.count;

                // Create material
                glTF::Material& material = scene.gltf_scene.materials[ mesh_primitive.material ];

                bool transparent = get_mesh_material( renderer, scene, material, mesh_draw );

                if ( transparent) {
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

                scene.mesh_draws.push( mesh_draw );
            }
        }
    }

    qsort( scene.mesh_draws.data, scene.mesh_draws.size, sizeof( MeshDraw ), mesh_material_compare );

    i64 begin_frame_tick = time_now();

    vec3s light = vec3s{ 0.0f, 4.0f, 0.0f };

    float model_scale = 1.0f;
    float light_range = 20.0f;
    float light_intensity = 80.0f;

    while ( !window.requested_exit ) {
        ZoneScopedN("RenderLoop");

        // New frame
        if ( !window.minimized ) {
            gpu.new_frame();
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

        if ( ImGui::Begin( "Raptor ImGui" ) ) {
            ImGui::InputFloat( "Model scale", &model_scale, 0.001f );
            ImGui::InputFloat3( "Light position", light.raw );
            ImGui::InputFloat( "Light range", &light_range );
            ImGui::InputFloat( "Light intensity", &light_intensity );
            ImGui::InputFloat3( "Camera position", game_camera.camera.position.raw );
            ImGui::InputFloat3( "Camera target movement", game_camera.target_movement.raw );
        }
        ImGui::End();

        if ( ImGui::Begin( "GPU" ) ) {
            gpu_profiler.imgui_draw();
        }
        ImGui::End();

        MemoryService::instance()->imgui_draw();

        {
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

            // Update per mesh material buffer
            for ( u32 mesh_index = 0; mesh_index < scene.mesh_draws.size; ++mesh_index ) {
                MeshDraw& mesh_draw = scene.mesh_draws[ mesh_index ];

                cb_map.buffer = mesh_draw.material_buffer;
                MeshData* mesh_data = ( MeshData* )gpu.map_buffer( cb_map );
                if ( mesh_data ) {
                    upload_material( *mesh_data, mesh_draw, model_scale );

                    gpu.unmap_buffer( cb_map );
                }
            }
        }

        if ( !window.minimized ) {
            raptor::CommandBuffer* gpu_commands = gpu.get_command_buffer( QueueType::Graphics, true );
            gpu_commands->push_marker( "Frame" );

            gpu_commands->clear( 0.3, 0.3, 0.3, 1 );
            gpu_commands->clear_depth_stencil( 1.0f, 0 );
            gpu_commands->bind_pass( gpu.get_swapchain_pass() );
            gpu_commands->set_scissor( nullptr );
            gpu_commands->set_viewport( nullptr );

            Material* last_material = nullptr;
            // TODO(marco): loop by material so that we can deal with multiple passes
            for ( u32 mesh_index = 0; mesh_index < scene.mesh_draws.size; ++mesh_index ) {
                MeshDraw& mesh_draw = scene.mesh_draws[ mesh_index ];

                if ( mesh_draw.material != last_material ) {
                    PipelineHandle pipeline = renderer.get_pipeline( mesh_draw.material );

                    gpu_commands->bind_pipeline( pipeline );

                    last_material = mesh_draw.material;
                }

                draw_mesh( renderer, gpu_commands, mesh_draw );
            }

            imgui->render( *gpu_commands );

            gpu_commands->pop_marker();

            gpu_profiler.update( gpu );

            // Send commands to GPU
            gpu.queue_command_buffer( gpu_commands );
            gpu.present();
        } else {
            ImGui::Render();
        }

        FrameMark;
    }

    gpu.destroy_buffer( scene_cb );

    imgui->shutdown();

    gpu_profiler.shutdown();

    scene_free_gpu_resources( scene, renderer );

    rm.shutdown();
    renderer.shutdown();

    scene_unload( scene, renderer );

    input.shutdown();
    window.unregister_os_messages_callback( input_os_messages_callback );
    window.shutdown();

    MemoryService::instance()->shutdown();

    return 0;
}
