#include "graphics/gltf_scene.hpp"
#include "graphics/gpu_profiler.hpp"
#include "graphics/raptor_imgui.hpp"
#include "graphics/asynchronous_loader.hpp"
#include "graphics/scene_graph.hpp"

#include "foundation/file.hpp"
#include "foundation/time.hpp"

#include "external/imgui/imgui.h"
#include "external/stb_image.h"

#include "external/cglm/struct/affine.h"
#include "external/cglm/struct/mat4.h"
#include "external/cglm/struct/vec3.h"
#include "external/cglm/struct/quat.h"

#include "external/tracy/tracy/Tracy.hpp"


namespace raptor {

static int gltf_mesh_material_compare( const void* a, const void* b ) {
    const Mesh* mesh_a = ( const Mesh* )a;
    const Mesh* mesh_b = ( const Mesh* )b;

    if ( mesh_a->pbr_material.material->render_index < mesh_b->pbr_material.material->render_index ) return -1;
    if ( mesh_a->pbr_material.material->render_index > mesh_b->pbr_material.material->render_index ) return  1;
    return 0;
}

//
//
static void copy_gpu_material_data( GpuMeshData& gpu_mesh_data, const Mesh& mesh ) {
    gpu_mesh_data.textures[ 0 ] = mesh.pbr_material.diffuse_texture_index;
    gpu_mesh_data.textures[ 1 ] = mesh.pbr_material.roughness_texture_index;
    gpu_mesh_data.textures[ 2 ] = mesh.pbr_material.normal_texture_index;
    gpu_mesh_data.textures[ 3 ] = mesh.pbr_material.occlusion_texture_index;
    gpu_mesh_data.base_color_factor = mesh.pbr_material.base_color_factor;
    gpu_mesh_data.metallic_roughness_occlusion_factor = mesh.pbr_material.metallic_roughness_occlusion_factor;
    gpu_mesh_data.alpha_cutoff = mesh.pbr_material.alpha_cutoff;
    gpu_mesh_data.flags = mesh.pbr_material.flags;
}

//
//
static void copy_gpu_mesh_matrix( GpuMeshData& gpu_mesh_data, const Mesh& mesh, const f32 global_scale, const SceneGraph* scene_graph ) {
    if ( scene_graph ) {
        // Apply global scale matrix
        // NOTE: for left-handed systems (as defined in cglm) need to invert positive and negative Z.
        const mat4s scale_matrix = glms_scale_make( { global_scale, global_scale, -global_scale } );
        gpu_mesh_data.world = glms_mat4_mul( scale_matrix, scene_graph->world_matrices[ mesh.scene_graph_node_index ] );

        gpu_mesh_data.inverse_world = glms_mat4_inv( glms_mat4_transpose( gpu_mesh_data.world ) );
    } else {
        gpu_mesh_data.world = glms_mat4_identity();
        gpu_mesh_data.inverse_world = glms_mat4_identity();
    }
}

//
// DepthPrePass ///////////////////////////////////////////////////////
void DepthPrePass::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    glTFScene* scene = ( glTFScene* )render_scene;

    Material* last_material = nullptr;
    for ( u32 mesh_index = 0; mesh_index < mesh_instances.size; ++mesh_index ) {
        MeshInstance& mesh_instance = mesh_instances[ mesh_index ];
        Mesh& mesh = *mesh_instance.mesh;

        if ( mesh.pbr_material.material != last_material ) {
            PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, mesh_instance.material_pass_index );

            gpu_commands->bind_pipeline( pipeline );

            last_material = mesh.pbr_material.material;
        }

        scene->draw_mesh( gpu_commands, mesh );
    }
}

void DepthPrePass::prepare_draws( glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "depth_pre_pass" );
    if ( node == nullptr ) {
        RASSERT( false );
        return;
    }

    // Create pipeline state
    PipelineCreation pipeline_creation;

    const u64 hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;

    material_creation.set_name( "material_depth_pre_pass" ).set_technique( main_technique ).set_render_index( 0 );
    Material* material_depth_pre_pass = renderer->create_material( material_creation );

    glTF::glTF& gltf_scene = scene.gltf_scene;

    mesh_instances.init( resident_allocator, 16 );

    // Copy all mesh draws and change only material.
    for ( u32 i = 0; i < scene.meshes.size; ++i ) {

        Mesh* mesh = &scene.meshes[ i ];
        if ( mesh->is_transparent() ) {
            continue;
        }

        MeshInstance mesh_instance{};
        mesh_instance.mesh = mesh;
        // TODO: pass 0 of main material is depth prepass.
        mesh_instance.material_pass_index = 0;

        mesh_instances.push( mesh_instance );
    }
}

void DepthPrePass::free_gpu_resources() {
    GpuDevice& gpu = *renderer->gpu;

    mesh_instances.shutdown();
}

