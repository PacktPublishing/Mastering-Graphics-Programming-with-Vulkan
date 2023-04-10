#include "graphics/render_scene.hpp"
#include "graphics/renderer.hpp"
#include "graphics/scene_graph.hpp"
#include "graphics/asynchronous_loader.hpp"
#include "graphics/raptor_imgui.hpp"
#include "graphics/gpu_profiler.hpp"

#include "foundation/time.hpp"
#include "foundation/numerics.hpp"

#include "external/imgui/imgui.h"
#include "external/stb_image.h"

#include "external/cglm/struct/affine.h"
#include "external/cglm/struct/mat4.h"
#include "external/cglm/struct/vec3.h"
#include "external/cglm/struct/quat.h"

#include "external/stb_image.h"
#include "external/tracy/tracy/Tracy.hpp"

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace raptor {


static int mesh_material_compare( const void* a, const void* b ) {
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

    gpu_mesh_data.emissive = { mesh.pbr_material.emissive_factor.x, mesh.pbr_material.emissive_factor.y, mesh.pbr_material.emissive_factor.z, ( float )mesh.pbr_material.emissive_texture_index };

    gpu_mesh_data.base_color_factor = mesh.pbr_material.base_color_factor;
    gpu_mesh_data.metallic_roughness_occlusion_factor = mesh.pbr_material.metallic_roughness_occlusion_factor;
    gpu_mesh_data.alpha_cutoff = mesh.pbr_material.alpha_cutoff;

    gpu_mesh_data.diffuse_colour = mesh.pbr_material.diffuse_colour;
    gpu_mesh_data.specular_colour = mesh.pbr_material.specular_colour;
    gpu_mesh_data.specular_exp = mesh.pbr_material.specular_exp;
    gpu_mesh_data.ambient_colour = mesh.pbr_material.ambient_colour;

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
// PhysicsVertex ///////////////////////////////////////////////////////
void PhysicsVertex::add_joint( u32 vertex_index ) {
    for ( u32 j = 0; j < joint_count; ++j ) {
        if ( joints[ j ].vertex_index == vertex_index ) {
            return;
        }
    }

    RASSERT( joint_count < k_max_joint_count );
    joints[ joint_count++ ].vertex_index = vertex_index;
}

//
// DepthPrePass ///////////////////////////////////////////////////////
void DepthPrePass::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {

    Material* last_material = nullptr;
    for ( u32 mesh_index = 0; mesh_index < mesh_instances.size; ++mesh_index ) {
        MeshInstance& mesh_instance = mesh_instances[ mesh_index ];
        Mesh& mesh = *mesh_instance.mesh;

        if ( mesh.pbr_material.material != last_material ) {
            PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, mesh_instance.material_pass_index );

            gpu_commands->bind_pipeline( pipeline );

            last_material = mesh.pbr_material.material;
        }

        render_scene->draw_mesh( gpu_commands, mesh );
    }
}

void DepthPrePass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
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

    mesh_instances.init( resident_allocator, 16 );

    // Copy all mesh draws and change only material.
    for ( u32 i = 0; i < scene.meshes.size; ++i ) {

        Mesh* mesh = &scene.meshes[ i ];
        if ( mesh->is_transparent() ) {
            continue;
        }

        MeshInstance mesh_instance{};
        mesh_instance.mesh = mesh;
        mesh_instance.material_pass_index = mesh->has_skinning() ? main_technique->name_hash_to_index.get( hash_calculate( "depth_pre_skinning" ) ) : main_technique->name_hash_to_index.get( hash_calculate( "depth_pre" ) );

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

    Material* last_material = nullptr;
    for ( u32 mesh_index = 0; mesh_index < mesh_instances.size; ++mesh_index ) {
        MeshInstance& mesh_instance = mesh_instances[ mesh_index ];
        Mesh& mesh = *mesh_instance.mesh;

        if ( mesh.pbr_material.material != last_material ) {
            PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, mesh_instance.material_pass_index );

            gpu_commands->bind_pipeline( pipeline );

            last_material = mesh.pbr_material.material;
        }

        render_scene->draw_mesh( gpu_commands, mesh );
    }
}

void GBufferPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
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
        mesh_instance.material_pass_index = mesh->has_skinning() ? main_technique->name_hash_to_index.get( hash_calculate( "gbuffer_skinning" ) ) : main_technique->name_hash_to_index.get( hash_calculate( "gbuffer_cull" ) );

        mesh_instances.push( mesh_instance );
    }

    //qsort( mesh_draws.data, mesh_draws.size, sizeof( MeshDraw ), gltf_mesh_material_compare );
}

void GBufferPass::free_gpu_resources() {
    GpuDevice& gpu = *renderer->gpu;

    mesh_instances.shutdown();
}

//
// LightPass //////////////////////////////////////////////////////////////

//
//
struct LightingConstants {
    u32             albedo_index;
    u32             rmo_index;
    u32             normal_index;
    u32             depth_index;

