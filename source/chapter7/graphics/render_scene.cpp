#include "graphics/render_scene.hpp"
#include "graphics/renderer.hpp"
#include "graphics/scene_graph.hpp"
#include "graphics/asynchronous_loader.hpp"
#include "graphics/raptor_imgui.hpp"
#include "graphics/gpu_profiler.hpp"

#include "foundation/time.hpp"
#include "foundation/numerics.hpp"

#include "application/game_camera.hpp"

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

#include "external/cglm/struct/vec2.h"
#include "external/cglm/struct/mat2.h"
#include "external/cglm/struct/mat3.h"
#include "external/cglm/struct/mat4.h"
#include "external/cglm/struct/cam.h"

#define DEBUG_DRAW_MESHLET_SPHERES 0
#define DEBUG_DRAW_MESHLET_CONES 0
#define DEBUG_DRAW_POINT_LIGHT_SPHERES 0

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
static void copy_gpu_material_data( GpuMaterialData& gpu_mesh_data, const Mesh& mesh ) {
    gpu_mesh_data.textures[ 0 ] = mesh.pbr_material.diffuse_texture_index;
    gpu_mesh_data.textures[ 1 ] = mesh.pbr_material.roughness_texture_index;
    gpu_mesh_data.textures[ 2 ] = mesh.pbr_material.normal_texture_index;
    gpu_mesh_data.textures[ 3 ] = mesh.pbr_material.occlusion_texture_index;

    gpu_mesh_data.emissive = { mesh.pbr_material.emissive_factor.x, mesh.pbr_material.emissive_factor.y, mesh.pbr_material.emissive_factor.z, ( float )mesh.pbr_material.emissive_texture_index };

    gpu_mesh_data.base_color_factor = mesh.pbr_material.base_color_factor;
    gpu_mesh_data.metallic_roughness_occlusion_factor.x = mesh.pbr_material.metallic;
    gpu_mesh_data.metallic_roughness_occlusion_factor.y = mesh.pbr_material.roughness;
    gpu_mesh_data.metallic_roughness_occlusion_factor.z = mesh.pbr_material.occlusion;
    gpu_mesh_data.alpha_cutoff = mesh.pbr_material.alpha_cutoff;

    gpu_mesh_data.flags = mesh.pbr_material.flags;

    gpu_mesh_data.mesh_index = mesh.gpu_mesh_index;
    gpu_mesh_data.meshlet_offset = mesh.meshlet_offset;
    gpu_mesh_data.meshlet_count = mesh.meshlet_count;
    gpu_mesh_data.meshlet_index_count = mesh.meshlet_index_count;
}

//
//
static void copy_gpu_mesh_transform( GpuMeshInstanceData& gpu_mesh_data, const MeshInstance& mesh_instance, const f32 global_scale, const SceneGraph* scene_graph ) {
    if ( scene_graph ) {
        // Apply global scale matrix
        // NOTE: for left-handed systems (as defined in cglm) need to invert positive and negative Z.
        const mat4s scale_matrix = glms_scale_make( { global_scale, global_scale, -global_scale } );
        gpu_mesh_data.world = glms_mat4_mul( scale_matrix, scene_graph->world_matrices[ mesh_instance.scene_graph_node_index ] );

        gpu_mesh_data.inverse_world = glms_mat4_inv( glms_mat4_transpose( gpu_mesh_data.world ) );
    } else {
        gpu_mesh_data.world = glms_mat4_identity();
        gpu_mesh_data.inverse_world = glms_mat4_identity();
    }

    gpu_mesh_data.mesh_index = mesh_instance.mesh->gpu_mesh_index;
}

static FrameGraphResource* get_output_texture( FrameGraph* frame_graph, FrameGraphResourceHandle input ) {
    FrameGraphResource* input_resource = frame_graph->access_resource( input );

    FrameGraphResource* output_resource = frame_graph->access_resource( input_resource->output_handle );
    RASSERT( output_resource != nullptr );

    return output_resource;
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
void DepthPrePass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    if ( render_scene->use_meshlets ) {
        Renderer* renderer = render_scene->renderer;

        // Draw meshlets
        const u64 meshlet_hashed_name = hash_calculate( "meshlet" );
        GpuTechnique* meshlet_technique = renderer->resource_cache.techniques.get( meshlet_hashed_name );

        PipelineHandle pipeline = meshlet_technique->passes[ meshlet_technique_index ].pipeline;

        gpu_commands->bind_pipeline( pipeline );

        gpu_commands->bind_descriptor_set( &render_scene->mesh_shader_early_descriptor_set[ current_frame_index ], 1, nullptr, 0);

        gpu_commands->draw_mesh_task_indirect( render_scene->mesh_task_indirect_early_commands_sb[ current_frame_index ], offsetof( GpuMeshDrawCommand, indirectMS ), render_scene->mesh_task_indirect_early_commands_sb[ current_frame_index ], 0, render_scene->mesh_instances.size, sizeof( GpuMeshDrawCommand ) );
    }
    else {
        Material* last_material = nullptr;
        for ( u32 mesh_index = 0; mesh_index < mesh_instance_draws.size; ++mesh_index ) {
            MeshInstanceDraw& mesh_instance_draw = mesh_instance_draws[ mesh_index ];
            Mesh& mesh = *mesh_instance_draw.mesh_instance->mesh;

            if ( mesh.pbr_material.material != last_material ) {
                PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, mesh_instance_draw.material_pass_index );

                gpu_commands->bind_pipeline( pipeline );

                last_material = mesh.pbr_material.material;
            }

            render_scene->draw_mesh_instance( gpu_commands, *mesh_instance_draw.mesh_instance );
        }
    }
}

void DepthPrePass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "depth_pre_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled )
        return;

    // Create pipeline state
    PipelineCreation pipeline_creation;

    const u64 hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    mesh_instance_draws.init( resident_allocator, 16 );

    // Copy all mesh draws and change only material.
    for ( u32 i = 0; i < scene.mesh_instances.size; ++i ) {

        MeshInstance& mesh_instance = scene.mesh_instances[ i ];
        Mesh* mesh = mesh_instance.mesh;
        if ( mesh->is_transparent() ) {
            continue;
        }

        MeshInstanceDraw mesh_instance_draw{};
        mesh_instance_draw.mesh_instance = &mesh_instance;
        mesh_instance_draw.material_pass_index = mesh->has_skinning() ? main_technique->get_pass_index( "depth_pre_skinning" ) : main_technique->get_pass_index(  "depth_pre" );

        mesh_instance_draws.push( mesh_instance_draw );
    }

    GpuDevice& gpu = *renderer->gpu;

    // Cache meshlet technique index
    if ( gpu.mesh_shaders_extension_present ) {
        GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hash_calculate( "meshlet" ) );
        meshlet_technique_index = main_technique->get_pass_index( "depth_pre" );
    }
}

void DepthPrePass::free_gpu_resources() {
    if ( !enabled )
        return;

    mesh_instance_draws.shutdown();
}

//
// DepthPrePass ///////////////////////////////////////////////////////
void DepthPyramidPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    update_depth_pyramid = ( render_scene->scene_data.freeze_occlusion_camera == 0 );
}

void DepthPyramidPass::post_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    GpuDevice* gpu = renderer->gpu;

    Texture* depth_pyramid_texture = gpu->access_texture( depth_pyramid );

    if ( update_depth_pyramid ) {
        gpu_commands->bind_pipeline( depth_pyramid_pipeline );

        u32 width = depth_pyramid_texture->width;
        u32 height = depth_pyramid_texture->height;

        FrameGraphResource* depth_resource = ( FrameGraphResource* )frame_graph->get_resource( "depth" );
        TextureHandle depth_handle = depth_resource->resource_info.texture.handle;
        Texture* depth_texture = gpu->access_texture( depth_handle );

        util_add_image_barrier( gpu, gpu_commands->vk_command_buffer, depth_texture, RESOURCE_STATE_SHADER_RESOURCE, 0, 1, true );

        for ( u32 mip_index = 0; mip_index < depth_pyramid_texture->mip_level_count; ++mip_index ) {
            util_add_image_barrier( gpu, gpu_commands->vk_command_buffer, depth_pyramid_texture->vk_image, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_UNORDERED_ACCESS, mip_index, 1, false );

            gpu_commands->bind_descriptor_set( &depth_hierarchy_descriptor_set[ mip_index ], 1, nullptr, 0 );

            // NOTE(marco): local workgroup is 8 x 8
            u32 group_x = ( width + 7 ) / 8;
            u32 group_y = ( height + 7 ) / 8;

            gpu_commands->dispatch( group_x, group_y, 1 );

            util_add_image_barrier( gpu, gpu_commands->vk_command_buffer, depth_pyramid_texture->vk_image, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_SHADER_RESOURCE, mip_index, 1, false );

            width /= 2;
            height /= 2;
        }
    }
}

void DepthPyramidPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {

    // Destroy old resources
    gpu.destroy_texture( depth_pyramid );
    // Use old depth pyramid levels value
    for ( u32 i = 0; i < depth_pyramid_levels; ++i ) {
        gpu.destroy_descriptor_set( depth_hierarchy_descriptor_set[ i ] );
        gpu.destroy_texture( depth_pyramid_views[ i ] );
    }

    FrameGraphResource* depth_resource = ( FrameGraphResource* )frame_graph->get_resource( "depth" );
    TextureHandle depth_handle = depth_resource->resource_info.texture.handle;
    Texture* depth_texture = gpu.access_texture( depth_handle );

    create_depth_pyramid_resource( depth_texture );
}

void DepthPyramidPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "depth_pyramid_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    FrameGraphResource* depth_resource = ( FrameGraphResource* )frame_graph->get_resource( "depth" );
    TextureHandle depth_handle = depth_resource->resource_info.texture.handle;
    Texture* depth_texture = gpu.access_texture( depth_handle );

    // Sampler does not need to be recreated
    SamplerCreation sc;
    sc.set_address_mode_uvw( VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE )
        .set_min_mag_mip( VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST ).set_reduction_mode( VK_SAMPLER_REDUCTION_MODE_MAX ).set_name( "depth_pyramid_sampler" );
    depth_pyramid_sampler = gpu.create_sampler( sc );

    create_depth_pyramid_resource( depth_texture );

    gpu.link_texture_sampler( depth_pyramid, depth_pyramid_sampler );
}

void DepthPyramidPass::free_gpu_resources() {
    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    gpu.destroy_sampler( depth_pyramid_sampler );
    gpu.destroy_texture( depth_pyramid );

    for ( u32 i = 0; i < depth_pyramid_levels; ++i ) {
        gpu.destroy_texture(depth_pyramid_views[ i ] );
        gpu.destroy_descriptor_set( depth_hierarchy_descriptor_set[ i ] );
    }
}