//
// GBufferPass ////////////////////////////////////////////////////////
void GBufferPass::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    glTFScene* scene = ( glTFScene* )render_scene;

    Material* last_material = nullptr;
    for ( u32 mesh_index = 0; mesh_index < mesh_instances.size; ++mesh_index ) {
        MeshInstance& mesh_instance = mesh_instances[ mesh_index ];
        Mesh& mesh = *mesh_instance.mesh;

        if ( mesh.pbr_material.material != last_material ) {
            PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, mesh_instance.material_pass_index );

            gpu_commands->bind_pipeline( pipeline );

            last_material = mesh.pbr_material.material;
        }

        scene->draw_mesh( gpu_commands, mesh );
    }
}

void GBufferPass::prepare_draws( glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "gbuffer_pass" );
    if ( node == nullptr ) {
        RASSERT( false );
        return;
    }

    const u64 hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;

    material_creation.set_name( "material_no_cull" ).set_technique( main_technique ).set_render_index( 0 );
    Material* material = renderer->create_material( material_creation );

    glTF::glTF& gltf_scene = scene.gltf_scene;

    mesh_instances.init( resident_allocator, 16 );

    // Copy all mesh draws and change only material.
    for ( u32 i = 0; i < scene.meshes.size; ++i ) {

        // Skip transparent meshes
        Mesh* mesh = &scene.meshes[ i ];
        if ( mesh->is_transparent() ) {
            continue;
        }

        MeshInstance mesh_instance{};
        mesh_instance.mesh = mesh;
        mesh_instance.material_pass_index = 1;

        mesh_instances.push( mesh_instance );
    }

    //qsort( mesh_draws.data, mesh_draws.size, sizeof( MeshDraw ), gltf_mesh_material_compare );
}

void GBufferPass::free_gpu_resources() {
    GpuDevice& gpu = *renderer->gpu;

    mesh_instances.shutdown();
}

//
// LightPass //////////////////////////////////////////////////////////
void LighPass::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    glTFScene* scene = ( glTFScene* )render_scene;

    PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, 0 );

    gpu_commands->bind_pipeline( pipeline );
    gpu_commands->bind_vertex_buffer( mesh.position_buffer, 0, 0 );
    gpu_commands->bind_descriptor_set( &mesh.pbr_material.descriptor_set, 1, nullptr, 0 );

    gpu_commands->draw( TopologyType::Triangle, 0, 3, 0, 1 );
}

void LighPass::prepare_draws( glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "lighting_pass" );
    if ( node == nullptr ) {
        RASSERT( false );
        return;
    }

    const u64 hashed_name = hash_calculate( "pbr_lighting" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;

    material_creation.set_name( "material_pbr" ).set_technique( main_technique ).set_render_index( 0 );
    Material* material_pbr = renderer->create_material( material_creation );

    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuMeshData ) ).set_name( "mesh_data" );
    mesh.pbr_material.material_buffer = renderer->gpu->create_buffer( buffer_creation );

    DescriptorSetCreation ds_creation{};
    DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ 0 ].pipeline, k_material_descriptor_set_index );
    ds_creation.buffer( scene.scene_cb, 0 ).buffer( mesh.pbr_material.material_buffer, 1 ).set_layout( layout );
    mesh.pbr_material.descriptor_set = renderer->gpu->create_descriptor_set( ds_creation );

    BufferHandle fs_vb = renderer->gpu->get_fullscreen_vertex_buffer();
    mesh.position_buffer = fs_vb;

    FrameGraphResource* color_texture = frame_graph->access_resource( node->inputs[ 0 ] );
    FrameGraphResource* normal_texture = frame_graph->access_resource( node->inputs[ 1 ] );
    FrameGraphResource* roughness_texture = frame_graph->access_resource( node->inputs[ 2 ] );
    FrameGraphResource* position_texture = frame_graph->access_resource( node->inputs[ 3 ] );

    mesh.pbr_material.diffuse_texture_index = color_texture->resource_info.texture.texture.index;
    mesh.pbr_material.normal_texture_index = normal_texture->resource_info.texture.texture.index;
    mesh.pbr_material.roughness_texture_index = roughness_texture->resource_info.texture.texture.index;
    mesh.pbr_material.occlusion_texture_index = position_texture->resource_info.texture.texture.index;
    mesh.pbr_material.material = material_pbr;
}

void LighPass::upload_materials() {

    MapBufferParameters cb_map = { mesh.pbr_material.material_buffer, 0, 0 };
    GpuMeshData* mesh_data = ( GpuMeshData* )renderer->gpu->map_buffer( cb_map );
    if ( mesh_data ) {
        copy_gpu_material_data( *mesh_data, mesh );

        renderer->gpu->unmap_buffer( cb_map );
    }
}

void LighPass::free_gpu_resources() {
    GpuDevice& gpu = *renderer->gpu;

    gpu.destroy_buffer( mesh.pbr_material.material_buffer );
    gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set );
}