    u32             output_index;
    u32             output_width;
    u32             output_height;
    u32             emissive;
}; // struct LightingConstants

void LighPass::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {

    if ( use_compute ) {
        PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, 1 );
        gpu_commands->bind_pipeline( pipeline );
        gpu_commands->bind_descriptor_set( &mesh.pbr_material.descriptor_set, 1, nullptr, 0 );

        gpu_commands->dispatch( ceilu32( renderer->gpu->swapchain_width * 1.f / 8 ), ceilu32( renderer->gpu->swapchain_height * 1.f / 8 ), 1 );
    } else {
        PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, 0 );

        gpu_commands->bind_pipeline( pipeline );
        gpu_commands->bind_vertex_buffer( mesh.position_buffer, 0, 0 );
        gpu_commands->bind_descriptor_set( &mesh.pbr_material.descriptor_set, 1, nullptr, 0 );

        gpu_commands->draw( TopologyType::Triangle, 0, 3, 0, 1 );
    }
}

void LighPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "lighting_pass" );
    if ( node == nullptr ) {
        RASSERT( false );
        return;
    }

    use_compute = node->compute;

    const u64 hashed_name = hash_calculate( "pbr_lighting" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;

    material_creation.set_name( "material_pbr" ).set_technique( main_technique ).set_render_index( 0 );
    Material* material_pbr = renderer->create_material( material_creation );

    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( LightingConstants ) ).set_name( "lighting_constants" );
    mesh.pbr_material.material_buffer = renderer->gpu->create_buffer( buffer_creation );

    const u32 pass_index = use_compute ? 1 : 0;
    DescriptorSetCreation ds_creation{};
    DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ pass_index ].pipeline, k_material_descriptor_set_index );
    ds_creation.buffer( scene.scene_cb, 0 ).buffer( mesh.pbr_material.material_buffer, 1 ).set_layout( layout );
    mesh.pbr_material.descriptor_set = renderer->gpu->create_descriptor_set( ds_creation );

    BufferHandle fs_vb = renderer->gpu->get_fullscreen_vertex_buffer();
    mesh.position_buffer = fs_vb;

    color_texture = frame_graph->access_resource( node->inputs[ 0 ] );
    normal_texture = frame_graph->access_resource( node->inputs[ 1 ] );
    roughness_texture = frame_graph->access_resource( node->inputs[ 2 ] );
    emissive_texture = frame_graph->access_resource( node->inputs[ 3 ] );
    depth_texture = frame_graph->access_resource( node->inputs[ 4 ] );

    output_texture = frame_graph->access_resource( node->outputs[ 0 ] );

    mesh.pbr_material.material = material_pbr;
}

void LighPass::upload_gpu_data() {

    u32 current_frame_index = renderer->gpu->current_frame;

    MapBufferParameters cb_map = { mesh.pbr_material.material_buffer, 0, 0 };
    LightingConstants* lighting_data = ( LightingConstants* )renderer->gpu->map_buffer( cb_map );
    if ( lighting_data ) {
        lighting_data->albedo_index = color_texture->resource_info.texture.handle[ current_frame_index ].index;;
        lighting_data->rmo_index = roughness_texture->resource_info.texture.handle[ current_frame_index ].index;
        lighting_data->normal_index = normal_texture->resource_info.texture.handle[ current_frame_index ].index;
        lighting_data->depth_index = depth_texture->resource_info.texture.handle[ current_frame_index ].index;
        lighting_data->output_index = output_texture->resource_info.texture.handle[ current_frame_index ].index;
        lighting_data->output_width = renderer->width;
        lighting_data->output_height = renderer->height;
        lighting_data->emissive = emissive_texture->resource_info.texture.handle[ current_frame_index ].index;

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

    Material* last_material = nullptr;
    for ( u32 mesh_index = 0; mesh_index < mesh_instances.size; ++mesh_index ) {
        MeshInstance& mesh_instance = mesh_instances[ mesh_index ];
        Mesh& mesh = *mesh_instance.mesh;

        if ( mesh.pbr_material.material != last_material ) {
            PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, mesh_instance.material_pass_index );

            gpu_commands->bind_pipeline( pipeline );

            last_material = mesh.pbr_material.material;
        }

        render_scene->draw_mesh( gpu_commands, mesh );
    }
}

void TransparentPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
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
        mesh_instance.material_pass_index = mesh->has_skinning() ? main_technique->name_hash_to_index.get( hash_calculate("transparent_skinning_no_cull") ) : main_technique->name_hash_to_index.get( hash_calculate( "transparent_no_cull" ) );

        mesh_instances.push( mesh_instance );
    }
}

void TransparentPass::free_gpu_resources() {

    mesh_instances.shutdown();
}