void DepthPyramidPass::create_depth_pyramid_resource( Texture* depth_texture ) {

    // TODO(marco): this assumes a pot depth resolution
    u32 width = depth_texture->width / 2;
    u32 height = depth_texture->height / 2;

    GpuDevice& gpu = *renderer->gpu;

    depth_pyramid_levels = 0;
    while ( width >= 2 && height >= 2 ) {
        depth_pyramid_levels++;

        width /= 2;
        height /= 2;
    }

    TextureCreation depth_hierarchy_creation{ };
    depth_hierarchy_creation.set_format_type( VK_FORMAT_R32_SFLOAT, TextureType::Enum::Texture2D ).set_flags( TextureFlags::Compute_mask ).set_size( depth_texture->width / 2, depth_texture->height / 2, 1 ).set_name( "depth_hierarchy" ).set_mips( depth_pyramid_levels );

    depth_pyramid = gpu.create_texture( depth_hierarchy_creation );

    TextureViewCreation depth_pyramid_view_creation;
    depth_pyramid_view_creation.parent_texture = depth_pyramid;
    depth_pyramid_view_creation.array_base_layer = 0;
    depth_pyramid_view_creation.array_layer_count = 1;
    depth_pyramid_view_creation.mip_level_count = 1;
    depth_pyramid_view_creation.name = "depth_pyramid_view";

    DescriptorSetCreation descriptor_set_creation{ };

    GpuTechnique* culling_technique = renderer->resource_cache.techniques.get( hash_calculate( "culling" ) );
    depth_pyramid_pipeline = culling_technique->passes[ 1 ].pipeline;
    DescriptorSetLayoutHandle depth_pyramid_layout = gpu.get_descriptor_set_layout( depth_pyramid_pipeline, k_material_descriptor_set_index );

    for ( u32 i = 0; i < depth_pyramid_levels; ++i ) {
        depth_pyramid_view_creation.mip_base_level = i;

        depth_pyramid_views[ i ] = gpu.create_texture_view( depth_pyramid_view_creation );

        if ( i == 0 ) {
            descriptor_set_creation.reset().texture( depth_texture->handle, 0 ).texture( depth_pyramid_views[ i ], 1 ).set_layout( depth_pyramid_layout );
        } else {
            descriptor_set_creation.reset().texture( depth_pyramid_views[ i - 1 ], 0 ).texture( depth_pyramid_views[ i ], 1 ).set_layout( depth_pyramid_layout );
        }

        depth_hierarchy_descriptor_set[ i ] = gpu.create_descriptor_set( descriptor_set_creation );
    }
}

//
// GBufferPass ////////////////////////////////////////////////////////
void GBufferPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {

    GpuDevice* gpu = renderer->gpu;

    if ( render_scene->use_meshlets_emulation ) {

        // TODO: remove
        gpu_commands->global_debug_barrier();

        // Generate meshlet list
        gpu_commands->bind_pipeline( generate_meshlets_instances_pipeline );
        gpu_commands->bind_descriptor_set( &generate_meshlets_instances_descriptor_set[ current_frame_index ], 1, nullptr, 0 );
        gpu_commands->dispatch( (render_scene->mesh_instances.size + 31) / 32, 1, 1 );

        // TODO: remove
        gpu_commands->global_debug_barrier();

        // Cull visible meshlets
        gpu_commands->bind_pipeline( meshlet_instance_culling_pipeline );
        gpu_commands->bind_descriptor_set( &meshlet_instance_culling_descriptor_set[ current_frame_index ], 1, nullptr, 0 );
        gpu_commands->dispatch_indirect( render_scene->meshlet_instances_indirect_count_sb[ current_frame_index ], 0 );

        // TODO: remove
        gpu_commands->global_debug_barrier();

        // Write counts
        gpu_commands->bind_pipeline( meshlet_write_counts_pipeline );
        gpu_commands->bind_descriptor_set( &meshlet_instance_culling_descriptor_set[ current_frame_index ], 1, nullptr, 0 );
        gpu_commands->dispatch( 1, 1, 1 );

        // TODO: remove
        gpu_commands->global_debug_barrier();

        // Generate index buffer
        BufferHandle meshlet_index_buffer = render_scene->meshlets_index_buffer_sb[ current_frame_index ];

        gpu_commands->buffer_barrier( meshlet_index_buffer, RESOURCE_STATE_INDEX_BUFFER, RESOURCE_STATE_UNORDERED_ACCESS, QueueType::Graphics, QueueType::Compute );

        gpu_commands->bind_pipeline( generate_meshlet_index_buffer_pipeline );
        gpu_commands->bind_descriptor_set( &generate_meshlet_index_buffer_descriptor_set[ current_frame_index ], 1, nullptr, 0 );
        gpu_commands->dispatch_indirect( generate_meshlet_dispatch_indirect_buffer[ current_frame_index ], offsetof( GpuMeshDrawCounts, dispatch_task_x ) );

        gpu_commands->buffer_barrier( meshlet_index_buffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_INDEX_BUFFER, QueueType::Compute, QueueType::Graphics );

        gpu_commands->global_debug_barrier();
    }
}

void GBufferPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    Renderer* renderer = render_scene->renderer;
    if ( render_scene->use_meshlets_emulation ) {

        gpu_commands->bind_pipeline( meshlet_emulation_draw_pipeline );

        gpu_commands->bind_descriptor_set( &render_scene->meshlet_emulation_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

        gpu_commands->bind_index_buffer( render_scene->meshlets_index_buffer_sb[ current_frame_index ], 0, VK_INDEX_TYPE_UINT32 );
        gpu_commands->draw_indexed_indirect( render_scene->mesh_task_indirect_early_commands_sb[ current_frame_index ], 1, offsetof( GpuMeshDrawCommand, indirect ), sizeof( GpuMeshDrawCommand ) );
    }
    else if ( render_scene->use_meshlets ) {

        gpu_commands->bind_pipeline( meshlet_draw_pipeline );

        gpu_commands->bind_descriptor_set( &render_scene->mesh_shader_early_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

        gpu_commands->draw_mesh_task_indirect( render_scene->mesh_task_indirect_early_commands_sb[ current_frame_index ], offsetof( GpuMeshDrawCommand, indirectMS ), render_scene->mesh_task_indirect_count_early_sb[ current_frame_index ], 0, render_scene->mesh_instances.size, sizeof( GpuMeshDrawCommand ) );
    }
    else {
        Material* last_material = nullptr;
        for ( u32 mesh_index = 0; mesh_index < mesh_instance_draws.size; ++mesh_index ) {
            MeshInstanceDraw& mesh_instance_draw = mesh_instance_draws[ mesh_index ];
            Mesh& mesh = *mesh_instance_draw.mesh_instance->mesh;

            if ( mesh.pbr_material.material != last_material ) {
                PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, mesh_instance_draw.material_pass_index );

                gpu_commands->bind_pipeline( pipeline );

                last_material = mesh.pbr_material.material;
            }

            render_scene->draw_mesh_instance( gpu_commands, *mesh_instance_draw.mesh_instance );
        }
    }
}

void GBufferPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "gbuffer_pass_early" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled )
        return;

    const u64 hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    mesh_instance_draws.init( resident_allocator, 16 );

    // Copy all mesh draws and change only material.
    for ( u32 i = 0; i < scene.mesh_instances.size; ++i ) {

        MeshInstance& mesh_instance = scene.mesh_instances[ i ];
        Mesh* mesh = mesh_instance.mesh;
        if ( mesh->is_transparent() ) {
            continue;
        }

        MeshInstanceDraw mesh_instance_draw{};
        mesh_instance_draw.mesh_instance = &mesh_instance;
        mesh_instance_draw.material_pass_index = mesh->has_skinning() ? main_technique->get_pass_index( "gbuffer_skinning" ) : main_technique->get_pass_index( "gbuffer_cull" );

        mesh_instance_draws.push( mesh_instance_draw );
    }

    // Cache meshlet technique index
    GpuTechnique* meshlet_technique = renderer->resource_cache.techniques.get( hash_calculate( "meshlet" ) );

    u32 technique_index = meshlet_technique->get_pass_index( "gbuffer_culling" );
    if ( technique_index != u16_max ) {
        meshlet_draw_pipeline = meshlet_technique->passes[ technique_index ].pipeline;
    }

    technique_index = meshlet_technique->get_pass_index( "emulation_gbuffer_culling" );
    meshlet_emulation_draw_pipeline = meshlet_technique->passes[ technique_index ].pipeline;

    technique_index = meshlet_technique->get_pass_index( "generate_meshlet_index_buffer" );
    generate_meshlet_index_buffer_pipeline = meshlet_technique->passes[ technique_index ].pipeline;

    technique_index = meshlet_technique->get_pass_index( "generate_meshlet_instances" );
    generate_meshlets_instances_pipeline = meshlet_technique->passes[ technique_index ].pipeline;

    technique_index = meshlet_technique->get_pass_index( "meshlet_instance_culling" );
    meshlet_instance_culling_pipeline = meshlet_technique->passes[ technique_index ].pipeline;

    technique_index = meshlet_technique->get_pass_index( "meshlet_write_counts" );
    meshlet_write_counts_pipeline = meshlet_technique->passes[ technique_index ].pipeline;

    DescriptorSetLayoutHandle layout_generate_ib = renderer->gpu->get_descriptor_set_layout( generate_meshlet_index_buffer_pipeline, k_material_descriptor_set_index );
    DescriptorSetLayoutHandle layout_generate_instances = renderer->gpu->get_descriptor_set_layout( generate_meshlets_instances_pipeline, k_material_descriptor_set_index );
    DescriptorSetLayoutHandle layout_instance_culling = renderer->gpu->get_descriptor_set_layout( meshlet_instance_culling_pipeline, k_material_descriptor_set_index );

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        DescriptorSetCreation ds_creation{};
        ds_creation.set_layout( layout_generate_ib ).buffer( scene.meshlets_sb, 1 ).buffer( scene.meshlets_data_sb, 3 )
            .buffer( scene.mesh_task_indirect_early_commands_sb[ i ], 6 ).buffer( scene.mesh_task_indirect_count_early_sb[ i ], 7 )
            .buffer( scene.meshlets_index_buffer_sb[ i ], 8 ).buffer( scene.meshlets_instances_sb[ i ], 9 ).buffer( scene.meshes_sb, 2 )
            .buffer( scene.mesh_instances_sb, 10 ).buffer( scene.meshlets_visible_instances_sb[ i ], 19 );
        generate_meshlet_index_buffer_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );

        ds_creation.reset().set_layout( layout_generate_instances ).buffer( scene.meshlets_sb, 1 ).buffer( scene.meshlets_data_sb, 3 )
            .buffer( scene.mesh_task_indirect_early_commands_sb[ i ], 6 ).buffer( scene.mesh_task_indirect_count_early_sb[ i ], 7 )
            .buffer( scene.meshlets_index_buffer_sb[ i ], 8 ).buffer( scene.meshlets_instances_sb[ i ], 9 ).buffer( scene.meshes_sb, 2 )
            .buffer( scene.mesh_instances_sb, 10 ).buffer( scene.meshlet_instances_indirect_count_sb[ i ], 17 );
        generate_meshlets_instances_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );

        BufferCreation buffer_creation;
        buffer_creation.reset().set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( u32 ) * 4 ).set_name( "meshlet_instance_culling_indirect_buffer" );
        meshlet_instance_culling_indirect_buffer[ i ] = renderer->gpu->create_buffer( buffer_creation );

        ds_creation.reset().set_layout( layout_instance_culling ).buffer( scene.meshlets_sb, 1 )
            .buffer( scene.meshlets_instances_sb[ i ], 9 ).buffer( scene.meshes_sb, 2 ).buffer( scene.scene_cb, 0 )
            .buffer( scene.mesh_instances_sb, 10 ).buffer( scene.meshlets_visible_instances_sb[ i ], 19 )
            .buffer( scene.mesh_bounds_sb, 12 ).buffer( scene.mesh_task_indirect_count_early_sb[ i ], 7 )
            .buffer( scene.mesh_task_indirect_early_commands_sb[ i ], 6 ).buffer( meshlet_instance_culling_indirect_buffer[ i ], 17 );
        meshlet_instance_culling_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );

        // Cache indirect buffer
        generate_meshlet_dispatch_indirect_buffer[ i ] = scene.mesh_task_indirect_count_early_sb[ i ];
    }
}