//
// TransparentPass ////////////////////////////////////////////////////////
void TransparentPass::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    glTFScene* scene = ( glTFScene* )render_scene;

    Material* last_material = nullptr;
    for ( u32 mesh_index = 0; mesh_index < mesh_instances.size; ++mesh_index ) {
        MeshInstance& mesh_instance = mesh_instances[ mesh_index ];
        Mesh& mesh = *mesh_instance.mesh;

        if ( mesh.pbr_material.material != last_material ) {
            PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, mesh_instance.material_pass_index );

            gpu_commands->bind_pipeline( pipeline );

            last_material = mesh.pbr_material.material;
        }

        scene->draw_mesh( gpu_commands, mesh );
    }
}

void TransparentPass::prepare_draws( glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "transparent_pass" );
    if ( node == nullptr ) {
        RASSERT( false );
        return;
    }

    // Create pipeline state
    PipelineCreation pipeline_creation;

    const u64 hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;

    material_creation.set_name( "material_transparent" ).set_technique( main_technique ).set_render_index( 0 );
    Material* material_depth_pre_pass = renderer->create_material( material_creation );

    glTF::glTF& gltf_scene = scene.gltf_scene;

    mesh_instances.init( resident_allocator, 16 );

    // Copy all mesh draws and change only material.
    for ( u32 i = 0; i < scene.meshes.size; ++i ) {

        // Skip transparent meshes
        Mesh* mesh = &scene.meshes[ i ];
        if ( !mesh->is_transparent() ) {
            continue;
        }

        MeshInstance mesh_instance{};
        mesh_instance.mesh = mesh;
        mesh_instance.material_pass_index = 4;

        mesh_instances.push( mesh_instance );
    }
}

void TransparentPass::free_gpu_resources() {
    GpuDevice& gpu = *renderer->gpu;

    mesh_instances.shutdown();
}

//
// DoFPass ////////////////////////////////////////////////////////////////
void DoFPass::add_ui() {
    ImGui::InputFloat( "Focal Length", &focal_length);
    ImGui::InputFloat( "Plane in Focus", &plane_in_focus);
    ImGui::InputFloat( "Aperture", &aperture);
}

void DoFPass::pre_render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    glTFScene* scene = ( glTFScene* )render_scene;

    FrameGraphResource* texture = ( FrameGraphResource* )scene->frame_graph->get_resource( "lighting" );
    RASSERT ( texture != nullptr );

    gpu_commands->copy_texture( texture->resource_info.texture.texture, RESOURCE_STATE_RENDER_TARGET, scene_mips->handle, RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
}

void DoFPass::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    glTFScene* scene = ( glTFScene* )render_scene;

    PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, 0 );

    gpu_commands->bind_pipeline( pipeline );
    gpu_commands->bind_vertex_buffer( mesh.position_buffer, 0, 0 );
    gpu_commands->bind_descriptor_set( &mesh.pbr_material.descriptor_set, 1, nullptr, 0 );

    gpu_commands->draw( TopologyType::Triangle, 0, 3, 0, 1 );
}

//TODO:
static TextureCreation dof_scene_tc;

void DoFPass::on_resize( GpuDevice& gpu, u32 new_width, u32 new_height ) {

    u32 w = new_width;
    u32 h = new_height;

    u32 mips = 1;
    while ( w > 1 && h > 1 ) {
        w /= 2;
        h /= 2;
        mips++;
    }

    // Destroy scene mips
    renderer->destroy_texture( scene_mips );

    // Reuse cached texture creation and create new scene mips.
    dof_scene_tc.set_flags( mips, 0 ).set_size( new_width, new_height, 1 );
    scene_mips = renderer->create_texture( dof_scene_tc );

    mesh.pbr_material.diffuse_texture_index = scene_mips->handle.index;
}