//
// DebugPass ////////////////////////////////////////////////////////
void DebugPass::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {

    PipelineHandle pipeline = renderer->get_pipeline( debug_material, 0 );

    gpu_commands->bind_pipeline( pipeline );

    for ( u32 mesh_index = 0; mesh_index < mesh_instances.size; ++mesh_index ) {
        MeshInstance& mesh_instance = mesh_instances[ mesh_index ];
        Mesh& mesh = *mesh_instance.mesh;

        if ( mesh.physics_mesh != nullptr ) {
            PhysicsMesh* physics_mesh = mesh.physics_mesh;

            gpu_commands->bind_vertex_buffer( sphere_mesh_buffer->handle, 0, 0 );
            gpu_commands->bind_index_buffer( sphere_mesh_indices->handle, 0, VK_INDEX_TYPE_UINT32 );

            gpu_commands->bind_descriptor_set( &physics_mesh->debug_mesh_descriptor_set, 1, nullptr, 0 );

            gpu_commands->draw_indexed( TopologyType::Triangle, sphere_index_count, physics_mesh->vertices.size, 0, 0, 0 );
        }
    }

    pipeline = renderer->get_pipeline( debug_material, 1 );

    gpu_commands->bind_pipeline( pipeline );

    for ( u32 mesh_index = 0; mesh_index < mesh_instances.size; ++mesh_index ) {
        MeshInstance& mesh_instance = mesh_instances[ mesh_index ];
        Mesh& mesh = *mesh_instance.mesh;

        if ( mesh.physics_mesh != nullptr ) {
            PhysicsMesh* physics_mesh = mesh.physics_mesh;

            gpu_commands->bind_descriptor_set( &physics_mesh->debug_mesh_descriptor_set, 1, nullptr, 0 );

            gpu_commands->draw_indirect( physics_mesh->draw_indirect_buffer, physics_mesh->vertices.size, 0, sizeof( VkDrawIndirectCommand  ) );
        }
    }
}

void DebugPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;
    scene_graph = scene.scene_graph;

    FrameGraphNode* node = frame_graph->get_node( "debug_pass" );
    if ( node == nullptr ) {
        RASSERT( false );
        return;
    }

    // Create pipeline state
    PipelineCreation pipeline_creation;

    const u64 hashed_name = hash_calculate( "debug" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;

    material_creation.set_name( "material_debug" ).set_technique( main_technique ).set_render_index( 0 );
    debug_material = renderer->create_material( material_creation );

    sizet marker = scratch_allocator->get_marker();

    StringBuffer mesh_name;
    mesh_name.init( 1024, scratch_allocator );
    cstring filename = mesh_name.append_use_f( "%s/sphere.obj", RAPTOR_DATA_FOLDER );

    const aiScene* sphere_mesh = aiImportFile( filename,
        aiProcess_CalcTangentSpace       |
        aiProcess_GenNormals             |
        aiProcess_Triangulate            |
        aiProcess_JoinIdenticalVertices  |
        aiProcess_SortByPType);

    scratch_allocator->free_marker( marker );

    Array<vec3s> positions;
    positions.init( resident_allocator, rkilo( 64 ) );

    Array<u32> indices;
    indices.init( resident_allocator, rkilo( 64 ) );

    sphere_index_count = 0;

    for ( u32 mesh_index = 0; mesh_index < sphere_mesh->mNumMeshes; ++mesh_index ) {
        aiMesh* mesh = sphere_mesh->mMeshes[ mesh_index ];

        RASSERT( ( mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE ) != 0 );

        for ( u32 vertex_index = 0; vertex_index < mesh->mNumVertices; ++vertex_index ) {
            vec3s position{
                mesh->mVertices[ vertex_index ].x,
                mesh->mVertices[ vertex_index ].y,
                mesh->mVertices[ vertex_index ].z
            };

            positions.push( position );
        }

        for ( u32 face_index = 0; face_index < mesh->mNumFaces; ++face_index ) {
            RASSERT( mesh->mFaces[ face_index ].mNumIndices == 3 );

            u32 index_a = mesh->mFaces[ face_index ].mIndices[ 0 ];
            u32 index_b = mesh->mFaces[ face_index ].mIndices[ 1 ];
            u32 index_c = mesh->mFaces[ face_index ].mIndices[ 2 ];

            indices.push( index_a );
            indices.push( index_b );
            indices.push( index_c );
        }

        sphere_index_count = indices.size;
    }

    {
        BufferCreation creation{ };
        sizet buffer_size = positions.size * sizeof( vec3s );
        creation.set( VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( positions.data ).set_name( "debug_sphere_pos" );

        sphere_mesh_buffer = renderer->create_buffer( creation );
    }

    {
        BufferCreation creation{ };
        sizet buffer_size = indices.size * sizeof( u32 );
        creation.set( VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( indices.data ).set_name( "debug_sphere_indices" );

        sphere_mesh_indices = renderer->create_buffer( creation );
    }

    positions.shutdown();
    indices.shutdown();

    mesh_instances.init( resident_allocator, 16 );

    // Copy all mesh draws
    for ( u32 i = 0; i < scene.meshes.size; ++i ) {
        Mesh& mesh = scene.meshes[ i ];

        MeshInstance new_instance{ };
        new_instance.mesh = &mesh;

        mesh_instances.push( new_instance );
    }
}

void DebugPass::free_gpu_resources() {

    renderer->destroy_buffer( sphere_mesh_indices );
    renderer->destroy_buffer( sphere_mesh_buffer );

    mesh_instances.shutdown();
}

//
// DoFPass ////////////////////////////////////////////////////////////////
void DoFPass::add_ui() {
    ImGui::InputFloat( "Focal Length", &focal_length );
    ImGui::InputFloat( "Plane in Focus", &plane_in_focus );
    ImGui::InputFloat( "Aperture", &aperture );
}

void DoFPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph ) {

    FrameGraphResource* texture = ( FrameGraphResource* )frame_graph->get_resource( "lighting" );
    RASSERT( texture != nullptr );

    gpu_commands->copy_texture( texture->resource_info.texture.handle[ current_frame_index ], scene_mips[ current_frame_index ]->handle, RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
}

void DoFPass::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {

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
    for ( u32 i = 0; i < k_max_frames; ++i ) {
        renderer->destroy_texture( scene_mips[ i ] );

        // Reuse cached texture creation and create new scene mips.
        dof_scene_tc.set_flags( mips, 0 ).set_size( new_width, new_height, 1 );
        scene_mips[ i ] = renderer->create_texture( dof_scene_tc );
    }
}

void DoFPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
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

    depth_texture = frame_graph->get_resource( depth_texture_reference->name );
    RASSERT( depth_texture != nullptr );

    FrameGraphResourceInfo& info = color_texture->resource_info;
    u32 w = info.texture.width;
    u32 h = info.texture.height;

    u32 mips = 1;
    while ( w > 1 && h > 1 ) {
        w /= 2;
        h /= 2;
        mips++;
    }

    dof_scene_tc.set_data( nullptr ).set_format_type( info.texture.format, TextureType::Texture2D ).set_flags( mips, 0 ).set_size( ( u16 )info.texture.width, ( u16 )info.texture.height, 1 ).set_name( "scene_mips" );
    for ( u32 i = 0; i < k_max_frames; ++i ) {
        scene_mips[ i ] = renderer->create_texture( dof_scene_tc );
    }
    mesh.pbr_material.material = material_dof;

    znear = 0.1f;
    zfar = 1000.0f;
    focal_length = 5.0f;
    plane_in_focus = 1.0f;
    aperture = 8.0f;
}

void DoFPass::upload_gpu_data() {

    u32 current_frame_index = renderer->gpu->current_frame;

    MapBufferParameters cb_map = { mesh.pbr_material.material_buffer, 0, 0 };
    DoFData* dof_data = ( DoFData* )renderer->gpu->map_buffer( cb_map );
    if ( dof_data ) {
        dof_data->textures[ 0 ] = scene_mips[ current_frame_index ]->handle.index;
        dof_data->textures[ 1 ] = depth_texture->resource_info.texture.handle[ current_frame_index ].index;

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

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        renderer->destroy_texture( scene_mips[ i ] );
    }
    gpu.destroy_buffer( mesh.pbr_material.material_buffer );
    gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set );
}