void GBufferPass::free_gpu_resources() {
    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    mesh_instance_draws.shutdown();

    for ( u32 i = 0; i < k_max_frames; ++i ) {

        gpu.destroy_buffer( meshlet_instance_culling_indirect_buffer[ i ] );

        gpu.destroy_descriptor_set( generate_meshlet_index_buffer_descriptor_set[ i ] );
        gpu.destroy_descriptor_set( generate_meshlets_instances_descriptor_set[ i ] );
        gpu.destroy_descriptor_set( meshlet_instance_culling_descriptor_set[ i ] );
    }
}

// LateGBufferPass /////////////////////////////////////////////////////////
void LateGBufferPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "gbuffer_pass_late" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled )
        return;

    const u64 hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    mesh_instance_draws.init( resident_allocator, 16 );

    // Copy all mesh draws and change only material.
    for ( u32 i = 0; i < scene.mesh_instances.size; ++i ) {

        MeshInstance& mesh_instance = scene.mesh_instances[ i ];
        Mesh* mesh = mesh_instance.mesh;
        if ( mesh->is_transparent() ) {
            continue;
        }

        MeshInstanceDraw mesh_instance_draw{};
        mesh_instance_draw.mesh_instance = &mesh_instance;
        mesh_instance_draw.material_pass_index = mesh->has_skinning() ? main_technique->get_pass_index( "gbuffer_skinning" ) : main_technique->get_pass_index( "gbuffer_cull" );

        mesh_instance_draws.push( mesh_instance_draw );
    }

    // Cache meshlet technique index
    if ( renderer->gpu->mesh_shaders_extension_present ) {
        GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hash_calculate( "meshlet" ) );
        meshlet_technique_index = main_technique->get_pass_index( "gbuffer_culling" );
    }
}

void LateGBufferPass::free_gpu_resources() {
    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    mesh_instance_draws.shutdown();
}

void LateGBufferPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {

    if ( !enabled )
        return;

    if ( render_scene->use_meshlets ) {

        const u64 meshlet_hashed_name = hash_calculate( "meshlet" );
        GpuTechnique* meshlet_technique = renderer->resource_cache.techniques.get( meshlet_hashed_name );

        PipelineHandle pipeline = meshlet_technique->passes[ meshlet_technique_index ].pipeline;

        gpu_commands->bind_pipeline( pipeline );

        gpu_commands->bind_descriptor_set( &render_scene->mesh_shader_late_descriptor_set[ current_frame_index ], 1, nullptr, 0);

        gpu_commands->draw_mesh_task_indirect( render_scene->mesh_task_indirect_late_commands_sb[ current_frame_index ], offsetof(GpuMeshDrawCommand, indirectMS), render_scene->mesh_task_indirect_count_late_sb[ current_frame_index ], 0, render_scene->mesh_instances.size, sizeof(GpuMeshDrawCommand) );
    }
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

void LightPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    if ( use_compute ) {
        PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, 1 );
        gpu_commands->bind_pipeline( pipeline );
        gpu_commands->bind_descriptor_set( &lighting_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

        gpu_commands->dispatch( ceilu32( renderer->gpu->swapchain_width * 1.f / 8 ), ceilu32( renderer->gpu->swapchain_height * 1.f / 8 ), 1 );
    } else {
        PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, 0 );

        gpu_commands->bind_pipeline( pipeline );
        gpu_commands->bind_vertex_buffer( mesh.position_buffer, 0, 0 );
        gpu_commands->bind_descriptor_set( &lighting_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

        gpu_commands->draw( TopologyType::Triangle, 0, 3, 0, 1 );
    }
}

void LightPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "lighting_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled )
        return;

    use_compute = node->compute;

    const u64 hashed_name = hash_calculate( "pbr_lighting" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;

    material_creation.set_name( "material_pbr" ).set_technique( main_technique ).set_render_index( 0 );
    Material* material_pbr = renderer->create_material( material_creation );

    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( LightingConstants ) ).set_name( "lighting_constants" );
    mesh.pbr_material.material_buffer = renderer->gpu->create_buffer( buffer_creation );

    BufferHandle fs_vb = renderer->gpu->get_fullscreen_vertex_buffer();
    mesh.position_buffer = fs_vb;

    color_texture = get_output_texture( frame_graph, node->inputs[ 0 ] );
    normal_texture = get_output_texture( frame_graph, node->inputs[ 1 ] );
    roughness_texture = get_output_texture( frame_graph, node->inputs[ 2 ] );
    emissive_texture = get_output_texture( frame_graph, node->inputs[ 3 ] );
    depth_texture = get_output_texture( frame_graph, node->inputs[ 4 ] );

    output_texture = frame_graph->access_resource( node->outputs[ 0 ] );

    mesh.pbr_material.material = material_pbr;
}

void LightPass::upload_gpu_data( RenderScene& scene ) {
    if ( !enabled )
        return;

    u32 current_frame_index = renderer->gpu->current_frame;

    MapBufferParameters cb_map = { mesh.pbr_material.material_buffer, 0, 0 };
    LightingConstants* lighting_data = ( LightingConstants* )renderer->gpu->map_buffer( cb_map );
    if ( lighting_data ) {
        lighting_data->albedo_index = color_texture->resource_info.texture.handle.index;;
        lighting_data->rmo_index = roughness_texture->resource_info.texture.handle.index;
        lighting_data->normal_index = normal_texture->resource_info.texture.handle.index;
        lighting_data->depth_index = depth_texture->resource_info.texture.handle.index;
        lighting_data->output_index = output_texture->resource_info.texture.handle.index;
        lighting_data->output_width = renderer->width;
        lighting_data->output_height = renderer->height;
        lighting_data->emissive = emissive_texture->resource_info.texture.handle.index;

        renderer->gpu->unmap_buffer( cb_map );
    }

    const u64 hashed_name = hash_calculate( "pbr_lighting" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    if ( last_lights_buffer.index != scene.lights_tiles_sb[ 0 ].index ) {
        scene.renderer->gpu->destroy_descriptor_set( mesh.pbr_material.descriptor_set );

        const u32 pass_index = use_compute ? 1 : 0;
        DescriptorSetCreation ds_creation{};
        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ pass_index ].pipeline, k_material_descriptor_set_index );

        for ( u32 i = 0; i < k_max_frames; ++i ) {

            scene.renderer->gpu->destroy_descriptor_set( lighting_descriptor_set[ i ] );

            ds_creation.reset().buffer( scene.scene_cb, 0 ).buffer( mesh.pbr_material.material_buffer, 1 ).buffer( scene.lights_lut_sb[ i ], 20 ).
                buffer( scene.lights_list_sb, 21 ).buffer( scene.lights_tiles_sb[ i ], 22 ).buffer( scene.lights_indices_sb[ i ], 25 ).set_layout( layout );

            lighting_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );

            // TODO(marco): this shouldn't be created here
            if ( scene.use_meshlets ) {
                scene.renderer->gpu->destroy_descriptor_set( scene.mesh_shader_transparent_descriptor_set[ i ] );

                GpuTechnique* transparent_technique = renderer->resource_cache.techniques.get( hash_calculate( "meshlet" ) );
                u32 meshlet_technique_index = transparent_technique->get_pass_index( "transparent_no_cull" );

                DescriptorSetLayoutHandle transparent_layout = renderer->gpu->get_descriptor_set_layout( transparent_technique->passes[ meshlet_technique_index ].pipeline, k_material_descriptor_set_index );

                ds_creation.reset().buffer( scene.scene_cb, 0 ).buffer( scene.meshlets_sb, 1 ).buffer( scene.meshes_sb, 2 )
                    .buffer( scene.meshlets_data_sb, 3 ).buffer( scene.meshlets_vertex_pos_sb, 4 ).buffer( scene.meshlets_vertex_data_sb, 5 )
                    .buffer( scene.mesh_task_indirect_early_commands_sb[ i ], 6 ).buffer( scene.mesh_task_indirect_count_early_sb[ i ], 7 )
                    .buffer( scene.mesh_instances_sb, 10 ).buffer( scene.mesh_bounds_sb, 12 )
                    .buffer( scene.lights_lut_sb[ i ], 20 ).buffer( scene.lights_list_sb, 21 )
                    .buffer( scene.lights_tiles_sb[ i ], 22 ).buffer( scene.lights_indices_sb[ i ], 25 ).set_layout( transparent_layout );

                scene.mesh_shader_transparent_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
            }
        }

        last_lights_buffer.index = scene.lights_tiles_sb[ 0 ].index;
    }
}

void LightPass::free_gpu_resources() {
    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    gpu.destroy_buffer( mesh.pbr_material.material_buffer );
    gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set );

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        gpu.destroy_descriptor_set( lighting_descriptor_set[ i ] );
    }
}