void DoFPass::prepare_draws( glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "depth_of_field_pass" );
    if ( node == nullptr ) {
        RASSERT( false );
        return;
    }

    const u64 hashed_name = hash_calculate( "depth_of_field" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;

    material_creation.set_name( "material_dof" ).set_technique( main_technique ).set_render_index( 0 );
    Material* material_dof = renderer->create_material( material_creation );

    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( DoFData ) ).set_name( "dof_data" );
    mesh.pbr_material.material_buffer = renderer->gpu->create_buffer( buffer_creation );

    DescriptorSetCreation ds_creation{};
    DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ 0 ].pipeline, k_material_descriptor_set_index );
    ds_creation.buffer( mesh.pbr_material.material_buffer, 0 ).set_layout( layout );
    mesh.pbr_material.descriptor_set = renderer->gpu->create_descriptor_set( ds_creation );

    BufferHandle fs_vb = renderer->gpu->get_fullscreen_vertex_buffer();
    mesh.position_buffer = fs_vb;

    FrameGraphResource* color_texture = frame_graph->access_resource( node->inputs[ 0 ] );
    FrameGraphResource* depth_texture_reference = frame_graph->access_resource( node->inputs[ 1 ] );

    FrameGraphResource* depth_texture = frame_graph->get_resource( depth_texture_reference->name );
    RASSERT( depth_texture != nullptr );

    FrameGraphResourceInfo& info = color_texture->resource_info;
    u32 w = info.texture.width;
    u32 h = info.texture.height;

    u32 mips = 1;
    while ( w > 1 && h > 1) {
        w /= 2;
        h /= 2;
        mips++;
    }

    dof_scene_tc.set_data( nullptr ).set_format_type( info.texture.format, TextureType::Texture2D ).set_flags( mips, 0 ).set_size( ( u16 )info.texture.width, ( u16 )info.texture.height, 1 ).set_name( "scene_mips" );
    scene_mips = renderer->create_texture( dof_scene_tc );

    mesh.pbr_material.diffuse_texture_index = scene_mips->handle.index;
    mesh.pbr_material.roughness_texture_index = depth_texture->resource_info.texture.texture.index;
    mesh.pbr_material.material = material_dof;

    znear = 0.1f;
    zfar = 1000.0f;
    focal_length = 5.0f;
    plane_in_focus = 1.0f;
    aperture = 8.0f;
}

void DoFPass::upload_materials() {

    MapBufferParameters cb_map = { mesh.pbr_material.material_buffer, 0, 0 };
    DoFData* dof_data = ( DoFData* )renderer->gpu->map_buffer( cb_map );
    if ( dof_data ) {
        dof_data->textures[ 0 ] = mesh.pbr_material.diffuse_texture_index;
        dof_data->textures[ 1 ] = mesh.pbr_material.roughness_texture_index;

        dof_data->znear = znear;
        dof_data->zfar = zfar;
        dof_data->focal_length = focal_length;
        dof_data->plane_in_focus = plane_in_focus;
        dof_data->aperture = aperture;

        renderer->gpu->unmap_buffer( cb_map );
    }
}

void DoFPass::free_gpu_resources() {
    GpuDevice& gpu = *renderer->gpu;

    renderer->destroy_texture( scene_mips );
    gpu.destroy_buffer( mesh.pbr_material.material_buffer );
    gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set );
}

//
// glTFScene //////////////////////////////////////////////////////////////
void glTFScene::draw_mesh( CommandBuffer* gpu_commands, Mesh& mesh ) {

    gpu_commands->bind_vertex_buffer( mesh.position_buffer, 0, mesh.position_offset );
    gpu_commands->bind_vertex_buffer( mesh.tangent_buffer, 1, mesh.tangent_offset );
    gpu_commands->bind_vertex_buffer( mesh.normal_buffer, 2, mesh.normal_offset );
    gpu_commands->bind_vertex_buffer( mesh.texcoord_buffer, 3, mesh.texcoord_offset );
    gpu_commands->bind_index_buffer( mesh.index_buffer, mesh.index_offset, mesh.index_type );

    if ( recreate_per_thread_descriptors ) {
        DescriptorSetCreation ds_creation{};
        ds_creation.buffer( scene_cb, 0 ).buffer( mesh.pbr_material.material_buffer, 1 );
        DescriptorSetHandle descriptor_set = renderer->create_descriptor_set( gpu_commands, mesh.pbr_material.material, ds_creation );

        gpu_commands->bind_local_descriptor_set( &descriptor_set, 1, nullptr, 0 );
    }
    else {
        gpu_commands->bind_descriptor_set( &mesh.pbr_material.descriptor_set, 1, nullptr, 0 );
    }

    gpu_commands->draw_indexed( TopologyType::Triangle, mesh.primitive_count, 1, 0, 0, 0 );
}

void glTFScene::get_mesh_vertex_buffer( i32 accessor_index, BufferHandle& out_buffer_handle, u32& out_buffer_offset ) {
    if ( accessor_index != -1 ) {
        glTF::Accessor& buffer_accessor = gltf_scene.accessors[ accessor_index ];
        glTF::BufferView& buffer_view = gltf_scene.buffer_views[ buffer_accessor.buffer_view ];
        BufferResource& buffer_gpu = buffers[ buffer_accessor.buffer_view ];

        out_buffer_handle = buffer_gpu.handle;
        out_buffer_offset = buffer_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : buffer_accessor.byte_offset;
    }
}