CommandBuffer* RenderScene::update_physics( f32 delta_time, f32 air_density, f32 spring_stiffness, f32 spring_damping, vec3s wind_direction, bool reset_simulation ) {
    // Based on http://graphics.stanford.edu/courses/cs468-02-winter/Papers/Rigidcloth.pdf

#if 0
    // NOTE(marco): left for reference
    const u32 sim_steps = 10;
    const f32 dt_multiplier = 1.0f / sim_steps;
    delta_time *= dt_multiplier;

    const vec3s g{ 0.0f, -9.8f, 0.0f };

    for ( u32 m = 0; m < meshes.size; ++m ) {
        Mesh& mesh = meshes[ m ];

        PhysicsMesh* physics_mesh = mesh.physics_mesh;

        if ( physics_mesh != nullptr ) {
            const vec3s fixed_vertex_1{ 0.0f,  1.0f, -1.0f };
            const vec3s fixed_vertex_2{ 0.0f,  1.0f,  1.0f };
            const vec3s fixed_vertex_3{ 0.0f, -1.0f,  1.0f };
            const vec3s fixed_vertex_4{ 0.0f, -1.0f, -1.0f };

            if ( reset_simulation ) {
                for ( u32 v = 0; v < physics_mesh->vertices.size; ++v ) {
                    PhysicsVertex& vertex = physics_mesh->vertices[ v ];
                    vertex.position = vertex.start_position;
                    vertex.previous_position = vertex.start_position;
                    vertex.velocity = vec3s{ };
                    vertex.force = vec3s{ };
                }
            }

            for ( u32 s = 0; s < sim_steps; ++s ) {
                // First calculate the force to apply to each vertex
                for ( u32 v = 0; v < physics_mesh->vertices.size; ++v ) {
                    PhysicsVertex& vertex = physics_mesh->vertices[ v ];

                    if ( glms_vec3_eqv( vertex.start_position, fixed_vertex_1 ) || glms_vec3_eqv( vertex.start_position, fixed_vertex_2 ) ||
                        glms_vec3_eqv( vertex.start_position, fixed_vertex_3 ) || glms_vec3_eqv( vertex.start_position, fixed_vertex_4 )) {
                        continue;
                    }

                    f32 m = vertex.mass;

                    vec3s spring_force{ };

                    for ( u32 j = 0; j < vertex.joint_count; ++j ) {
                        PhysicsVertex& other_vertex = physics_mesh->vertices[ vertex.joints[ j ].vertex_index ];

                        f32 spring_rest_length =  glms_vec3_distance( vertex.start_position, other_vertex.start_position );

                        vec3s pull_direction = glms_vec3_sub( vertex.position, other_vertex.position );
                        vec3s relative_pull_direction = glms_vec3_sub( pull_direction, glms_vec3_scale( glms_vec3_normalize( pull_direction ), spring_rest_length ) );
                        pull_direction = glms_vec3_scale( relative_pull_direction, spring_stiffness );
                        spring_force = glms_vec3_add( spring_force, pull_direction );
                    }

                    vec3s viscous_damping = glms_vec3_scale( vertex.velocity, -spring_damping );

                    vec3s viscous_velocity = glms_vec3_sub( wind_direction, vertex.velocity );
                    viscous_velocity = glms_vec3_scale( vertex.normal, glms_vec3_dot( vertex.normal, viscous_velocity ) );
                    viscous_velocity = glms_vec3_scale( viscous_velocity, air_density );

                    vertex.force = glms_vec3_scale( g, m );
                    vertex.force = glms_vec3_sub( vertex.force, spring_force );
                    vertex.force = glms_vec3_add( vertex.force, viscous_damping );
                    vertex.force = glms_vec3_add( vertex.force, viscous_velocity );
                }

                // Then update their position
                for ( u32 v = 0; v < physics_mesh->vertices.size; ++v ) {
                    PhysicsVertex& vertex = physics_mesh->vertices[ v ];

                    vec3s previous_position = vertex.previous_position;
                    vec3s current_position = vertex.position;

                    // Verlet integration
                    vertex.position = glms_vec3_scale( current_position, 2.0f );
                    vertex.position = glms_vec3_sub( vertex.position, previous_position );
                    vertex.position = glms_vec3_add( vertex.position, glms_vec3_scale( vertex.force, delta_time * delta_time ) );

                    vertex.previous_position = current_position;

                    vertex.velocity = glms_vec3_sub( vertex.position, current_position );
                }
            }

            Buffer* position_buffer = renderer->gpu->access_buffer( mesh.position_buffer );
            vec3s* positions = ( vec3s* )( position_buffer->mapped_data + mesh.position_offset );

            Buffer* normal_buffer = renderer->gpu->access_buffer( mesh.normal_buffer );
            vec3s* normals = ( vec3s* )( normal_buffer->mapped_data + mesh.normal_offset );

            Buffer* tangent_buffer = renderer->gpu->access_buffer( mesh.tangent_buffer );
            vec3s* tangents = ( vec3s* )( tangent_buffer->mapped_data + mesh.tangent_offset );

            Buffer* index_buffer = renderer->gpu->access_buffer( mesh.index_buffer );
            u32* indices = ( u32* )( index_buffer->mapped_data + mesh.index_offset );

            for ( u32 v = 0; v < physics_mesh->vertices.size; ++v ) {
                positions[ v ] = physics_mesh->vertices[ v ].position;
            }

            for ( u32 i = 0; i < mesh.primitive_count; i += 3 ) {
                u32 i0 = indices[ i + 0 ];
                u32 i1 = indices[ i + 1 ];
                u32 i2 = indices[ i + 2 ];

                vec3s p0 = physics_mesh->vertices[ i0 ].position;
                vec3s p1 = physics_mesh->vertices[ i1 ].position;
                vec3s p2 = physics_mesh->vertices[ i2 ].position;

                // TODO(marco): better normal compuation, also update tangents
                vec3s edge1 = glms_vec3_sub( p1, p0 );
                vec3s edge2 = glms_vec3_sub( p2, p0 );

                vec3s n = glms_cross( edge1, edge2 );

                physics_mesh->vertices[ i0 ].normal = glms_normalize( glms_vec3_add( normals[ i0 ], n ) );
                physics_mesh->vertices[ i1 ].normal = glms_normalize( glms_vec3_add( normals[ i1 ], n ) );
                physics_mesh->vertices[ i2 ].normal = glms_normalize( glms_vec3_add( normals[ i2 ], n ) );

                normals[ i0 ] = physics_mesh->vertices[ i0 ].normal;
                normals[ i1 ] = physics_mesh->vertices[ i1 ].normal;
                normals[ i2 ] = physics_mesh->vertices[ i2 ].normal;
            }
        }
    }
#else
    if ( physics_cb.index == k_invalid_buffer.index )
        return nullptr;

    GpuDevice& gpu = *renderer->gpu;

    MapBufferParameters physics_cb_map = { physics_cb, 0, 0 };
    PhysicsSceneData* gpu_physics_data = ( PhysicsSceneData* )gpu.map_buffer( physics_cb_map );
    if ( gpu_physics_data ) {
        gpu_physics_data->wind_direction = wind_direction;
        gpu_physics_data->reset_simulation = reset_simulation ? 1 : 0;
        gpu_physics_data->air_density = air_density;
        gpu_physics_data->spring_stiffness = spring_stiffness;
        gpu_physics_data->spring_damping = spring_damping;

        gpu.unmap_buffer( physics_cb_map );
    }

    CommandBuffer* cb = nullptr;

    for ( u32 m = 0; m < meshes.size; ++m ) {
        Mesh& mesh = meshes[ m ];

        PhysicsMesh* physics_mesh = mesh.physics_mesh;

        if ( physics_mesh != nullptr ) {
            if ( !gpu.buffer_ready( mesh.position_buffer ) ||
                 !gpu.buffer_ready( mesh.normal_buffer ) ||
                 !gpu.buffer_ready( mesh.tangent_buffer ) ||
                 !gpu.buffer_ready( mesh.index_buffer ) ||
                 !gpu.buffer_ready( physics_mesh->gpu_buffer ) ||
                 !gpu.buffer_ready( physics_mesh->draw_indirect_buffer ) ) {
                continue;
            }

            if ( cb == nullptr ) {
                cb = gpu.get_command_buffer( 0, gpu.current_frame, true, true /*compute*/ );

                cb->push_marker( "Frame" );
                cb->push_marker( "async" );

                const u64 cloth_hashed_name = hash_calculate( "cloth" );
                GpuTechnique* cloth_technique = renderer->resource_cache.techniques.get( cloth_hashed_name );

                cb->bind_pipeline( cloth_technique->passes[ 0 ].pipeline );
            }

            cb->bind_descriptor_set( &physics_mesh->descriptor_set, 1, nullptr, 0 );

            // TODO(marco): submit all meshes at once
            cb->dispatch( 1, 1, 1 );
        }
    }

    if ( cb != nullptr ) {
        cb->pop_marker();
        cb->pop_marker();

        // Graphics queries not available in compute only queues.

        cb->end();
    }

    return cb;
#endif
}