//
// TransparentPass ////////////////////////////////////////////////////////
void TransparentPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    Renderer* renderer = render_scene->renderer;

    if ( render_scene->use_meshlets_emulation ) {
        // TODO:
    }
    else if ( render_scene->use_meshlets ) {

        const u64 meshlet_hashed_name = hash_calculate( "meshlet" );
        GpuTechnique* meshlet_technique = renderer->resource_cache.techniques.get( meshlet_hashed_name );

        PipelineHandle pipeline = meshlet_technique->passes[ meshlet_technique_index ].pipeline;

        gpu_commands->bind_pipeline( pipeline );

        gpu_commands->bind_descriptor_set( &render_scene->mesh_shader_transparent_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

        // Transparent commands are put after mesh instances count commands.
        const u32 indirect_commands_offset = offsetof( GpuMeshDrawCommand, indirectMS ) + sizeof( GpuMeshDrawCommand ) * render_scene->mesh_instances.size;
        // Transparent count is after opaque and total count offset.
        const u32 indirect_count_offset = sizeof( u32 ) * 2;

        gpu_commands->draw_mesh_task_indirect( render_scene->mesh_task_indirect_early_commands_sb[ current_frame_index ], indirect_commands_offset,
                                               render_scene->mesh_task_indirect_count_early_sb[ current_frame_index ], indirect_count_offset, render_scene->mesh_instances.size, sizeof( GpuMeshDrawCommand ) );
    }
    else {
        Material* last_material = nullptr;
        for ( u32 mesh_index = 0; mesh_index < mesh_instance_draws.size; ++mesh_index ) {
            MeshInstanceDraw& mesh_instance_draw = mesh_instance_draws[ mesh_index ];
            Mesh& mesh = *mesh_instance_draw.mesh_instance->mesh;

            if ( mesh.pbr_material.material != last_material ) {
                PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, mesh_instance_draw.material_pass_index );

                gpu_commands->bind_pipeline( pipeline );

                last_material = mesh.pbr_material.material;
            }

            render_scene->draw_mesh_instance( gpu_commands, *mesh_instance_draw.mesh_instance );
        }
    }
}

void TransparentPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "transparent_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled )
        return;

    // Create pipeline state
    PipelineCreation pipeline_creation;

    const u64 hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    mesh_instance_draws.init( resident_allocator, 16 );

    for ( u32 i = 0; i < scene.mesh_instances.size; ++i ) {

        MeshInstance& mesh_instance = scene.mesh_instances[ i ];
        Mesh* mesh = mesh_instance.mesh;
        if ( !mesh->is_transparent() ) {
            continue;
        }

        MeshInstanceDraw mesh_instance_draw{};
        mesh_instance_draw.mesh_instance = &mesh_instance;
        mesh_instance_draw.material_pass_index = mesh->has_skinning() ? main_technique->get_pass_index( "transparent_skinning_no_cull" ) : main_technique->get_pass_index( "transparent_no_cull" );

        mesh_instance_draws.push( mesh_instance_draw );
    }

    // Cache meshlet technique index
    if ( renderer->gpu->mesh_shaders_extension_present ) {
        GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hash_calculate( "meshlet" ) );
        meshlet_technique_index = main_technique->get_pass_index( "transparent_no_cull" );
    }
}

void TransparentPass::free_gpu_resources() {
    if ( !enabled )
        return;

    mesh_instance_draws.shutdown();
}

//
// DebugPass ////////////////////////////////////////////////////////
static void load_debug_mesh( cstring filename, Allocator* resident_allocator, Renderer* renderer, u32& index_count, BufferResource** mesh_buffer, BufferResource** index_buffer ) {
    const aiScene* mesh_scene = aiImportFile( filename,
       aiProcess_CalcTangentSpace       |
       aiProcess_GenNormals             |
       aiProcess_Triangulate            |
       aiProcess_JoinIdenticalVertices  |
       aiProcess_SortByPType);

    Array<vec3s> positions;
    positions.init( resident_allocator, rkilo( 64 ) );

    Array<u32> indices;
    indices.init( resident_allocator, rkilo( 64 ) );

    index_count = 0;

    for ( u32 mesh_index = 0; mesh_index < mesh_scene->mNumMeshes; ++mesh_index ) {
       aiMesh* mesh = mesh_scene->mMeshes[ mesh_index ];

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

       index_count = indices.size;
    }

    {
       BufferCreation creation{ };
       sizet buffer_size = positions.size * sizeof( vec3s );
       creation.set( VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( positions.data ).set_name( "debug_mesh_pos" );

       *mesh_buffer = renderer->create_buffer( creation );
    }

    {
       BufferCreation creation{ };
       sizet buffer_size = indices.size * sizeof( u32 );
       creation.set( VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( indices.data ).set_name( "debug_mesh_indices" );

       *index_buffer = renderer->create_buffer( creation );
    }

    positions.shutdown();
    indices.shutdown();
}

void DebugPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    PipelineHandle pipeline = renderer->get_pipeline( debug_material, 0 );

    gpu_commands->bind_pipeline( pipeline );

#if ( DEBUG_DRAW_MESHLET_SPHERES || DEBUG_DRAW_POINT_LIGHT_SPHERES )
    gpu_commands->bind_vertex_buffer( sphere_mesh_buffer->handle, 0, 0 );
    gpu_commands->bind_index_buffer( sphere_mesh_indices->handle, 0, VK_INDEX_TYPE_UINT32 );

    gpu_commands->bind_descriptor_set( &sphere_mesh_descriptor_set, 1, nullptr, 0 );

    gpu_commands->draw_indexed_indirect( sphere_draw_indirect_buffer->handle, bounding_sphere_count, 0, sizeof( VkDrawIndexedIndirectCommand ) );
#endif

#if DEBUG_DRAW_MESHLET_CONES
    gpu_commands->bind_vertex_buffer( cone_mesh_buffer->handle, 0, 0 );
    gpu_commands->bind_index_buffer( cone_mesh_indices->handle, 0, VK_INDEX_TYPE_UINT32 );

    gpu_commands->bind_descriptor_set( &cone_mesh_descriptor_set, 1, nullptr, 0 );

    gpu_commands->draw_indexed_indirect( cone_draw_indirect_buffer->handle, bounding_sphere_count, 0, sizeof( VkDrawIndexedIndirectCommand ) );
#endif

    // pipeline = renderer->get_pipeline( debug_material, 1 );

    // gpu_commands->bind_pipeline( pipeline );

    // for ( u32 mesh_index = 0; mesh_index < mesh_instances.size; ++mesh_index ) {
    //     MeshInstance& mesh_instance = mesh_instances[ mesh_index ];
    //     Mesh& mesh = *mesh_instance.mesh;

    //     if ( mesh.physics_mesh != nullptr ) {
    //         PhysicsMesh* physics_mesh = mesh.physics_mesh;

    //         gpu_commands->bind_descriptor_set( &physics_mesh->debug_mesh_descriptor_set, 1, nullptr, 0 );

    //         gpu_commands->draw_indirect( physics_mesh->draw_indirect_buffer, physics_mesh->vertices.size, 0, sizeof( VkDrawIndirectCommand  ) );
    //     }
    // }

    // Draw gpu written debug lines
    if ( render_scene->show_debug_gpu_draws ) {

        gpu_commands->bind_pipeline( debug_lines_draw_pipeline );
        gpu_commands->bind_descriptor_set( &debug_lines_draw_set, 1, nullptr, 0 );
        gpu_commands->draw_indirect( render_scene->debug_line_commands_sb, 1, 0, sizeof( VkDrawIndirectCommand ) );
        // Draw 2d lines
        gpu_commands->bind_pipeline( debug_lines_2d_draw_pipeline );
        gpu_commands->bind_descriptor_set( &debug_lines_draw_set, 1, nullptr, 0 );
        gpu_commands->draw_indirect( render_scene->debug_line_commands_sb, 1, sizeof( VkDrawIndirectCommand ), sizeof( VkDrawIndirectCommand ) );
    }
}

void DebugPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {

    if ( !enabled ) {
        return;
    }

    Buffer* line_commands = renderer->gpu->access_buffer( debug_line_commands_sb_cache );

    util_add_buffer_barrier( renderer->gpu, gpu_commands->vk_command_buffer, line_commands->vk_buffer,
                             RESOURCE_STATE_INDIRECT_ARGUMENT, RESOURCE_STATE_UNORDERED_ACCESS, line_commands->size );

    // Write final command
    gpu_commands->bind_pipeline( debug_lines_finalize_pipeline );
    gpu_commands->bind_descriptor_set( &debug_lines_finalize_set, 1, nullptr, 0 );
    gpu_commands->dispatch( 1, 1, 1 );

    util_add_buffer_barrier( renderer->gpu, gpu_commands->vk_command_buffer, line_commands->vk_buffer,
                             RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_INDIRECT_ARGUMENT, line_commands->size );
}

void DebugPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;
    scene_graph = scene.scene_graph;

    FrameGraphNode* node = frame_graph->get_node( "debug_pass" );
    if ( node == nullptr ) {
       enabled = false;

       return;
    }

    enabled = node->enabled;
    if ( !enabled )
       return;

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

#if ( DEBUG_DRAW_MESHLET_SPHERES | DEBUG_DRAW_POINT_LIGHT_SPHERES)
    load_debug_mesh( filename, resident_allocator, renderer, sphere_index_count, &sphere_mesh_buffer, &sphere_mesh_indices );
#endif // DEBUG_DRAW_MESHLET_SPHERES | DEBUG_DRAW_POINT_LIGHT_SPHERES

    filename = mesh_name.append_use_f( "%s/cone.obj", RAPTOR_DATA_FOLDER );

#if DEBUG_DRAW_MESHLET_CONES
    load_debug_mesh( filename, resident_allocator, renderer, cone_index_count, &cone_mesh_buffer, &cone_mesh_indices );
#endif // DEBUG_DRAW_MESHLET_CONES

    scratch_allocator->free_marker( marker );

    // Get all meshlets bounding spheres
    Array<mat4s> bounding_matrices;
    bounding_matrices.init( resident_allocator, 4096 );

    Array<VkDrawIndexedIndirectCommand> sphere_indirect_commands;
    sphere_indirect_commands.init( resident_allocator, 4096 );

#if DEBUG_DRAW_MESHLET_SPHERES
    Array<mat4s> cone_matrices;
    cone_matrices.init( resident_allocator, 4096 );

    Array<VkDrawIndexedIndirectCommand> cone_indirect_commands;
    cone_indirect_commands.init( resident_allocator, 4096 );

    for ( u32 i = 0; i < scene.meshlets.size; ++i ) {
       GpuMeshlet& meshlet = scene.meshlets[ i ];

       if ( meshlet.radius == 0.0f ) {
           // NOTE(marco): meshlet that was added for padding
           continue;
       }

       if ( meshlet.radius > 80.0f) {
           continue;
       }

        MeshInstance& mesh = scene.mesh_instances[ meshlet.mesh_index ];
        mat4s local_transform = scene_graph->local_matrices[ mesh.scene_graph_node_index ];

        // Meshlet bounding spheres
        mat4s sphere_bounding_matrix = glms_mat4_identity();
        sphere_bounding_matrix = glms_translate( sphere_bounding_matrix, meshlet.center );
        sphere_bounding_matrix = glms_scale( sphere_bounding_matrix, vec3s{ meshlet.radius, meshlet.radius, meshlet.radius } );
        sphere_bounding_matrix = glms_mat4_mul( local_transform, sphere_bounding_matrix );

        bounding_matrices.push( sphere_bounding_matrix );

        VkDrawIndexedIndirectCommand draw_command{ };
        draw_command.indexCount = sphere_index_count;
        draw_command.instanceCount = 1;

        sphere_indirect_commands.push( draw_command );

        // Meshlet cones
        vec3s up{ 0.0f, 1.0f, 0.0f };

        vec3s cone_axis{ meshlet.cone_axis[0] / 127.0f, meshlet.cone_axis[1] / 127.0f, meshlet.cone_axis[2] / 127.0f };
        cone_axis = glms_vec3_normalize( cone_axis );

        versors qrotation = glms_quat_from_vecs( up, cone_axis );
        mat4s rotation = glms_quat_mat4( qrotation );

        mat4s id = glms_mat4_identity();
        mat4s t = glms_translate( id, meshlet.center );
        mat4s s = glms_scale( id, vec3s{ meshlet.radius * 0.5f, meshlet.radius * 0.5f, meshlet.radius * 0.5f } );
        mat4s r = glms_mat4_mul( id, rotation );

        mat4s cone_matrix = glms_mat4_mul( glms_mat4_mul( t, r ), s );
        cone_matrix = glms_mat4_mul( local_transform, cone_matrix );

        cone_matrices.push( cone_matrix );

        draw_command = { };
        draw_command.indexCount = cone_index_count;
        draw_command.instanceCount = 1;

        cone_indirect_commands.push( draw_command );
    }

    bounding_sphere_count = bounding_matrices.size;

    {
       BufferCreation creation{ };
       sizet buffer_size = bounding_matrices.size * sizeof( mat4s );
       creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( bounding_matrices.data ).set_name( "meshlet_bounding_spheres_transform" );

       sphere_matrices_buffer = renderer->create_buffer( creation );
    }

    {
       BufferCreation creation{ };
       sizet buffer_size = sphere_indirect_commands.size * sizeof( VkDrawIndexedIndirectCommand );
       creation.set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( sphere_indirect_commands.data ).set_name( "meshlet_bound_sphere_draw_commands" );

       sphere_draw_indirect_buffer = renderer->create_buffer( creation );
    }

    {
        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ 0 ].pipeline, k_material_descriptor_set_index );

        DescriptorSetCreation creation{ };
        creation.buffer( scene.scene_cb, 0 ).buffer( sphere_matrices_buffer->handle, 1 ).set_layout( layout );

        sphere_mesh_descriptor_set = renderer->gpu->create_descriptor_set( creation );
    }

    {
       BufferCreation creation{ };
       sizet buffer_size = cone_matrices.size * sizeof( mat4s );
       creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( cone_matrices.data ).set_name( "meshlet_cones_transform" );

       cone_matrices_buffer = renderer->create_buffer( creation );
    }

    {
       BufferCreation creation{ };
       sizet buffer_size = cone_indirect_commands.size * sizeof( VkDrawIndexedIndirectCommand );
       creation.set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( cone_indirect_commands.data ).set_name( "meshlet_cone_draw_commands" );

       cone_draw_indirect_buffer = renderer->create_buffer( creation );
    }

    {
        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ 0 ].pipeline, k_material_descriptor_set_index );

        DescriptorSetCreation creation{ };
        creation.buffer( scene.scene_cb, 0 ).buffer( cone_matrices_buffer->handle, 1 ).set_layout( layout );

        cone_mesh_descriptor_set = renderer->gpu->create_descriptor_set( creation );
    }


    cone_matrices.shutdown();
    cone_indirect_commands.shutdown();