void glTFScene::fill_pbr_material( Renderer& renderer, glTF::Material& material, PBRMaterial& pbr_material ) {
    GpuDevice& gpu = *renderer.gpu;

    // Handle flags
    if ( material.alpha_mode.data != nullptr && strcmp( material.alpha_mode.data, "MASK" ) == 0 ) {
        pbr_material.flags |= DrawFlags_AlphaMask;
    } else if ( material.alpha_mode.data != nullptr && strcmp( material.alpha_mode.data, "BLEND" ) == 0 ) {
        pbr_material.flags |= DrawFlags_Transparent;
    }

    pbr_material.flags |= material.double_sided ? DrawFlags_DoubleSided : 0;
    // Alpha cutoff
    pbr_material.alpha_cutoff = material.alpha_cutoff != glTF::INVALID_FLOAT_VALUE ? material.alpha_cutoff : 1.f;

    if ( material.pbr_metallic_roughness != nullptr ) {
        if ( material.pbr_metallic_roughness->base_color_factor_count != 0 ) {
            RASSERT( material.pbr_metallic_roughness->base_color_factor_count == 4 );

            memcpy( pbr_material.base_color_factor.raw, material.pbr_metallic_roughness->base_color_factor, sizeof( vec4s ) );
        } else {
            pbr_material.base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
        }

        pbr_material.metallic_roughness_occlusion_factor.x = material.pbr_metallic_roughness->roughness_factor != glTF::INVALID_FLOAT_VALUE ? material.pbr_metallic_roughness->roughness_factor : 1.f;
        pbr_material.metallic_roughness_occlusion_factor.y = material.pbr_metallic_roughness->metallic_factor != glTF::INVALID_FLOAT_VALUE ? material.pbr_metallic_roughness->metallic_factor : 1.f;

        pbr_material.diffuse_texture_index = get_material_texture( gpu, material.pbr_metallic_roughness->base_color_texture );
        pbr_material.roughness_texture_index = get_material_texture( gpu, material.pbr_metallic_roughness->metallic_roughness_texture );
    }

    pbr_material.occlusion_texture_index = get_material_texture( gpu, ( material.occlusion_texture != nullptr ) ? material.occlusion_texture->index : -1 );
    pbr_material.normal_texture_index = get_material_texture( gpu, ( material.normal_texture != nullptr ) ? material.normal_texture->index : -1 );

    if ( material.occlusion_texture != nullptr ) {
        if ( material.occlusion_texture->strength != glTF::INVALID_FLOAT_VALUE ) {
            pbr_material.metallic_roughness_occlusion_factor.z = material.occlusion_texture->strength;
        } else {
            pbr_material.metallic_roughness_occlusion_factor.z = 1.0f;
        }
    }
}

u16 glTFScene::get_material_texture( GpuDevice& gpu, glTF::TextureInfo* texture_info ) {
    if ( texture_info != nullptr ) {
        glTF::Texture& gltf_texture = gltf_scene.textures[ texture_info->index ];
        TextureResource& texture_gpu = images[ gltf_texture.source ];
        SamplerResource& sampler_gpu = samplers[ gltf_texture.sampler ];

        gpu.link_texture_sampler( texture_gpu.handle, sampler_gpu.handle );

        return texture_gpu.handle.index;
    }
    else {
        return k_invalid_scene_texture_index;
    }
}

u16 glTFScene::get_material_texture( GpuDevice& gpu, i32 gltf_texture_index ) {
    if ( gltf_texture_index >= 0 ) {
        glTF::Texture& gltf_texture = gltf_scene.textures[ gltf_texture_index ];
        TextureResource& texture_gpu = images[ gltf_texture.source ];
        SamplerResource& sampler_gpu = samplers[ gltf_texture.sampler ];

        gpu.link_texture_sampler( texture_gpu.handle, sampler_gpu.handle );

        return texture_gpu.handle.index;
    } else {
        return k_invalid_scene_texture_index;
    }
}

void glTFScene::init( cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) {
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
        glTF::Image& image = gltf_scene.images[ image_index ];

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

        TextureCreation tc;
        tc.set_data( nullptr ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D ).set_flags( mip_levels, 0 ).set_size( ( u16 )width, ( u16 )height, 1 ).set_name( image.uri.data );
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
    Array<void*> buffers_data;
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
    meshes.init( resident_allocator, gltf_scene.meshes_count );

    i64 end_loading = time_now();

    rprint( "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating %f seconds\n\tCreating Samplers %f seconds\n\tReading Buffers Data %f seconds\n\tCreating Buffers %f seconds\n", filename,
            time_delta_seconds( start_scene_loading, end_loading ), time_delta_seconds( start_scene_loading, end_loading_file ), time_delta_seconds( end_loading_file, end_creating_textures ),
            time_delta_seconds( end_creating_textures, end_creating_samplers ),
            time_delta_seconds( end_creating_samplers, end_reading_buffers_data ), time_delta_seconds( end_reading_buffers_data, end_creating_buffers ) );
}