// TODO: refactor
Transform animated_transforms[ 256 ];

void RenderScene::update_animations( f32 delta_time ) {

    if ( animations.size == 0 ) {
        return;
    }

    // TODO: update the first animation as test
    Animation& animation = animations[ 0 ];
    static f32 current_time = 0.f;

    current_time += delta_time;
    if ( current_time > animation.time_end ) {
        current_time -= animation.time_end;
    }

    // TODO: fix skeleton/scene graph relationship
    for ( u32 i = 0; i < 256; ++i ) {

        Transform& transform = animated_transforms[ i ];
        transform.reset();
    }
    // Accumulate transformations

    u8 changed[ 256 ];
    memset( changed, 0, 256 );

    // For each animation channel
    for ( u32 ac = 0; ac < animation.channels.size; ++ac ) {
        AnimationChannel& channel = animation.channels[ ac ];
        AnimationSampler& sampler = animation.samplers[ channel.sampler ];

        if ( sampler.interpolation_type != AnimationSampler::Linear ) {
            rprint( "Interpolation %s still not supported.\n", sampler.interpolation_type );
            continue;
        }

        // Scroll through all key frames
        for ( u32 ki = 0; ki < sampler.key_frames.size - 1; ++ki ) {
            const f32 keyframe = sampler.key_frames[ ki ];
            const f32 next_keyframe = sampler.key_frames[ ki + 1 ];
            if ( current_time >= keyframe && current_time <= next_keyframe ) {

                const f32 interpolation = ( current_time - keyframe ) / ( next_keyframe - keyframe );

                RASSERT( channel.target_node < 256 );
                changed[ channel.target_node ] = 1;
                Transform& transform = animated_transforms[ channel.target_node ];
                switch ( channel.target_type ) {
                    case AnimationChannel::TargetType::Translation:
                    {
                        const vec3s current_data{ sampler.data[ ki ].x, sampler.data[ ki ].y, sampler.data[ ki ].z };
                        const vec3s next_data{ sampler.data[ ki + 1 ].x, sampler.data[ ki + 1 ].y, sampler.data[ ki + 1 ].z };
                        transform.translation = glms_vec3_lerp( current_data, next_data, interpolation );

                        break;
                    }
                    case AnimationChannel::TargetType::Rotation:
                    {
                        const vec4s current_data = sampler.data[ ki ];
                        const versors current_rotation = glms_quat_init( current_data.x, current_data.y, current_data.z, current_data.w );

                        const vec4s next_data = sampler.data[ ki + 1 ];
                        const versors next_rotation = glms_quat_init( next_data.x, next_data.y, next_data.z, next_data.w );

                        transform.rotation = glms_quat_normalize( glms_quat_slerp( current_rotation, next_rotation, interpolation ) );

                        break;
                    }
                    case AnimationChannel::TargetType::Scale:
                    {
                        const vec3s current_data{ sampler.data[ ki ].x, sampler.data[ ki ].y, sampler.data[ ki ].z };
                        const vec3s next_data{ sampler.data[ ki + 1 ].x, sampler.data[ ki + 1 ].y, sampler.data[ ki + 1 ].z };
                        transform.scale = glms_vec3_lerp( current_data, next_data, interpolation );

                        break;
                    }
                    default:
                        break;
                }

                break;
            }
        }
    }
}