#endif

#if DEBUG_DRAW_POINT_LIGHT_SPHERES
    for ( u32 i = 0; i < k_num_lights; ++i ) {
        Light& light = scene.lights[ i ];

        // Meshlet bounding spheres
        mat4s sphere_bounding_matrix = glms_mat4_identity();
        sphere_bounding_matrix = glms_translate( sphere_bounding_matrix, light.world_position );
        sphere_bounding_matrix = glms_scale( sphere_bounding_matrix, vec3s{ light.radius, light.radius, light.radius } );

        bounding_matrices.push( sphere_bounding_matrix );

        VkDrawIndexedIndirectCommand draw_command{ };
        draw_command.indexCount = sphere_index_count;
        draw_command.instanceCount = 1;

        sphere_indirect_commands.push( draw_command );
    }

    bounding_sphere_count = bounding_matrices.size;

    {
       BufferCreation creation{ };
       sizet buffer_size = bounding_matrices.size * sizeof( mat4s );
       creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( bounding_matrices.data ).set_name( "lights_bounding_spheres_transform" );

       sphere_matrices_buffer = renderer->create_buffer( creation );
    }

    {
       BufferCreation creation{ };
       sizet buffer_size = sphere_indirect_commands.size * sizeof( VkDrawIndexedIndirectCommand );
       creation.set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( sphere_indirect_commands.data ).set_name( "lights_bound_sphere_draw_commands" );

       sphere_draw_indirect_buffer = renderer->create_buffer( creation );
    }

    {
        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ 0 ].pipeline, k_material_descriptor_set_index );

        DescriptorSetCreation creation{ };
        creation.buffer( scene.scene_cb, 0 ).buffer( sphere_matrices_buffer->handle, 1 ).set_layout( layout );

        sphere_mesh_descriptor_set = renderer->gpu->create_descriptor_set( creation );
    }
#endif

    bounding_matrices.shutdown();
    sphere_indirect_commands.shutdown();

    // Prepare gpu debug line resources
    {
        DescriptorSetCreation descriptor_set_creation{ };

        // Finalize pass
        u32 pass_index = main_technique->get_pass_index( "commands_finalize" );

        debug_lines_finalize_pipeline = main_technique->passes[ pass_index ].pipeline;
        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ pass_index ].pipeline, k_material_descriptor_set_index );

        DescriptorSetCreation set_creation{ };
        set_creation.set_layout( layout ).buffer( scene.scene_cb, 0 ).buffer( scene.debug_line_sb, 20 ).buffer( scene.debug_line_count_sb, 21 ).buffer( scene.debug_line_commands_sb, 22 );
        debug_lines_finalize_set = renderer->gpu->create_descriptor_set( set_creation );

        // Draw pass
        pass_index = main_technique->get_pass_index( "debug_line_gpu" );
        debug_lines_draw_pipeline = main_technique->passes[ pass_index ].pipeline;
        layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ pass_index ].pipeline, k_material_descriptor_set_index );

        set_creation.reset().set_layout( layout ).buffer( scene.scene_cb, 0 ).buffer( scene.debug_line_sb, 20 ).buffer( scene.debug_line_count_sb, 21 ).buffer( scene.debug_line_commands_sb, 22 );
        debug_lines_draw_set = renderer->gpu->create_descriptor_set( set_creation );

        pass_index = main_technique->get_pass_index( "debug_line_2d_gpu" );
        debug_lines_2d_draw_pipeline = main_technique->passes[ pass_index ].pipeline;

        debug_line_commands_sb_cache = scene.debug_line_commands_sb;
    }
}

void DebugPass::free_gpu_resources() {
    if ( !enabled )
        return;

#if ( DEBUG_DRAW_MESHLET_SPHERES | DEBUG_DRAW_POINT_LIGHT_SPHERES)
    renderer->destroy_buffer( sphere_mesh_indices );
    renderer->destroy_buffer( sphere_mesh_buffer );
    renderer->destroy_buffer( sphere_matrices_buffer );
    renderer->destroy_buffer( sphere_draw_indirect_buffer );

    renderer->gpu->destroy_descriptor_set( sphere_mesh_descriptor_set );
#endif

#if DEBUG_DRAW_MESHLET_CONES
    renderer->destroy_buffer( cone_mesh_indices );
    renderer->destroy_buffer( cone_mesh_buffer );
    renderer->destroy_buffer( cone_matrices_buffer );
    renderer->destroy_buffer( cone_draw_indirect_buffer );

    renderer->gpu->destroy_descriptor_set( cone_mesh_descriptor_set );
#endif

    renderer->gpu->destroy_descriptor_set( debug_lines_finalize_set );
    renderer->gpu->destroy_descriptor_set( debug_lines_draw_set );
}

//
// DoFPass ////////////////////////////////////////////////////////////////
void DoFPass::add_ui() {
    if ( !enabled )
        return;

    ImGui::InputFloat( "Focal Length", &focal_length );
    ImGui::InputFloat( "Plane in Focus", &plane_in_focus );
    ImGui::InputFloat( "Aperture", &aperture );
}

void DoFPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {

    FrameGraphResource* texture = ( FrameGraphResource* )frame_graph->get_resource( "lighting" );
    RASSERT( texture != nullptr );

    gpu_commands->copy_texture( texture->resource_info.texture.handle, scene_mips->handle, RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
}

void DoFPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, 0 );

    gpu_commands->bind_pipeline( pipeline );
    gpu_commands->bind_vertex_buffer( mesh.position_buffer, 0, 0 );
    gpu_commands->bind_descriptor_set( &mesh.pbr_material.descriptor_set, 1, nullptr, 0 );

    gpu_commands->draw( TopologyType::Triangle, 0, 3, 0, 1 );
}

//TODO:
static TextureCreation dof_scene_tc;

void DoFPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {
    if ( !enabled ) return;

    u32 w = new_width;
    u32 h = new_height;

    u32 mips = 1;
    while ( w > 1 && h > 1 ) {
        w /= 2;
        h /= 2;
        mips++;
    }

    // Destroy scene mips
    {
        renderer->destroy_texture( scene_mips );

        // Reuse cached texture creation and create new scene mips.
        dof_scene_tc.set_mips( mips ).set_size( new_width, new_height, 1 );
        scene_mips = renderer->create_texture( dof_scene_tc );
    }
}

void DoFPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "depth_of_field_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled )
        return;

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

    dof_scene_tc.set_data( nullptr ).set_format_type( info.texture.format, TextureType::Texture2D ).set_mips( mips ).set_size( ( u16 )info.texture.width, ( u16 )info.texture.height, 1 ).set_name( "scene_mips" );
    {
        scene_mips = renderer->create_texture( dof_scene_tc );
    }
    mesh.pbr_material.material = material_dof;

    znear = 0.1f;
    zfar = 1000.0f;
    focal_length = 5.0f;
    plane_in_focus = 1.0f;
    aperture = 8.0f;
}

void DoFPass::upload_gpu_data() {
    if ( !enabled )
        return;

    u32 current_frame_index = renderer->gpu->current_frame;

    MapBufferParameters cb_map = { mesh.pbr_material.material_buffer, 0, 0 };
    DoFData* dof_data = ( DoFData* )renderer->gpu->map_buffer( cb_map );
    if ( dof_data ) {
        dof_data->textures[ 0 ] = scene_mips->handle.index;
        dof_data->textures[ 1 ] = depth_texture->resource_info.texture.handle.index;

        dof_data->znear = znear;
        dof_data->zfar = zfar;
        dof_data->focal_length = focal_length;
        dof_data->plane_in_focus = plane_in_focus;
        dof_data->aperture = aperture;

        renderer->gpu->unmap_buffer( cb_map );
    }
}