void glTFScene::shutdown( Renderer* renderer ) {
    GpuDevice& gpu = *renderer->gpu;

    for ( u32 mesh_index = 0; mesh_index < meshes.size; ++mesh_index ) {
        Mesh& mesh = meshes[ mesh_index ];

        gpu.destroy_buffer( mesh.pbr_material.material_buffer );
        gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set );
    }

    gpu.destroy_descriptor_set( fullscreen_ds );
    gpu.destroy_buffer( scene_cb );

    for ( u32 i = 0; i < images.size; ++i) {
        renderer->destroy_texture( &images[ i ] );
    }

    for ( u32 i = 0; i < samplers.size; ++i ) {
        renderer->destroy_sampler( &samplers[ i ] );
    }

    for ( u32 i = 0; i < buffers.size; ++i ) {
        renderer->destroy_buffer( &buffers[ i ] );
    }

    meshes.shutdown();

    depth_pre_pass.free_gpu_resources();
    gbuffer_pass.free_gpu_resources();
    light_pass.free_gpu_resources();
    transparent_pass.free_gpu_resources();
    dof_pass.free_gpu_resources();

    // Free scene buffers
    samplers.shutdown();
    images.shutdown();
    buffers.shutdown();

    // NOTE(marco): we can't destroy this sooner as textures and buffers
    // hold a pointer to the names stored here
    gltf_free( gltf_scene );
}

void glTFScene::register_render_passes( FrameGraph* frame_graph_ ) {
    frame_graph = frame_graph_;

    frame_graph->builder->register_render_pass( "depth_pre_pass", &depth_pre_pass );
    frame_graph->builder->register_render_pass( "gbuffer_pass", &gbuffer_pass );
    frame_graph->builder->register_render_pass( "lighting_pass", &light_pass );
    frame_graph->builder->register_render_pass( "transparent_pass", &transparent_pass );
    frame_graph->builder->register_render_pass( "depth_of_field_pass", &dof_pass );
}