// TODO: remove, improve
mat4s get_local_matrix( SceneGraph* scene_graph, u32 node_index ) {
    const mat4s& a = animated_transforms[ node_index ].calculate_matrix();
    // NOTE(marco): according to the spec (3.7.3.2)
    // Only the joint transforms are applied to the skinned mesh; the transform of the skinned mesh node MUST be ignored
    return a;
}

mat4s get_node_transform( SceneGraph* scene_graph, u32 node_index ) {
    mat4s node_transform = get_local_matrix( scene_graph, node_index );

    i32 parent = scene_graph->nodes_hierarchy[ node_index ].parent;
    while ( parent >= 0 ) {
        node_transform = glms_mat4_mul( get_local_matrix( scene_graph, parent ), node_transform );

        parent = scene_graph->nodes_hierarchy[ parent ].parent;
    }

    return node_transform;
}

void RenderScene::update_joints() {

    for ( u32 i = 0; i < skins.size; i++ ) {
        Skin& skin = skins[ i ];

        // Calculate joint transforms and upload to GPU
        MapBufferParameters cb_map { skin.joint_transforms, 0, 0 };
        mat4s* joint_transforms = (mat4s*)renderer->gpu->map_buffer( cb_map );

        if ( joint_transforms ) {
            for ( u32 ji = 0; ji < skin.joints.size; ji++ ) {
                u32 joint = skin.joints[ ji ];

                mat4s& joint_transform = joint_transforms[ ji ];

                joint_transform = glms_mat4_mul( get_node_transform( scene_graph, joint ), skin.inverse_bind_matrices[ ji ] );
            }

            renderer->gpu->unmap_buffer( cb_map );
        }
    }
}

// RenderScene ////////////////////////////////////////////////////////////
void RenderScene::upload_gpu_data() {

    //u32 current_frame_index = renderer->gpu->absolute_frame;

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
}

void RenderScene::draw_mesh( CommandBuffer* gpu_commands, Mesh& mesh ) {

    BufferHandle buffers[]{ mesh.position_buffer, mesh.tangent_buffer, mesh.normal_buffer, mesh.texcoord_buffer, mesh.joints_buffer, mesh.weights_buffer };
    u32 offsets[]{ mesh.position_offset, mesh.tangent_offset, mesh.normal_offset, mesh.texcoord_offset, mesh.joints_offset, mesh.weights_offset };
    gpu_commands->bind_vertex_buffers( buffers, 0, mesh.skin_index != i32_max ? 6 : 4, offsets );

    gpu_commands->bind_index_buffer( mesh.index_buffer, mesh.index_offset, mesh.index_type );

    if ( recreate_per_thread_descriptors ) {
        DescriptorSetCreation ds_creation{};
        ds_creation.buffer( scene_cb, 0 ).buffer( mesh.pbr_material.material_buffer, 1 );
        DescriptorSetHandle descriptor_set = renderer->create_descriptor_set( gpu_commands, mesh.pbr_material.material, ds_creation );

        gpu_commands->bind_local_descriptor_set( &descriptor_set, 1, nullptr, 0 );
    } else {
        gpu_commands->bind_descriptor_set( &mesh.pbr_material.descriptor_set, 1, nullptr, 0 );
    }

    gpu_commands->draw_indexed( TopologyType::Triangle, mesh.primitive_count, 1, 0, 0, 0 );
}

// DrawTask ///////////////////////////////////////////////////////////////
void DrawTask::init( GpuDevice* gpu_, FrameGraph* frame_graph_, Renderer* renderer_,
                     ImGuiService* imgui_, GpuVisualProfiler* gpu_profiler_, RenderScene* scene_,
                     FrameRenderer* frame_renderer_ ) {
    gpu = gpu_;
    frame_graph = frame_graph_;
    renderer = renderer_;
    imgui = imgui_;
    gpu_profiler = gpu_profiler_;
    scene = scene_;
    frame_renderer = frame_renderer_;

    current_frame_index = gpu->current_frame;
    current_framebuffer = gpu->get_current_framebuffer();
}