void DoFPass::free_gpu_resources() {
    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    renderer->destroy_texture( scene_mips );

    gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set );
}

//
// MeshPass ////////////////////////////////////////////////////////
void MeshPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    Renderer* renderer = render_scene->renderer;

    const u64 meshlet_hashed_name = hash_calculate( "meshlet" );
    GpuTechnique* meshlet_technique = renderer->resource_cache.techniques.get( meshlet_hashed_name );

    PipelineHandle pipeline = meshlet_technique->passes[ 0 ].pipeline;

    gpu_commands->bind_pipeline( pipeline );

    gpu_commands->bind_descriptor_set( &render_scene->mesh_shader_early_descriptor_set[ current_frame_index ], 1, nullptr, 0);

    gpu_commands->draw_mesh_task( ( render_scene->meshlets.size + 31 ) / 32, 0 );
}

void MeshPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    FrameGraphNode* node = frame_graph->get_node( "mesh_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
}

// CullingEarlyPass /////////////////////////////////////////////////////////
void CullingEarlyPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {

    if ( !enabled )
        return;

    Renderer* renderer = render_scene->renderer;

    // Frustum cull meshes
    GpuMeshDrawCounts& mesh_draw_counts = render_scene->mesh_draw_counts;
    mesh_draw_counts.opaque_mesh_visible_count = 0;
    mesh_draw_counts.opaque_mesh_culled_count = 0;
    mesh_draw_counts.transparent_mesh_visible_count = 0;
    mesh_draw_counts.transparent_mesh_culled_count = 0;

    mesh_draw_counts.total_count = render_scene->mesh_instances.size;
    mesh_draw_counts.depth_pyramid_texture_index = depth_pyramid_texture_index;
    mesh_draw_counts.late_flag = 0;
    mesh_draw_counts.meshlet_index_count = 0;
    mesh_draw_counts.dispatch_task_x = 0;
    mesh_draw_counts.dispatch_task_y = 1;
    mesh_draw_counts.dispatch_task_z = 1;

    // Reset mesh draw counts
    MapBufferParameters cb_map{ render_scene->mesh_task_indirect_count_early_sb[ current_frame_index ], 0, 0};
    GpuMeshDrawCounts* count_data = ( GpuMeshDrawCounts* )renderer->gpu->map_buffer( cb_map );
    if ( count_data ) {
        *count_data = mesh_draw_counts;

        renderer->gpu->unmap_buffer( cb_map );
    }

    // Reset debug draw counts
    cb_map.buffer = render_scene->debug_line_count_sb;
    f32* debug_line_count = ( f32* )renderer->gpu->map_buffer( cb_map );
    if ( debug_line_count ) {

        debug_line_count[ 0 ] = 0;
        debug_line_count[ 1 ] = 0;
        debug_line_count[ 2 ] = renderer->gpu->current_frame;
        debug_line_count[ 3 ] = 0;

        renderer->gpu->unmap_buffer( cb_map );
    }

    gpu_commands->bind_pipeline( frustum_cull_pipeline );

    const Buffer* visible_commands_sb = renderer->gpu->access_buffer( render_scene->mesh_task_indirect_early_commands_sb[ current_frame_index ] );
    util_add_buffer_barrier( renderer->gpu, gpu_commands->vk_command_buffer, visible_commands_sb->vk_buffer,
                                 RESOURCE_STATE_INDIRECT_ARGUMENT, RESOURCE_STATE_UNORDERED_ACCESS, visible_commands_sb->size );

    const Buffer* count_sb = renderer->gpu->access_buffer( render_scene->mesh_task_indirect_count_early_sb[ current_frame_index ] );
    util_add_buffer_barrier( renderer->gpu, gpu_commands->vk_command_buffer, count_sb->vk_buffer,
                                 RESOURCE_STATE_INDIRECT_ARGUMENT, RESOURCE_STATE_UNORDERED_ACCESS, count_sb->size );

    gpu_commands->bind_descriptor_set( &frustum_cull_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

    u32 group_x = raptor::ceilu32( render_scene->mesh_instances.size / 64.0f );
    gpu_commands->dispatch( group_x, 1, 1 );

    util_add_buffer_barrier( renderer->gpu, gpu_commands->vk_command_buffer, visible_commands_sb->vk_buffer,
                                 RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_INDIRECT_ARGUMENT, visible_commands_sb->size );

    util_add_buffer_barrier( renderer->gpu, gpu_commands->vk_command_buffer, count_sb->vk_buffer,
                                 RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_INDIRECT_ARGUMENT, count_sb->size );
}

void CullingEarlyPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {

    FrameGraphNode* node = frame_graph->get_node( "mesh_occlusion_early_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;

    renderer = scene.renderer;
    GpuDevice& gpu = *renderer->gpu;

    // Cache frustum cull shader
    GpuTechnique* culling_technique = renderer->resource_cache.techniques.get( hash_calculate( "culling" ) );
    {
        u32 pipeline_index = culling_technique->get_pass_index( "gpu_mesh_culling" );
        frustum_cull_pipeline = culling_technique->passes[ pipeline_index ].pipeline;
        DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( frustum_cull_pipeline, k_material_descriptor_set_index );

        for ( u32 i = 0; i < k_max_frames; ++i ) {
            DescriptorSetCreation ds_creation{};
            ds_creation.buffer( scene.meshes_sb, 2 ).buffer( scene.mesh_instances_sb, 10 ).buffer( scene.scene_cb, 0 )
                .buffer( scene.mesh_task_indirect_count_early_sb[ i ], 11 ).buffer( scene.mesh_task_indirect_count_early_sb[ i ], 13 ).buffer(scene.mesh_task_indirect_early_commands_sb[ i ], 1 ).buffer(scene.mesh_task_indirect_culled_commands_sb[ i ], 3 )
                .buffer( scene.mesh_bounds_sb, 12 ).buffer( scene.debug_line_sb, 20 ).buffer( scene.debug_line_count_sb, 21 ).buffer( scene.debug_line_commands_sb, 22 ).set_layout(layout);

            frustum_cull_descriptor_set[ i ] = gpu.create_descriptor_set(ds_creation);
        }
    }
}

void CullingEarlyPass::free_gpu_resources() {
    GpuDevice& gpu = *renderer->gpu;

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        gpu.destroy_descriptor_set( frustum_cull_descriptor_set[ i ]);
    }
}

// CullingLatePass /////////////////////////////////////////////////////////
void CullingLatePass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {

    if ( !enabled )
        return;

    Renderer* renderer = render_scene->renderer;

    // Frustum cull meshes
    GpuMeshDrawCounts& mesh_draw_counts = render_scene->mesh_draw_counts;
    mesh_draw_counts.opaque_mesh_visible_count = 0;
    mesh_draw_counts.opaque_mesh_culled_count = 0;
    mesh_draw_counts.transparent_mesh_visible_count = 0;
    mesh_draw_counts.transparent_mesh_culled_count = 0;
    mesh_draw_counts.late_flag = 1;

    mesh_draw_counts.total_count = render_scene->mesh_instances.size;
    mesh_draw_counts.depth_pyramid_texture_index = depth_pyramid_texture_index;

    MapBufferParameters cb_map{ render_scene->mesh_task_indirect_count_late_sb[ current_frame_index ], 0, 0};
    GpuMeshDrawCounts* count_data = ( GpuMeshDrawCounts* )renderer->gpu->map_buffer( cb_map );
    if ( count_data ) {
        *count_data = mesh_draw_counts;

        renderer->gpu->unmap_buffer( cb_map );
    }

    gpu_commands->bind_pipeline( frustum_cull_pipeline );

    const Buffer* visible_commands_sb = renderer->gpu->access_buffer( render_scene->mesh_task_indirect_late_commands_sb[ current_frame_index ] );
    util_add_buffer_barrier( renderer->gpu, gpu_commands->vk_command_buffer, visible_commands_sb->vk_buffer,
                                 RESOURCE_STATE_INDIRECT_ARGUMENT, RESOURCE_STATE_UNORDERED_ACCESS, visible_commands_sb->size );

    const Buffer* count_sb = renderer->gpu->access_buffer( render_scene->mesh_task_indirect_count_late_sb[ current_frame_index ] );
    util_add_buffer_barrier( renderer->gpu, gpu_commands->vk_command_buffer, count_sb->vk_buffer,
                                 RESOURCE_STATE_INDIRECT_ARGUMENT, RESOURCE_STATE_UNORDERED_ACCESS, count_sb->size );

    gpu_commands->bind_descriptor_set( &frustum_cull_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

    u32 group_x = raptor::ceilu32( render_scene->mesh_instances.size / 64.0f );
    gpu_commands->dispatch( group_x, 1, 1 );

    util_add_buffer_barrier( renderer->gpu, gpu_commands->vk_command_buffer, visible_commands_sb->vk_buffer,
                                 RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_INDIRECT_ARGUMENT, visible_commands_sb->size );

    util_add_buffer_barrier( renderer->gpu, gpu_commands->vk_command_buffer, count_sb->vk_buffer,
                                 RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_INDIRECT_ARGUMENT, count_sb->size );
}

void CullingLatePass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {

    FrameGraphNode* node = frame_graph->get_node( "mesh_occlusion_late_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;

    renderer = scene.renderer;
    GpuDevice& gpu = *renderer->gpu;

    // Cache frustum cull shader
    GpuTechnique* culling_technique = renderer->resource_cache.techniques.get( hash_calculate( "culling" ) );
    {
        frustum_cull_pipeline = culling_technique->passes[ 0 ].pipeline;
        DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( frustum_cull_pipeline, k_material_descriptor_set_index );

        for ( u32 i = 0; i < k_max_frames; ++i ) {
            DescriptorSetCreation ds_creation{};
            ds_creation.buffer( scene.meshes_sb, 2 ).buffer( scene.mesh_instances_sb, 10 ).buffer( scene.scene_cb, 0 )
                .buffer( scene.mesh_task_indirect_count_late_sb[ i ], 11 ).buffer( scene.mesh_task_indirect_count_early_sb[ i ], 13 ).buffer(scene.mesh_task_indirect_late_commands_sb[ i ], 1 ).buffer(scene.mesh_task_indirect_culled_commands_sb[ i ], 3 )
                .buffer( scene.mesh_bounds_sb, 12 ).buffer( scene.mesh_bounds_sb, 12 ).buffer( scene.debug_line_sb, 20 ).buffer( scene.debug_line_count_sb, 21 ).buffer( scene.debug_line_commands_sb, 22 ).set_layout(layout);

            frustum_cull_descriptor_set[ i ] = gpu.create_descriptor_set(ds_creation);
        }
    }
}

void CullingLatePass::free_gpu_resources() {
    GpuDevice& gpu = *renderer->gpu;

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        gpu.destroy_descriptor_set( frustum_cull_descriptor_set[ i ]);
    }
}


// RenderScene ////////////////////////////////////////////////////////////
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
                cb = gpu.get_command_buffer( 0, gpu.current_frame, true );

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

        // If marker are present, then queries are as well.
        if ( cb->thread_frame_pool->time_queries->allocated_time_query ) {
            vkCmdEndQuery( cb->vk_command_buffer, cb->thread_frame_pool->vulkan_pipeline_stats_query_pool, 0 );
        }

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

struct SortedLight {

    u32             light_index;
    f32             projected_z;
    f32             projected_z_min;
    f32             projected_z_max;
}; // struct SortedLight

static int sorting_light_fn( const void* a, const void* b ) {
    const SortedLight* la = (const SortedLight*)a;
    const SortedLight* lb = (const SortedLight*)b;

    if ( la->projected_z < lb->projected_z ) return -1;
    else if ( la->projected_z > lb->projected_z ) return 1;
    return 0;
}

void RenderScene::upload_gpu_data( UploadGpuDataContext& context ) {

    GpuDevice& gpu = *renderer->gpu;

    // Update per mesh material buffer
    // TODO: update only changed stuff, this is now dynamic so it can't be done.
    MapBufferParameters cb_map = { meshes_sb, 0, 0 };
    GpuMaterialData* gpu_mesh_data = ( GpuMaterialData* )gpu.map_buffer( cb_map );
    if ( gpu_mesh_data ) {
        for ( u32 mesh_index = 0; mesh_index < meshes.size; ++mesh_index ) {
            copy_gpu_material_data( gpu_mesh_data[ mesh_index ], meshes[ mesh_index ] );
        }
        gpu.unmap_buffer( cb_map );
    }

    // Copy mesh bounding spheres
    cb_map.buffer = mesh_bounds_sb;
    vec4s* gpu_bounds_data = (vec4s*)gpu.map_buffer( cb_map );
    if ( gpu_bounds_data ) {
        for ( u32 mesh_index = 0; mesh_index < meshes.size; ++mesh_index ) {
            gpu_bounds_data[ mesh_index ] = meshes[ mesh_index ].bounding_sphere;
        }
        gpu.unmap_buffer( cb_map );
    }

    // Copy mesh instances data
    cb_map.buffer = mesh_instances_sb;
    GpuMeshInstanceData* gpu_mesh_instance_data = ( GpuMeshInstanceData* )gpu.map_buffer( cb_map );
    if ( gpu_mesh_instance_data ) {
        for ( u32 mi = 0; mi < mesh_instances.size; ++mi ) {
            copy_gpu_mesh_transform( gpu_mesh_instance_data[ mi ], mesh_instances[ mi ], global_scale, scene_graph );
        }
        gpu.unmap_buffer( cb_map );
    }

    sizet current_marker = context.scratch_allocator->get_marker();

    Array<SortedLight> sorted_lights;
    sorted_lights.init( context.scratch_allocator, k_num_lights, k_num_lights );

    // Sort lights based on Z
    mat4s& world_to_camera = scene_data.world_to_camera;
    float z_far = scene_data.z_far;
    for ( u32 i = 0; i < k_num_lights; ++i ) {
        Light& light = lights[ i ];

        vec4s p{ light.world_position.x, light.world_position.y, light.world_position.z, 1.0f };

        vec4s projected_p = glms_mat4_mulv( world_to_camera, p );
        vec4s projected_p_min = glms_vec4_add( projected_p, { 0,0,-light.radius, 0 } );
        vec4s projected_p_max = glms_vec4_add( projected_p, { 0,0,light.radius, 0 } );

        // NOTE(marco): linearize depth
        SortedLight& sorted_light = sorted_lights[ i ];
        sorted_light.light_index = i;
        // Remove negative numbers as they cause false negatives for bin 0.
        sorted_light.projected_z = ( ( projected_p.z - scene_data.z_near ) / ( z_far - scene_data.z_near ) );
        sorted_light.projected_z_min = ( ( projected_p_min.z - scene_data.z_near ) / ( z_far - scene_data.z_near ) );
        sorted_light.projected_z_max = ( ( projected_p_max.z - scene_data.z_near ) / ( z_far - scene_data.z_near ) );

        //rprint( "Light Z %f, Zmin %f, Zmax %f\n", sorted_light.projected_z, sorted_light.projected_z_min, sorted_light.projected_z_max );
    }

    qsort( sorted_lights.data, k_num_lights, sizeof( SortedLight ), sorting_light_fn );

    // Upload light list
    cb_map.buffer = lights_list_sb;
    GpuLight* gpu_lights_data = ( GpuLight* )gpu.map_buffer( cb_map );
    if ( gpu_lights_data ) {
        for ( u32 i = 0; i < k_num_lights; ++i ) {
            Light& light = lights[ i ];
            GpuLight& gpu_light = gpu_lights_data[ i ];

            gpu_light.world_position = light.world_position;
            gpu_light.attenuation = light.radius;
            gpu_light.color = light.color;
            gpu_light.intensity = light.intensity;
        }

        gpu.unmap_buffer( cb_map );
    }

    // Calculate lights LUT
    // NOTE(marco): it might be better to use logarithmic slices to have better resolution
    // closer to the camera. We could also use a different far plane and discard any lights
    // that are too far
    f32 bin_size = 1.0f / k_light_z_bins;

    for ( u32 bin = 0; bin < k_light_z_bins; ++bin ) {
        u32 min_light_id = k_num_lights + 1;
        u32 max_light_id = 0;

        f32 bin_min = bin_size * bin;
        f32 bin_max = bin_min + bin_size;

        for ( u32 i = 0; i < k_num_lights; ++i ) {
            const SortedLight& light = sorted_lights[ i ];

            if ( ( light.projected_z >= bin_min && light.projected_z <= bin_max ) ||
                 ( light.projected_z_min >= bin_min && light.projected_z_min <= bin_max ) ||
                 ( light.projected_z_max >= bin_min && light.projected_z_max <= bin_max ) ) {
                if ( i < min_light_id ) {
                    min_light_id = i;
                }

                if ( i > max_light_id ) {
                    max_light_id = i;
                }
            }
        }

        lights_lut[ bin ] = min_light_id | ( max_light_id << 16 );
    }

    // Upload light indices
    cb_map.buffer = lights_indices_sb[ gpu.current_frame ];

    u32* gpu_light_indices = ( u32* )gpu.map_buffer( cb_map );
    if ( gpu_light_indices ) {
        // TODO: improve
        //memcpy( gpu_light_indices, lights_lut.data, lights_lut.size * sizeof( u32 ) );
        for ( u32 i = 0; i < k_num_lights; ++i ) {
            gpu_light_indices[ i ] = sorted_lights[ i ].light_index;
        }

        gpu.unmap_buffer( cb_map );
    }

    // Upload lights LUT
    cb_map.buffer = lights_lut_sb[ gpu.current_frame ];
    u32* gpu_lut_data = ( u32* )gpu.map_buffer( cb_map );
    if ( gpu_lut_data ) {
        memcpy( gpu_lut_data, lights_lut.data, lights_lut.size * sizeof( u32 ) );

        gpu.unmap_buffer( cb_map );
    }

    const u32 tile_x_count = scene_data.resolution_x / k_tile_size;
    const u32 tile_y_count = scene_data.resolution_y / k_tile_size;
    const u32 tiles_entry_count = tile_x_count * tile_y_count * k_num_words;
    const u32 buffer_size = tiles_entry_count * sizeof( u32 );

    // Assign light
    Array<u32> light_tiles_bits;
    light_tiles_bits.init( context.scratch_allocator, tiles_entry_count, tiles_entry_count );
    memset( light_tiles_bits.data, 0, buffer_size );

    float near_z = scene_data.z_near;
    float tile_size_inv = 1.0f / k_tile_size;

    u32 tile_stride = tile_x_count * k_num_words;

    GameCamera& game_camera = context.game_camera;

    for ( u32 i = 0; i < k_num_lights; ++i ) {
        const u32 light_index = sorted_lights[ i ].light_index;
        Light& light = lights[ light_index ];

        vec4s pos{ light.world_position.x, light.world_position.y, light.world_position.z, 1.0f };
        float radius = light.radius;

        vec4s view_space_pos = glms_mat4_mulv( game_camera.camera.view, pos );
        bool camera_visible = -view_space_pos.z - radius < game_camera.camera.near_plane;

        if ( !camera_visible && context.skip_invisible_lights ) {
            continue;
        }

        //rprint( "Camera vis %u view z %f\n", camera_visible ? 1 : 0, view_space_pos.z );

        // X is positive, then it returns the same values as the longer method.
        vec2s cx{ view_space_pos.x, view_space_pos.z };
        const f32 tx_squared = glms_vec2_dot( cx, cx ) - ( radius * radius );
        const bool tx_camera_inside = tx_squared <= 0;//
        vec2s vx{ sqrtf( tx_squared ), radius };
        mat2s xtransf_min{ vx.x, vx.y, -vx.y, vx.x };
        vec2s minx = glms_mat2_mulv( xtransf_min, cx );
        mat2s xtransf_max{ vx.x, -vx.y, vx.y, vx.x };
        vec2s maxx = glms_mat2_mulv( xtransf_max, cx );

        vec2s cy{ -view_space_pos.y, view_space_pos.z };
        const f32 ty_squared = glms_vec2_dot( cy, cy ) - ( radius * radius );
        const bool ty_camera_inside = ty_squared <= 0;//
        vec2s vy{ sqrtf( ty_squared ), radius };
        mat2s ytransf_min{ vy.x, vy.y, -vy.y, vy.x };
        vec2s miny = glms_mat2_mulv( ytransf_min, cy );
        mat2s ytransf_max{ vy.x, -vy.y, vy.y, vy.x };
        vec2s maxy = glms_mat2_mulv( ytransf_max, cy );

        vec4s aabb{ minx.x / minx.y * game_camera.camera.projection.m00, miny.x / miny.y * game_camera.camera.projection.m11,
                    maxx.x / maxx.y * game_camera.camera.projection.m00, maxy.x / maxy.y * game_camera.camera.projection.m11 };


        //if ( tx_camera_inside ) {
        //    //aabb = { -1,-1,1,1 };
        //    aabb.x = -1;
        //    aabb.z = 1;
        //}

        //if ( ty_camera_inside ) {
        //    //aabb = { -1,-1,1,1 };
        //    aabb.y = -1;
        //    aabb.w = 1;
        //}
        // TODO:
        if ( context.use_mcguire_method ) {
            vec3s left, right, top, bottom;
            get_bounds_for_axis( vec3s{ 1, 0, 0 }, { view_space_pos.x, view_space_pos.y, view_space_pos.z }, radius, game_camera.camera.near_plane, left, right );
            get_bounds_for_axis( vec3s{ 0, 1, 0 }, { view_space_pos.x, view_space_pos.y, view_space_pos.z }, radius, game_camera.camera.near_plane, top, bottom );

            left = project( game_camera.camera.projection, left );
            right = project( game_camera.camera.projection, right );
            top = project( game_camera.camera.projection, top );
            bottom = project( game_camera.camera.projection, bottom );

            aabb.x = right.x;
            aabb.z = left.x;
            aabb.y = -top.y;
            aabb.w = -bottom.y;
        }

        if ( context.use_view_aabb ) {
            // Build view space AABB and project it, then calculate screen AABB
            vec3s aabb_min{ FLT_MAX, FLT_MAX ,FLT_MAX }, aabb_max{ -FLT_MAX ,-FLT_MAX ,-FLT_MAX };

            for ( u32 c = 0; c < 8; ++c ) {
                vec3s corner{ ( c % 2 ) ? 1.f : -1.f, ( c & 2 ) ? 1.f : -1.f, ( c & 4 ) ? 1.f : -1.f };
                corner = glms_vec3_scale( corner, radius );
                corner = glms_vec3_add( corner, glms_vec3( pos ) );

                // transform in view space
                vec4s corner_vs = glms_mat4_mulv( game_camera.camera.view, glms_vec4( corner, 1.f ) );
                // adjust z on the near plane.
                // visible Z is negative, thus corner vs will be always negative, but near is positive.
                // get positive Z and invert ad the end.
                corner_vs.z = glm_max( game_camera.camera.near_plane, corner_vs.z );

                vec4s corner_ndc = glms_mat4_mulv( game_camera.camera.projection, corner_vs );
                corner_ndc = glms_vec4_divs( corner_ndc, corner_ndc.w );

                // clamp
                aabb_min.x = glm_min( aabb_min.x, corner_ndc.x );
                aabb_min.y = glm_min( aabb_min.y, corner_ndc.y );

                aabb_max.x = glm_max( aabb_max.x, corner_ndc.x );
                aabb_max.y = glm_max( aabb_max.y, corner_ndc.y );
            }

            aabb.x = aabb_min.x;
            aabb.z = aabb_max.x;
            // Inverted Y aabb
            aabb.w = -1 * aabb_min.y;
            aabb.y = -1 * aabb_max.y;
        }

        const f32 position_len = glms_vec3_norm( { view_space_pos.x, view_space_pos.y, view_space_pos.z } );
        const bool camera_inside = ( position_len - radius ) < game_camera.camera.near_plane;

        if ( camera_inside && context.enable_camera_inside ) {
            aabb = { -1,-1, 1, 1 };
        }

        if ( context.force_fullscreen_light_aabb ) {
            aabb = { -1,-1, 1, 1 };
        }

        // NOTE(marco): xy = top-left, zw = bottom-right
        vec4s aabb_screen{ ( aabb.x * 0.5f + 0.5f ) * ( gpu.swapchain_width - 1 ),
                           ( aabb.y * 0.5f + 0.5f ) * ( gpu.swapchain_height - 1 ),
                           ( aabb.z * 0.5f + 0.5f ) * ( gpu.swapchain_width - 1 ),
                           ( aabb.w * 0.5f + 0.5f ) * ( gpu.swapchain_height - 1 ) };

        f32 width = aabb_screen.z - aabb_screen.x;
        f32 height = aabb_screen.w - aabb_screen.y;

        if ( width < 0.0001f || height < 0.0001f ) {
            continue;
        }

        float min_x = aabb_screen.x;
        float min_y = aabb_screen.y;

        float max_x = min_x + width;
        float max_y = min_y + height;

        if ( min_x > gpu.swapchain_width || min_y > gpu.swapchain_height ) {
            continue;
        }

        if ( max_x < 0.0f || max_y < 0.0f ) {
            continue;
        }

        min_x = max( min_x, 0.0f );
        min_y = max( min_y, 0.0f );

        max_x = min( max_x, ( float )gpu.swapchain_width );
        max_y = min( max_y, ( float )gpu.swapchain_height );

        u32 first_tile_x = ( u32 )( min_x * tile_size_inv );
        u32 last_tile_x = min( tile_x_count - 1, ( u32 )( max_x * tile_size_inv ) );

        u32 first_tile_y = ( u32 )( min_y * tile_size_inv );
        u32 last_tile_y = min( tile_y_count - 1, ( u32 )( max_y * tile_size_inv ) );

        for ( u32 y = first_tile_y; y <= last_tile_y; ++y ) {
            for ( u32 x = first_tile_x; x <= last_tile_x; ++x ) {
                u32 array_index = y * tile_stride + x;

                u32 word_index = i / 32;
                u32 bit_index = i % 32;

                light_tiles_bits[ array_index + word_index ] |= ( 1 << bit_index );
            }
        }
    }

    MapBufferParameters light_tiles_cb_map = { lights_tiles_sb[ gpu.current_frame ], 0, 0 };
    u32* light_tiles_data = ( u32* )gpu.map_buffer( light_tiles_cb_map );
    if ( light_tiles_data ) {
        memcpy( light_tiles_data, light_tiles_bits.data, light_tiles_bits.size * sizeof( u32 ) );

        gpu.unmap_buffer( light_tiles_cb_map );
    }

    context.scratch_allocator->free_marker( current_marker );
}

void RenderScene::draw_mesh_instance( CommandBuffer* gpu_commands, MeshInstance& mesh_instance ) {

    Mesh& mesh = *mesh_instance.mesh;
    BufferHandle buffers[]{ mesh.position_buffer, mesh.tangent_buffer, mesh.normal_buffer, mesh.texcoord_buffer, mesh.joints_buffer, mesh.weights_buffer };
    u32 offsets[]{ mesh.position_offset, mesh.tangent_offset, mesh.normal_offset, mesh.texcoord_offset, mesh.joints_offset, mesh.weights_offset };
    gpu_commands->bind_vertex_buffers( buffers, 0, mesh.skin_index != i32_max ? 6 : 4, offsets );

    gpu_commands->bind_index_buffer( mesh.index_buffer, mesh.index_offset, mesh.index_type );

    if ( recreate_per_thread_descriptors ) {
        DescriptorSetCreation ds_creation{};
        ds_creation.buffer( scene_cb, 0 ).buffer( mesh_instances_sb, 10 ).buffer( meshes_sb, 2 );
        DescriptorSetHandle descriptor_set = renderer->create_descriptor_set( gpu_commands, mesh.pbr_material.material, ds_creation );

        gpu_commands->bind_local_descriptor_set( &descriptor_set, 1, nullptr, 0 );
    } else {
        gpu_commands->bind_descriptor_set( &mesh.pbr_material.descriptor_set, 1, nullptr, 0 );
    }

    // Gpu mesh index used to retrieve mesh data
    gpu_commands->draw_indexed( TopologyType::Triangle, mesh.primitive_count, 1, 0, 0, mesh_instance.gpu_mesh_instance_index );
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
    gpu_commands->draw( TopologyType::Triangle, 0, 3, texture->resource_info.texture.handle.index, 1 );

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
    frame_graph->builder->register_render_pass( "gbuffer_pass_early", &gbuffer_pass_early );
    frame_graph->builder->register_render_pass( "gbuffer_pass_late", &gbuffer_pass_late );
    frame_graph->builder->register_render_pass( "lighting_pass", &light_pass );
    frame_graph->builder->register_render_pass( "transparent_pass", &transparent_pass );
    frame_graph->builder->register_render_pass( "depth_of_field_pass", &dof_pass );
    frame_graph->builder->register_render_pass( "debug_pass", &debug_pass );
    frame_graph->builder->register_render_pass( "mesh_pass", &mesh_pass );
    frame_graph->builder->register_render_pass( "mesh_occlusion_early_pass", &mesh_occlusion_early_pass );
    frame_graph->builder->register_render_pass( "mesh_occlusion_late_pass", &mesh_occlusion_late_pass );
    frame_graph->builder->register_render_pass( "depth_pyramid_pass", &depth_pyramid_pass );
}

void FrameRenderer::shutdown() {
    depth_pre_pass.free_gpu_resources();
    gbuffer_pass_early.free_gpu_resources();
    gbuffer_pass_late.free_gpu_resources();
    light_pass.free_gpu_resources();
    transparent_pass.free_gpu_resources();
    dof_pass.free_gpu_resources();
    debug_pass.free_gpu_resources();
    mesh_occlusion_early_pass.free_gpu_resources();
    mesh_occlusion_late_pass.free_gpu_resources();
    depth_pyramid_pass.free_gpu_resources();

    renderer->gpu->destroy_descriptor_set( fullscreen_ds );
}

void FrameRenderer::upload_gpu_data( UploadGpuDataContext& context ) {
    light_pass.upload_gpu_data( *scene );
    dof_pass.upload_gpu_data();

    scene->upload_gpu_data( context );

    // TODO: move this
    mesh_occlusion_early_pass.depth_pyramid_texture_index = depth_pyramid_pass.depth_pyramid.index;
    mesh_occlusion_late_pass.depth_pyramid_texture_index = depth_pyramid_pass.depth_pyramid.index;
}

void FrameRenderer::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {
}

void FrameRenderer::prepare_draws( StackAllocator* scratch_allocator ) {

    scene->prepare_draws( renderer, scratch_allocator, scene_graph );

    depth_pre_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    gbuffer_pass_early.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    gbuffer_pass_late.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    light_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    transparent_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    dof_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    debug_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    mesh_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    mesh_occlusion_early_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    mesh_occlusion_late_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    depth_pyramid_pass.prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );

    // Handle fullscreen pass.
    fullscreen_tech = renderer->resource_cache.techniques.get( hash_calculate( "fullscreen" ) );

    DescriptorSetCreation dsc;
    DescriptorSetLayoutHandle descriptor_set_layout = renderer->gpu->get_descriptor_set_layout( fullscreen_tech->passes[ 0 ].pipeline, k_material_descriptor_set_index );
    dsc.reset().buffer( scene->scene_cb, 0 ).set_layout( descriptor_set_layout );
    fullscreen_ds = renderer->gpu->create_descriptor_set( dsc );
}

// Transform /////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////

// CGLM converted method from:
// 2D Polyhedral Bounds of a Clipped, Perspective - Projected 3D Sphere
// By Michael Mara Morgan McGuire
void get_bounds_for_axis( const vec3s& a, // Bounding axis (camera space)
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
    float k = sqrt( ( r * r ) - ( ( nearZ - c.y ) * ( nearZ - c.y ) ) );
    for ( int i = 0; i < 2; ++i ) {
        if ( !cameraInsideSphere ) {
            mat2s transform{ v.x, -v.y,
                              v.y, v.x };

            bounds[ i ] = glms_mat2_mulv( transform, glms_vec2_scale( c, v.x ) );
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

    return vec3s{ v.x, v.y, v.z };
}
} // namespace raptor