void glTFScene::prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph_ ) {

    scene_graph = scene_graph_;

    sizet cached_scratch_size = scratch_allocator->get_marker();

    // Scene constant buffer
    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuSceneData ) ).set_name( "scene_cb" );
    scene_cb = renderer->gpu->create_buffer( buffer_creation );

    // Create material
    const u64 hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;
    material_creation.set_name( "material_no_cull_opaque" ).set_technique( main_technique ).set_render_index( 0 );

    Material* pbr_material = renderer->create_material( material_creation );

    glTF::Scene& root_gltf_scene = gltf_scene.scenes[ gltf_scene.scene ];

    //
    Array<i32> nodes_to_visit;
    nodes_to_visit.init( scratch_allocator, 4 );

    // Calculate total node count: add first the root nodes.
    u32 total_node_count = root_gltf_scene.nodes_count;

    // Add initial nodes
    for ( u32 node_index = 0; node_index < root_gltf_scene.nodes_count; ++node_index ) {
        const i32 node = root_gltf_scene.nodes[ node_index ];
        nodes_to_visit.push( node );
    }
    // Visit nodes
    while ( nodes_to_visit.size ) {
        i32 node_index = nodes_to_visit.front();
        nodes_to_visit.delete_swap( 0 );

        glTF::Node& node = gltf_scene.nodes[ node_index ];
        for ( u32 ch = 0; ch < node.children_count; ++ch ) {
            const i32 children_index = node.children[ ch ];
            nodes_to_visit.push( children_index );
        }

        // Add only children nodes to the count, as the current node is
        // already calculated when inserting it.
        total_node_count += node.children_count;
    }

    scene_graph->resize( total_node_count );

    // Populate scene graph: visit again
    nodes_to_visit.clear();
    // Add initial nodes
    for ( u32 node_index = 0; node_index < root_gltf_scene.nodes_count; ++node_index ) {
        const i32 node = root_gltf_scene.nodes[ node_index ];
        nodes_to_visit.push( node );
    }

    while ( nodes_to_visit.size ) {
        i32 node_index = nodes_to_visit.front();
        nodes_to_visit.delete_swap( 0 );

        glTF::Node& node = gltf_scene.nodes[ node_index ];

        // Compute local transform: read either raw matrix or individual Scale/Rotation/Translation components
        if ( node.matrix_count ) {
            // CGLM and glTF have the same matrix layout, just memcopy it
            memcpy( &scene_graph->local_matrices[ node_index ], node.matrix, sizeof( mat4s ) );
            scene_graph->updated_nodes.set_bit( node_index );
        }
        else {
            // Handle individual transform components: SRT (scale, rotation, translation)
            vec3s node_scale{ 1.0f, 1.0f, 1.0f };
            if ( node.scale_count ) {
                RASSERT( node.scale_count == 3 );
                node_scale = vec3s{ node.scale[ 0 ], node.scale[ 1 ], node.scale[ 2 ] };
            }
            mat4s scale_matrix = glms_scale_make( node_scale );

            vec3s translation{ 0.f, 0.f, 0.f };
            if ( node.translation_count ) {
                RASSERT( node.translation_count == 3 );
                translation = vec3s{ node.translation[ 0 ], node.translation[ 1 ], node.translation[ 2 ] };
            }
            mat4s translation_matrix = glms_translate_make( translation );
            // Rotation is written as a plain quaternion
            versors rotation = glms_quat_identity();
            if ( node.rotation_count ) {
                RASSERT( node.rotation_count == 4 );
                rotation = glms_quat_init( node.rotation[ 0 ], node.rotation[ 1 ], node.rotation[ 2 ], node.rotation[ 3 ] );
            }
            // Final SRT composition
            const mat4s local_matrix = glms_mat4_mul( glms_mat4_mul( scale_matrix, glms_quat_mat4( rotation ) ), translation_matrix );
            scene_graph->set_local_matrix( node_index, local_matrix );
        }

        // Handle parent-relationship
        if ( node.children_count ) {
            const Hierarchy& node_hierarchy = scene_graph->nodes_hierarchy[ node_index ];

            for ( u32 ch = 0; ch < node.children_count; ++ch) {
                const i32 children_index = node.children[ ch ];
                Hierarchy& children_hierarchy = scene_graph->nodes_hierarchy[ children_index ];
                scene_graph->set_hierarchy( children_index, node_index, node_hierarchy.level + 1 );

                nodes_to_visit.push( children_index );
            }
        }

        if ( node.mesh == glTF::INVALID_INT_VALUE ) {
            continue;
        }

        glTF::Mesh& gltf_mesh = gltf_scene.meshes[ node.mesh ];

        // Gltf primitives are conceptually submeshes.
        for ( u32 primitive_index = 0; primitive_index < gltf_mesh.primitives_count; ++primitive_index ) {
            Mesh mesh{ };
            // Assign scene graph node index
            mesh.scene_graph_node_index = node_index;

            glTF::MeshPrimitive& mesh_primitive = gltf_mesh.primitives[ primitive_index ];

            const i32 position_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "POSITION" );
            const i32 tangent_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "TANGENT" );
            const i32 normal_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "NORMAL" );
            const i32 texcoord_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "TEXCOORD_0" );

            get_mesh_vertex_buffer( position_accessor_index, mesh.position_buffer, mesh.position_offset );
            get_mesh_vertex_buffer( tangent_accessor_index, mesh.tangent_buffer, mesh.tangent_offset );
            get_mesh_vertex_buffer( normal_accessor_index, mesh.normal_buffer, mesh.normal_offset );
            get_mesh_vertex_buffer( texcoord_accessor_index, mesh.texcoord_buffer, mesh.texcoord_offset );

            // Create index buffer
            glTF::Accessor& indices_accessor = gltf_scene.accessors[ mesh_primitive.indices ];
            RASSERT( indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_SHORT || indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_INT );
            mesh.index_type = ( indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_SHORT ) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

            glTF::BufferView& indices_buffer_view = gltf_scene.buffer_views[ indices_accessor.buffer_view ];
            BufferResource& indices_buffer_gpu = buffers[ indices_accessor.buffer_view ];
            mesh.index_buffer = indices_buffer_gpu.handle;
            mesh.index_offset = indices_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : indices_accessor.byte_offset;
            mesh.primitive_count = indices_accessor.count;

            // Read pbr material data
            glTF::Material& material = gltf_scene.materials[ mesh_primitive.material ];
            fill_pbr_material( *renderer, material, mesh.pbr_material );

            // Create material buffer
            BufferCreation buffer_creation;
            buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuMeshData ) ).set_name( "mesh_data" );
            mesh.pbr_material.material_buffer = renderer->gpu->create_buffer( buffer_creation );

            DescriptorSetCreation ds_creation{};
            DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ 3 ].pipeline, k_material_descriptor_set_index );
            ds_creation.buffer( scene_cb, 0 ).buffer( mesh.pbr_material.material_buffer, 1 ).set_layout( layout );
            mesh.pbr_material.descriptor_set = renderer->gpu->create_descriptor_set( ds_creation );

            mesh.pbr_material.material = pbr_material;

            meshes.push(mesh);
        }
    }

    qsort( meshes.data, meshes.size, sizeof( Mesh ), gltf_mesh_material_compare );

    scratch_allocator->free_marker( cached_scratch_size );

    depth_pre_pass.prepare_draws( *this, frame_graph, renderer->gpu->allocator, scratch_allocator );
    gbuffer_pass.prepare_draws( *this, frame_graph, renderer->gpu->allocator, scratch_allocator );
    light_pass.prepare_draws( *this, frame_graph, renderer->gpu->allocator, scratch_allocator );
    transparent_pass.prepare_draws( *this, frame_graph, renderer->gpu->allocator, scratch_allocator );
    dof_pass.prepare_draws( *this, frame_graph, renderer->gpu->allocator, scratch_allocator );

    // Handle fullscreen pass.
    fullscreen_tech = renderer->resource_cache.techniques.get( hash_calculate( "fullscreen" ) );

    DescriptorSetCreation dsc;
    DescriptorSetLayoutHandle descriptor_set_layout = renderer->gpu->get_descriptor_set_layout( fullscreen_tech->passes[ 0 ].pipeline, k_material_descriptor_set_index );
    dsc.reset().buffer( scene_cb, 0 ).set_layout( descriptor_set_layout );
    fullscreen_ds = renderer->gpu->create_descriptor_set( dsc );

    FrameGraphResource* texture = frame_graph->get_resource( "final" );
    if ( texture != nullptr ) {
        fullscreen_input_rt = texture->resource_info.texture.texture.index;
    }
}