void DrawTask::ExecuteRange( enki::TaskSetPartition range_, uint32_t threadnum_ ) {
    ZoneScoped;

        using namespace raptor;

    thread_id = threadnum_;

    //rprint( "Executing draw task from thread %u\n", threadnum_ );
    // TODO: improve getting a command buffer/pool
    CommandBuffer* gpu_commands = gpu->get_command_buffer( threadnum_, current_frame_index, true );
    gpu_commands->push_marker( "Frame" );

    frame_graph->render( current_frame_index, gpu_commands, scene );

    gpu_commands->push_marker( "Fullscreen" );
    gpu_commands->clear( 0.3f, 0.3f, 0.3f, 1.f, 0 );
    gpu_commands->clear_depth_stencil( 1.0f, 0 );
    gpu_commands->bind_pass( gpu->get_swapchain_pass(), current_framebuffer, false );
    gpu_commands->set_scissor( nullptr );
    gpu_commands->set_viewport( nullptr );

    // Apply fullscreen material
    FrameGraphResource* texture = frame_graph->get_resource( "final" );
    RASSERT( texture != nullptr );

    gpu_commands->bind_pipeline( frame_renderer->fullscreen_tech->passes[ 0 ].pipeline );
    gpu_commands->bind_descriptor_set( &frame_renderer->fullscreen_ds, 1, nullptr, 0 );
    gpu_commands->draw( TopologyType::Triangle, 0, 3, texture->resource_info.texture.handle[ current_frame_index ].index, 1 );

    imgui->render( *gpu_commands, false );

    gpu_commands->pop_marker(); // Fullscreen marker
    gpu_commands->pop_marker(); // Frame marker

    gpu_profiler->update( *gpu );

    // Send commands to GPU
    gpu->queue_command_buffer( gpu_commands );
}

// FrameRenderer //////////////////////////////////////////////////////////
void FrameRenderer::init( Allocator* resident_allocator_, Renderer* renderer_,
                          FrameGraph* frame_graph_, SceneGraph* scene_graph_,
                          RenderScene* scene_ ) {
    resident_allocator = resident_allocator_;
    renderer = renderer_;
    frame_graph = frame_graph_;
    scene_graph = scene_graph_;
    scene = scene_;

    frame_graph->builder->register_render_pass( "depth_pre_pass", &depth_pre_pass );
    frame_graph->builder->register_render_pass( "gbuffer_pass", &gbuffer_pass );
    frame_graph->builder->register_render_pass( "lighting_pass", &light_pass );
    frame_graph->builder->register_render_pass( "transparent_pass", &transparent_pass );
    frame_graph->builder->register_render_pass( "depth_of_field_pass", &dof_pass );
    frame_graph->builder->register_render_pass( "debug_pass", &debug_pass );
}

void FrameRenderer::shutdown() {
    depth_pre_pass.free_gpu_resources();
    gbuffer_pass.free_gpu_resources();
    light_pass.free_gpu_resources();
    transparent_pass.free_gpu_resources();
    // TODO(marco): check that node is enabled before calling
    // dof_pass.free_gpu_resources();
    debug_pass.free_gpu_resources();

    renderer->gpu->destroy_descriptor_set( fullscreen_ds );
}

void FrameRenderer::upload_gpu_data() {
    light_pass.upload_gpu_data();
    // dof_pass.upload_gpu_data();

    scene->upload_gpu_data();
}

void FrameRenderer::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {
}

void FrameRenderer::prepare_draws( StackAllocator* scratch_allocator ) {

    scene->prepare_draws( renderer, scratch_allocator, scene_graph );

    depth_pre_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    gbuffer_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    light_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    transparent_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    // dof_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    debug_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );

    // Handle fullscreen pass.
    fullscreen_tech = renderer->resource_cache.techniques.get( hash_calculate( "fullscreen" ) );

    DescriptorSetCreation dsc;
    DescriptorSetLayoutHandle descriptor_set_layout = renderer->gpu->get_descriptor_set_layout( fullscreen_tech->passes[ 0 ].pipeline, k_material_descriptor_set_index );
    dsc.reset().buffer( scene->scene_cb, 0 ).set_layout( descriptor_set_layout );
    fullscreen_ds = renderer->gpu->create_descriptor_set( dsc );
}

// Transform ////////////////////////////////////////////////////

void Transform::reset() {
    translation = { 0.f, 0.f, 0.f };
    scale = { 1.f, 1.f, 1.f };
    rotation = glms_quat_identity();
}

mat4s Transform::calculate_matrix() const {

    const mat4s translation_matrix = glms_translate_make( translation );
    const mat4s scale_matrix = glms_scale_make( scale );
    const mat4s local_matrix = glms_mat4_mul( glms_mat4_mul( translation_matrix, glms_quat_mat4( rotation ) ), scale_matrix );
    return local_matrix;
}

} // namespace raptor