void glTFScene::upload_materials() {
    // Update per mesh material buffer
    for ( u32 mesh_index = 0; mesh_index < meshes.size; ++mesh_index ) {
        Mesh& mesh = meshes[ mesh_index ];

        MapBufferParameters cb_map = { mesh.pbr_material.material_buffer, 0, 0 };
        GpuMeshData* mesh_data = ( GpuMeshData* )renderer->gpu->map_buffer( cb_map );
        if ( mesh_data ) {
            copy_gpu_material_data( *mesh_data, mesh );
            copy_gpu_mesh_matrix( *mesh_data, mesh, global_scale, scene_graph );

            renderer->gpu->unmap_buffer( cb_map );
        }
    }

    light_pass.upload_materials();
    dof_pass.upload_materials();
}

void glTFScene::submit_draw_task( ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler ) {
    glTFDrawTask draw_task;
    draw_task.init( renderer->gpu, frame_graph, renderer, imgui, gpu_profiler, this );
    task_scheduler->AddTaskSetToPipe( &draw_task );
    task_scheduler->WaitforTaskSet( &draw_task );

    // Avoid using the same command buffer
    renderer->add_texture_update_commands( ( draw_task.thread_id + 1 ) % task_scheduler->GetNumTaskThreads() );
}

// glTFDrawTask ///////////////////////////////////////////////////////////
void glTFDrawTask::init( GpuDevice* gpu_, FrameGraph* frame_graph_, Renderer* renderer_,
                         ImGuiService* imgui_, GPUProfiler* gpu_profiler_, glTFScene* scene_ ) {
    gpu = gpu_;
    frame_graph = frame_graph_;
    renderer = renderer_;
    imgui = imgui_;
    gpu_profiler = gpu_profiler_;
    scene = scene_;
}

void glTFDrawTask::ExecuteRange( enki::TaskSetPartition range_, uint32_t threadnum_ ) {
    ZoneScoped;

    using namespace raptor;

    thread_id = threadnum_;

    //rprint( "Executing draw task from thread %u\n", threadnum_ );
    // TODO: improve getting a command buffer/pool
    CommandBuffer* gpu_commands = gpu->get_command_buffer( threadnum_, true );
    gpu_commands->push_marker( "Frame" );

    frame_graph->render( gpu_commands, scene );

    gpu_commands->push_marker( "Fullscreen" );
    gpu_commands->clear( 0.3f, 0.3f, 0.3f, 1.f );
    gpu_commands->clear_depth_stencil( 1.0f, 0 );
    gpu_commands->bind_pass( gpu->get_swapchain_pass(), gpu->get_current_framebuffer(), false );
    gpu_commands->set_scissor( nullptr );
    gpu_commands->set_viewport( nullptr );

    // TODO: add global switch
    if ( false ) {
        Material* last_material = nullptr;
        // TODO(marco): loop by material so that we can deal with multiple passes
        for ( u32 mesh_index = 0; mesh_index < scene->meshes.size; ++mesh_index ) {
            Mesh& mesh = scene->meshes[ mesh_index ];

            if ( mesh.pbr_material.material != last_material ) {
                PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, 3 );

                gpu_commands->bind_pipeline( pipeline );

                last_material = mesh.pbr_material.material;
            }

            scene->draw_mesh( gpu_commands, mesh );
        }
    }
    else {
        // Apply fullscreen material
        gpu_commands->bind_pipeline( scene->fullscreen_tech->passes[ 0 ].pipeline );
        gpu_commands->bind_descriptor_set( &scene->fullscreen_ds, 1, nullptr, 0 );
        gpu_commands->draw( TopologyType::Triangle, 0, 3, scene->fullscreen_input_rt, 1 );
    }

    imgui->render( *gpu_commands, false );

    gpu_commands->pop_marker(); // Fullscreen marker
    gpu_commands->pop_marker(); // Frame marker

    gpu_profiler->update( *gpu );

    // Send commands to GPU
    gpu->queue_command_buffer( gpu_commands );
}

} // namespace raptor
