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

#include "external/cglm/struct/vec2.h"
#include "external/cglm/struct/mat2.h"
#include "external/cglm/struct/mat3.h"
#include "external/cglm/struct/cam.h"
#include "external/cglm/struct/euler.h"

#include "external/stb_image.h"
#include "external/tracy/tracy/Tracy.hpp"

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define DEBUG_DRAW_MESHLET_SPHERES 0
#define DEBUG_DRAW_MESHLET_CONES 0
#define DEBUG_DRAW_POINT_LIGHT_SPHERES 0
#define DEBUG_DRAW_REFLECTION_PROBES 1

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
static void copy_gpu_material_data( GpuDevice& gpu, GpuMaterialData& gpu_mesh_data, const Mesh& mesh ) {
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

    gpu_mesh_data.position_buffer = gpu.get_buffer_device_address( mesh.position_buffer ) + mesh.position_offset;
    gpu_mesh_data.uv_buffer = gpu.get_buffer_device_address( mesh.texcoord_buffer ) + mesh.texcoord_offset;
    gpu_mesh_data.index_buffer = gpu.get_buffer_device_address( mesh.index_buffer ) + mesh.index_offset;
    gpu_mesh_data.normals_buffer = gpu.get_buffer_device_address( mesh.normal_buffer ) + mesh.normal_offset;
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

        gpu_commands->draw_mesh_task_indirect_count( render_scene->mesh_task_indirect_early_commands_sb[ current_frame_index ], offsetof( GpuMeshDrawCommand, indirectMS ), render_scene->mesh_task_indirect_early_commands_sb[ current_frame_index ], 0, render_scene->mesh_instances.size, sizeof( GpuMeshDrawCommand ) );
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

            render_scene->draw_mesh_instance( gpu_commands, *mesh_instance_draw.mesh_instance, false );
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

void DepthPrePass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled )
        return;

    mesh_instance_draws.shutdown();
}

//
// DepthPrePass ///////////////////////////////////////////////////////
void DepthPyramidPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    update_depth_pyramid = ( render_scene->scene_data.freeze_occlusion_camera() == 0 );
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

void DepthPyramidPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled )
        return;

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
    depth_pyramid_view_creation.set_view_type( VK_IMAGE_VIEW_TYPE_2D ).set_parent_texture( depth_pyramid ).set_name( "depth_pyramid_view" );

    DescriptorSetCreation descriptor_set_creation{ };

    GpuTechnique* culling_technique = renderer->resource_cache.techniques.get( hash_calculate( "culling" ) );
    depth_pyramid_pipeline = culling_technique->passes[ 1 ].pipeline;
    DescriptorSetLayoutHandle depth_pyramid_layout = gpu.get_descriptor_set_layout( depth_pyramid_pipeline, k_material_descriptor_set_index );

    for ( u32 i = 0; i < depth_pyramid_levels; ++i ) {
        depth_pyramid_view_creation.sub_resource.mip_base_level = i;

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

        gpu_commands->issue_buffer_barrier( meshlet_index_buffer, RESOURCE_STATE_INDEX_BUFFER, RESOURCE_STATE_UNORDERED_ACCESS, QueueType::Graphics, QueueType::Compute );

        gpu_commands->bind_pipeline( generate_meshlet_index_buffer_pipeline );
        gpu_commands->bind_descriptor_set( &generate_meshlet_index_buffer_descriptor_set[ current_frame_index ], 1, nullptr, 0 );
        gpu_commands->dispatch_indirect( generate_meshlet_dispatch_indirect_buffer[ current_frame_index ], offsetof( GpuMeshDrawCounts, dispatch_task_x ) );

        gpu_commands->issue_buffer_barrier( meshlet_index_buffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_INDEX_BUFFER, QueueType::Compute, QueueType::Graphics );

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

        gpu_commands->draw_mesh_task_indirect_count( render_scene->mesh_task_indirect_early_commands_sb[ current_frame_index ], offsetof( GpuMeshDrawCommand, indirectMS ), render_scene->mesh_task_indirect_count_early_sb[ current_frame_index ], 0, render_scene->mesh_instances.size, sizeof( GpuMeshDrawCommand ) );
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

            render_scene->draw_mesh_instance( gpu_commands, *mesh_instance_draw.mesh_instance, false );
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
    GpuTechniquePass& generate_ib_pass = meshlet_technique->passes[ technique_index ];
    generate_meshlet_index_buffer_pipeline = generate_ib_pass.pipeline;

    technique_index = meshlet_technique->get_pass_index( "generate_meshlet_instances" );
    GpuTechniquePass& generate_inst_pass = meshlet_technique->passes[ technique_index ];
    generate_meshlets_instances_pipeline = generate_inst_pass.pipeline;

    technique_index = meshlet_technique->get_pass_index( "meshlet_instance_culling" );
    GpuTechniquePass& inst_cull_pass = meshlet_technique->passes[ technique_index ];
    meshlet_instance_culling_pipeline = inst_cull_pass.pipeline;

    technique_index = meshlet_technique->get_pass_index( "meshlet_write_counts" );
    meshlet_write_counts_pipeline = meshlet_technique->passes[ technique_index ].pipeline;

    DescriptorSetLayoutHandle layout_generate_ib = renderer->gpu->get_descriptor_set_layout( generate_meshlet_index_buffer_pipeline, k_material_descriptor_set_index );
    DescriptorSetLayoutHandle layout_generate_instances = renderer->gpu->get_descriptor_set_layout( generate_meshlets_instances_pipeline, k_material_descriptor_set_index );
    DescriptorSetLayoutHandle layout_instance_culling = renderer->gpu->get_descriptor_set_layout( meshlet_instance_culling_pipeline, k_material_descriptor_set_index );

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        DescriptorSetCreation ds_creation{};
        ds_creation.set_layout( layout_generate_ib )
            .buffer( scene.mesh_task_indirect_early_commands_sb[ i ], 6 ).buffer( scene.mesh_task_indirect_count_early_sb[ i ], 7 )
            .buffer( scene.meshlets_index_buffer_sb[ i ], 8 ).buffer( scene.meshlets_instances_sb[ i ], 9 ).buffer( scene.meshlets_visible_instances_sb[ i ], 19 );
        scene.add_scene_descriptors( ds_creation, generate_ib_pass );
        scene.add_mesh_descriptors( ds_creation, generate_ib_pass );
        scene.add_meshlet_descriptors( ds_creation, generate_ib_pass );
        generate_meshlet_index_buffer_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );

        ds_creation.reset().set_layout( layout_generate_instances )
            .buffer( scene.mesh_task_indirect_early_commands_sb[ i ], 6 ).buffer( scene.mesh_task_indirect_count_early_sb[ i ], 7 )
            .buffer( scene.meshlets_index_buffer_sb[ i ], 8 ).buffer( scene.meshlets_instances_sb[ i ], 9 ).buffer( scene.meshlet_instances_indirect_count_sb[ i ], 17 );
        scene.add_scene_descriptors( ds_creation, generate_inst_pass );
        scene.add_mesh_descriptors( ds_creation, generate_inst_pass );
        scene.add_meshlet_descriptors( ds_creation, generate_inst_pass );
        generate_meshlets_instances_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );

        BufferCreation buffer_creation;
        buffer_creation.reset().set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( u32 ) * 4 ).set_name( "meshlet_instance_culling_indirect_buffer" );
        meshlet_instance_culling_indirect_buffer[ i ] = renderer->gpu->create_buffer( buffer_creation );

        ds_creation.reset().set_layout( layout_instance_culling )
            .buffer( scene.meshlets_instances_sb[ i ], 9 ).buffer( scene.meshlets_visible_instances_sb[ i ], 19 )
            .buffer( scene.mesh_task_indirect_count_early_sb[ i ], 7 )
            .buffer( scene.mesh_task_indirect_early_commands_sb[ i ], 6 ).buffer( meshlet_instance_culling_indirect_buffer[ i ], 17 );

        scene.add_scene_descriptors( ds_creation, inst_cull_pass );
        scene.add_mesh_descriptors( ds_creation, inst_cull_pass );
        scene.add_meshlet_descriptors( ds_creation, inst_cull_pass );

        meshlet_instance_culling_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );

        // Cache indirect buffer
        generate_meshlet_dispatch_indirect_buffer[ i ] = scene.mesh_task_indirect_count_early_sb[ i ];
    }
}

void GBufferPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled )
        return;

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

void LateGBufferPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled )
        return;

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

        gpu_commands->draw_mesh_task_indirect_count( render_scene->mesh_task_indirect_late_commands_sb[ current_frame_index ], offsetof(GpuMeshDrawCommand, indirectMS), render_scene->mesh_task_indirect_count_late_sb[ current_frame_index ], 0, render_scene->mesh_instances.size, sizeof(GpuMeshDrawCommand) );
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
void LightPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {
    if ( !enabled )
        return;

    FrameGraphResource* resource = frame_graph->get_resource( "shading_rate_image" );
    if ( resource ) {
        u32 adjusted_width = ( new_width + gpu.min_fragment_shading_rate_texel_size.width - 1 ) / gpu.min_fragment_shading_rate_texel_size.width;
        u32 adjusted_height = ( new_height + gpu.min_fragment_shading_rate_texel_size.height - 1 ) / gpu.min_fragment_shading_rate_texel_size.height;
        gpu.resize_texture( resource->resource_info.texture.handle, adjusted_width, adjusted_height );

        resource->resource_info.texture.width = adjusted_width;
        resource->resource_info.texture.height = adjusted_height;
    }
}

void LightPass::post_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {

    if ( gpu_commands->gpu_device->fragment_shading_rate_present && !use_compute ) {
        Texture* attachment_texture = renderer->gpu->access_texture( output_texture->resource_info.texture.handle );
        Texture* frs_texture = renderer->gpu->access_texture( render_scene->fragment_shading_rate_image );

        util_add_image_barrier( renderer->gpu, gpu_commands->vk_command_buffer, attachment_texture,
                                RESOURCE_STATE_SHADER_RESOURCE, 0, 1, false );

        util_add_image_barrier( renderer->gpu, gpu_commands->vk_command_buffer, frs_texture,
                                RESOURCE_STATE_UNORDERED_ACCESS, 0, 1, false );

        u32 filter_size = 16;
        u32 workgroup_x = ( attachment_texture->width + ( filter_size - 1 ) ) / filter_size;
        u32 workgroup_y = ( attachment_texture->height + ( filter_size - 1 ) ) / filter_size;

        PipelineHandle pipeline = renderer->get_pipeline( mesh.pbr_material.material, 2 );
        gpu_commands->bind_pipeline( pipeline );
        gpu_commands->bind_descriptor_set( &fragment_rate_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

        gpu_commands->dispatch( workgroup_x, workgroup_y, 1 );

        util_add_image_barrier( renderer->gpu, gpu_commands->vk_command_buffer, frs_texture,
                                RESOURCE_STATE_SHADING_RATE_SOURCE, 0, 1, false );
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

    // Create debug texture
    TextureCreation texture_creation;
    texture_creation.set_size( 1280, 800, 1 ).set_layers( 1 ).set_mips( 1 ).set_format_type( VK_FORMAT_R16G16B16A16_SFLOAT, TextureType::Texture2D )
        .set_flags( TextureFlags::RenderTarget_mask | TextureFlags::Compute_mask ).set_name( "lighting_debug_texture" );

    lighting_debug_texture = renderer->gpu->create_texture( texture_creation );
    scene.lighting_debug_texture_index = lighting_debug_texture.index;

    for ( u32 f = 0; f < k_max_frames; ++f ) {
        fragment_rate_descriptor_set[ f ].index = k_invalid_index;
        fragment_rate_texture_index[ f ].index = k_invalid_index;
    }

    if ( renderer->gpu->fragment_shading_rate_present && !use_compute ) {
        Texture* colour_texture = renderer->gpu->access_texture( color_texture->resource_info.texture.handle );

        u32 frs_pass_index = main_technique->get_pass_index( "edge_detection" );
        GpuTechniquePass& pass = main_technique->passes[ frs_pass_index ];

        BufferCreation buffer_creation{ };
        buffer_creation.set_name( "fragment_rate_texture_index" ).set(  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( u32 ) * 2 );

        for ( u32 f = 0; f < k_max_frames; ++f ) {
            fragment_rate_texture_index[ f ] = renderer->gpu->create_buffer( buffer_creation );

            DescriptorSetHandle ds_handle = fragment_rate_descriptor_set[ f ];

            renderer->gpu->destroy_descriptor_set( ds_handle );

            DescriptorSetLayoutHandle frs_layout = renderer->gpu->get_descriptor_set_layout( pass.pipeline, k_material_descriptor_set_index );

            DescriptorSetCreation ds_creation{ };
            ds_creation.set_layout( frs_layout );
            scene.add_scene_descriptors( ds_creation, pass );
            ds_creation.buffer( mesh.pbr_material.material_buffer, 1 );
            ds_creation.buffer( fragment_rate_texture_index[ f ], 2 );

            fragment_rate_descriptor_set[ f ] = renderer->gpu->create_descriptor_set( ds_creation );
        }
    }

    {
        scene.renderer->gpu->destroy_descriptor_set( mesh.pbr_material.descriptor_set_transparent );

        const u32 pass_index = use_compute ? main_technique->get_pass_index("deferred_lighting_compute") : main_technique->get_pass_index( "deferred_lighting_pixel");
        DescriptorSetCreation ds_creation{};
        GpuTechniquePass& pass = main_technique->passes[ pass_index ];
        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( pass.pipeline, k_material_descriptor_set_index );

        for ( u32 i = 0; i < k_max_frames; ++i ) {
            scene.renderer->gpu->destroy_descriptor_set( lighting_descriptor_set[ i ] );

            // Legacy non-compute descriptor set.
            ds_creation.reset().set_layout( layout );

            scene.add_lighting_descriptors( ds_creation, pass, i );
            ds_creation.buffer( mesh.pbr_material.material_buffer, 1 );
            scene.add_scene_descriptors( ds_creation, pass );

            lighting_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
        }
    }
}

void LightPass::upload_gpu_data( RenderScene& scene ) {
    if ( !enabled )
        return;

    u32 current_frame_index = renderer->gpu->current_frame;

    MapBufferParameters cb_map = { mesh.pbr_material.material_buffer, 0, 0 };
    LightingConstants* lighting_data = ( LightingConstants* )renderer->gpu->map_buffer( cb_map );
    if ( lighting_data ) {
        lighting_data->albedo_index = color_texture->resource_info.texture.handle.index;
        lighting_data->rmo_index = roughness_texture->resource_info.texture.handle.index;
        lighting_data->normal_index = normal_texture->resource_info.texture.handle.index;
        lighting_data->depth_index = depth_texture->resource_info.texture.handle.index;
        lighting_data->output_index = output_texture->resource_info.texture.handle.index;
        lighting_data->output_width = renderer->width;
        lighting_data->output_height = renderer->height;
        lighting_data->emissive = emissive_texture->resource_info.texture.handle.index;

        renderer->gpu->unmap_buffer( cb_map );
    }

    if ( renderer->gpu->fragment_shading_rate_present ) {

        for ( u32 f = 0; f < k_max_frames; ++f ) {

            cb_map = { fragment_rate_texture_index[ f ], 0, 0 };
            u32* frs_texture_indices = ( u32* )renderer->gpu->map_buffer( cb_map );

            if ( frs_texture_indices != nullptr ) {
                frs_texture_indices[ 0 ] = output_texture->resource_info.texture.handle.index;
                frs_texture_indices[ 1 ] = scene.fragment_shading_rate_image.index;

                renderer->gpu->unmap_buffer( cb_map );
            }
        }
    }
}

void LightPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled )
        return;

    gpu.destroy_buffer( mesh.pbr_material.material_buffer );
    gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set_transparent );
    gpu.destroy_texture( lighting_debug_texture );

    for ( u32 f = 0; f < k_max_frames; ++f ) {
        gpu.destroy_buffer( fragment_rate_texture_index[ f ] );
        gpu.destroy_descriptor_set( fragment_rate_descriptor_set[ f ] );
        gpu.destroy_descriptor_set( lighting_descriptor_set[ f ] );
    }

    // TODO(marco): destroy scene.fragment_shading_rate_image
}

void LightPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {

    if ( !enabled )
        return;

    const u64 hashed_name = hash_calculate( "pbr_lighting" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    {
        render_scene->renderer->gpu->destroy_descriptor_set( mesh.pbr_material.descriptor_set_transparent );

        const u32 pass_index = use_compute ? 1 : 0;
        DescriptorSetCreation ds_creation{};
        GpuTechniquePass& pass = main_technique->passes[ pass_index ];
        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( pass.pipeline, k_material_descriptor_set_index );

        for ( u32 i = 0; i < k_max_frames; ++i ) {
            render_scene->renderer->gpu->destroy_descriptor_set( lighting_descriptor_set[ i ] );

            // Legacy non-compute descriptor set.
            ds_creation.reset().set_layout( layout );

            render_scene->add_lighting_descriptors( ds_creation, pass, i );
            ds_creation.buffer( mesh.pbr_material.material_buffer, 1 );
            render_scene->add_scene_descriptors( ds_creation, pass );

            lighting_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
        }
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

        gpu_commands->draw_mesh_task_indirect_count( render_scene->mesh_task_indirect_early_commands_sb[ current_frame_index ], indirect_commands_offset,
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

            render_scene->draw_mesh_instance( gpu_commands, *mesh_instance_draw.mesh_instance, true );
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

void TransparentPass::free_gpu_resources( GpuDevice& gpu ) {
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

#if ( DEBUG_DRAW_MESHLET_SPHERES || DEBUG_DRAW_POINT_LIGHT_SPHERES )
    gpu_commands->bind_pipeline( pipeline );
    gpu_commands->bind_vertex_buffer( sphere_mesh_buffer->handle, 0, 0 );
    gpu_commands->bind_index_buffer( sphere_mesh_indices->handle, 0, VK_INDEX_TYPE_UINT32 );

    gpu_commands->bind_descriptor_set( &sphere_mesh_descriptor_set, 1, nullptr, 0 );

    gpu_commands->draw_indexed_indirect( sphere_draw_indirect_buffer->handle, bounding_sphere_count, 0, sizeof( VkDrawIndexedIndirectCommand ) );
#endif

#if DEBUG_DRAW_MESHLET_CONES
    gpu_commands->bind_pipeline( pipeline );
    gpu_commands->bind_vertex_buffer( cone_mesh_buffer->handle, 0, 0 );
    gpu_commands->bind_index_buffer( cone_mesh_indices->handle, 0, VK_INDEX_TYPE_UINT32 );

    gpu_commands->bind_descriptor_set( &cone_mesh_descriptor_set, 1, nullptr, 0 );

    gpu_commands->draw_indexed_indirect( cone_draw_indirect_buffer->handle, bounding_sphere_count, 0, sizeof( VkDrawIndexedIndirectCommand ) );
#endif

    // Draw GI debug probe spheres
    if ( render_scene->gi_show_probes ) {
        gpu_commands->bind_pipeline( gi_debug_probes_pipeline );
        gpu_commands->bind_vertex_buffer( sphere_mesh_buffer->handle, 0, 0 );
        gpu_commands->bind_index_buffer( sphere_mesh_indices->handle, 0, VK_INDEX_TYPE_UINT32 );

        gpu_commands->bind_descriptor_set( &gi_debug_probes_descriptor_set, 1, nullptr, 0 );

        // TODO: draw only one sphere
        gpu_commands->draw_indexed( TopologyType::Triangle, sphere_index_count, render_scene->gi_total_probes, 0, 0, 0 );
    }

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

    // Draw cpu debug rendering
    render_scene->debug_renderer.render( current_frame_index, gpu_commands, render_scene );

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

    const u64 hashed_name = hash_calculate( "debug" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;

    material_creation.set_name( "material_debug" ).set_technique( main_technique ).set_render_index( 0 );
    debug_material = renderer->create_material( material_creation );

    sizet marker = scratch_allocator->get_marker();

    StringBuffer mesh_name;
    mesh_name.init( 1024, scratch_allocator );
    cstring filename = mesh_name.append_use_f( "%s/sphere.obj", RAPTOR_DATA_FOLDER );

#if ( DEBUG_DRAW_MESHLET_SPHERES || DEBUG_DRAW_POINT_LIGHT_SPHERES || DEBUG_DRAW_REFLECTION_PROBES)
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

#if (DEBUG_DRAW_MESHLET_SPHERES)
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
#endif // DEBUG_DRAW_MESHLET_SPHERES

#if (DEBUG_DRAW_MESHLET_CONES)
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
#endif // DEBUG_DRAW_MESHLET_CONES

#if DEBUG_DRAW_POINT_LIGHT_SPHERES
    for ( u32 i = 0; i < scene.active_lights; ++i ) {
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
#endif // DEBUG_DRAW_POINT_LIGHT_SPHERES

    bounding_matrices.shutdown();
    sphere_indirect_commands.shutdown();

    // Prepare gpu debug line resources
    {
        DescriptorSetCreation descriptor_set_creation{ };

        // Finalize pass
        u32 pass_index = main_technique->get_pass_index( "commands_finalize" );
        GpuTechniquePass& pass = main_technique->passes[ pass_index ];
        debug_lines_finalize_pipeline = pass.pipeline;
        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( pass.pipeline, k_material_descriptor_set_index );

        DescriptorSetCreation set_creation{ };
        set_creation.set_layout( layout );
        scene.add_scene_descriptors( set_creation, pass );
        scene.add_debug_descriptors( set_creation, pass );
        debug_lines_finalize_set = renderer->gpu->create_descriptor_set( set_creation );

        // Draw pass
        pass_index = main_technique->get_pass_index( "debug_line_gpu" );
        GpuTechniquePass& line_gpu_pass = main_technique->passes[ pass_index ];
        debug_lines_draw_pipeline = main_technique->passes[ pass_index ].pipeline;
        layout = renderer->gpu->get_descriptor_set_layout( line_gpu_pass.pipeline, k_material_descriptor_set_index );

        set_creation.reset().set_layout( layout );
        scene.add_scene_descriptors( set_creation, line_gpu_pass );
        scene.add_debug_descriptors( set_creation, line_gpu_pass );
        debug_lines_draw_set = renderer->gpu->create_descriptor_set( set_creation );

        pass_index = main_technique->get_pass_index( "debug_line_2d_gpu" );
        GpuTechniquePass& line_2d_gpu_pass = main_technique->passes[ pass_index ];
        debug_lines_2d_draw_pipeline = line_2d_gpu_pass.pipeline;

        debug_line_commands_sb_cache = scene.debug_line_commands_sb;
    }
}

void DebugPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled )
        return;

#if ( DEBUG_DRAW_MESHLET_SPHERES || DEBUG_DRAW_POINT_LIGHT_SPHERES || DEBUG_DRAW_REFLECTION_PROBES )
    renderer->destroy_buffer( sphere_mesh_indices );
    renderer->destroy_buffer( sphere_mesh_buffer );
#endif

#if ( DEBUG_DRAW_MESHLET_SPHERES || DEBUG_DRAW_POINT_LIGHT_SPHERES )
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

    renderer->gpu->destroy_descriptor_set( gi_debug_probes_descriptor_set );
    renderer->gpu->destroy_descriptor_set( debug_lines_finalize_set );
    renderer->gpu->destroy_descriptor_set( debug_lines_draw_set );
}

void DebugPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "ddgi" ) );
    if ( technique ) {
        gpu.destroy_descriptor_set( gi_debug_probes_descriptor_set );

        // Probe raytracing
        u32 pass_index = technique->get_pass_index( "debug_mesh" );
        GpuTechniquePass& pass = technique->passes[ pass_index ];

        gi_debug_probes_pipeline = pass.pipeline;

        DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( gi_debug_probes_pipeline, k_material_descriptor_set_index );
        DescriptorSetCreation ds_creation{};
        ds_creation.reset().set_layout( layout ).buffer( render_scene->ddgi_constants_cache, 55 ).buffer( render_scene->ddgi_probe_status_cache, 43 );
        render_scene->add_scene_descriptors( ds_creation, pass );

        gi_debug_probes_descriptor_set = gpu.create_descriptor_set( ds_creation );
    }
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
    gpu_commands->bind_descriptor_set( &mesh.pbr_material.descriptor_set_transparent, 1, nullptr, 0 );

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
    mesh.pbr_material.descriptor_set_transparent = renderer->gpu->create_descriptor_set( ds_creation );

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

void DoFPass::upload_gpu_data( RenderScene& scene ) {
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

void DoFPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled )
        return;

    renderer->destroy_texture( scene_mips );

    gpu.destroy_buffer( mesh.pbr_material.material_buffer );
    gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set_transparent );
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
        GpuTechniquePass& pass = culling_technique->passes[ pipeline_index ];
        frustum_cull_pipeline = pass.pipeline;
        DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( frustum_cull_pipeline, k_material_descriptor_set_index );

        for ( u32 i = 0; i < k_max_frames; ++i ) {
            DescriptorSetCreation ds_creation{};
            ds_creation.buffer( scene.mesh_task_indirect_count_early_sb[ i ], 11 ).buffer( scene.mesh_task_indirect_count_early_sb[ i ], 13 )
                .buffer(scene.mesh_task_indirect_early_commands_sb[ i ], 1 ).buffer(scene.mesh_task_indirect_culled_commands_sb[ i ], 3 )
                .set_layout(layout);

            scene.add_scene_descriptors( ds_creation, pass );
            scene.add_debug_descriptors( ds_creation, pass );
            scene.add_mesh_descriptors( ds_creation, pass );

            frustum_cull_descriptor_set[ i ] = gpu.create_descriptor_set(ds_creation);
        }
    }
}

void CullingEarlyPass::free_gpu_resources( GpuDevice& gpu ) {

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
        GpuTechniquePass& pass = culling_technique->passes[ 0 ];
        frustum_cull_pipeline = pass.pipeline;
        DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( frustum_cull_pipeline, k_material_descriptor_set_index );

        for ( u32 i = 0; i < k_max_frames; ++i ) {
            DescriptorSetCreation ds_creation{};
            ds_creation.buffer( scene.meshes_sb, 2 ).buffer( scene.mesh_instances_sb, 10 ).buffer( scene.scene_cb, 0 )
                .buffer( scene.mesh_task_indirect_count_late_sb[ i ], 11 ).buffer( scene.mesh_task_indirect_count_early_sb[ i ], 13 ).buffer(scene.mesh_task_indirect_late_commands_sb[ i ], 1 ).buffer(scene.mesh_task_indirect_culled_commands_sb[ i ], 3 )
                .buffer( scene.mesh_bounds_sb, 12 )
                .set_layout(layout);

                scene.add_debug_descriptors( ds_creation, pass );

            frustum_cull_descriptor_set[ i ] = gpu.create_descriptor_set(ds_creation);
        }
    }
}

void CullingLatePass::free_gpu_resources( GpuDevice& gpu ) {

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        gpu.destroy_descriptor_set( frustum_cull_descriptor_set[ i ]);
    }
}

// RayTracingTestPass ///////////////////////////////////////////////////
void RayTracingTestPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled ) {
        return;
    }

    Texture* texture = gpu_commands->gpu_device->access_texture( render_target );

    util_add_image_barrier( gpu_commands->gpu_device, gpu_commands->vk_command_buffer, texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1, false );

    gpu_commands->bind_pipeline( pipeline );

    gpu_commands->bind_descriptor_set( descriptor_set + current_frame_index, 1, nullptr, 0 );

    gpu_commands->trace_rays( pipeline, renderer->width, renderer->height, 1 );

    util_add_image_barrier( gpu_commands->gpu_device, gpu_commands->vk_command_buffer, texture, RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, 1, false );
}

void RayTracingTestPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {
    if ( !enabled ) {
        return;
    }

    if ( owns_render_target ) {
        gpu.resize_texture( render_target, new_width, new_height );
    }
}

void RayTracingTestPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    FrameGraphNode* node = frame_graph->get_node( "ray_tracing_test" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;

    renderer = scene.renderer;

    if ( !enabled ) {
        return;
    }

    GpuTechnique* ray_tracing_technique = renderer->resource_cache.techniques.get( hash_calculate( "ray_tracing" ) );
    pipeline = ray_tracing_technique->passes[ 0 ].pipeline;

    GpuDevice& gpu = *renderer->gpu;

    cstring rt_render_target = "final";

    FrameGraphResource* texture = frame_graph->get_resource( rt_render_target );
    RASSERT( texture != nullptr );

    if ( texture->resource_info.texture.handle.index == k_invalid_index ) {
        TextureCreation texture_creation{ };
        texture_creation.set_flags( TextureFlags::Compute_mask ).set_name( rt_render_target ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D ).set_size( gpu.swapchain_width, gpu.swapchain_height, 1 ).set_mips( 1 ).set_layers( 1 );

        render_target = gpu.create_texture( texture_creation );

        texture->resource_info.set_external_texture_2d( gpu.swapchain_width, gpu.swapchain_height, VK_FORMAT_R8_UINT, 0, render_target );

        owns_render_target = true;
    } else {
        render_target = texture->resource_info.texture.handle;

        owns_render_target = false;
    }

    DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( pipeline, k_material_descriptor_set_index );

    BufferCreation uniform_buffer_creation{ };
    uniform_buffer_creation.set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof ( GpuData ) ).set_name( "ray_tracing_uniform_buffer" );


    for ( u32 i = 0; i < k_max_frames; ++i ) {
        uniform_buffer[ i ] = gpu.create_buffer( uniform_buffer_creation );

        DescriptorSetCreation ds_creation{};
        ds_creation.buffer( scene.scene_cb, 0 ).set_as( scene.tlas, 1 ).buffer( scene.meshes_sb, 2 ).buffer( scene.mesh_instances_sb, 10 ).buffer( scene.mesh_bounds_sb, 12 ).buffer( uniform_buffer[ i ], 3 ).set_layout( layout );

        descriptor_set[ i ] = gpu.create_descriptor_set( ds_creation );
    }
}

void RayTracingTestPass::upload_gpu_data( RenderScene& scene ) {
    if ( !enabled ) {
        return;
    }

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        MapBufferParameters mb{ uniform_buffer[ i ], 0, 0 };

        GpuData* gpu_data = ( GpuData* )renderer->gpu->map_buffer( mb );

        if ( gpu_data ) {
            gpu_data->sbt_offset = 0; // shader binding table offset
            gpu_data->sbt_stride = renderer->gpu->ray_tracing_pipeline_properties.shaderGroupHandleAlignment; // shader binding table stride
            gpu_data->miss_index = 0;
            gpu_data->out_image_index = render_target.index;

            renderer->gpu->unmap_buffer( mb );
        }
    }
}

void RayTracingTestPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled ) {
        return;
    }

    if ( owns_render_target ) {
        gpu.destroy_texture( render_target );
    }

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        gpu.destroy_descriptor_set( descriptor_set[ i ] );
        gpu.destroy_buffer( uniform_buffer[ i ] );
    }
}

// ShadowVisbilityPass ///////////////////////////////////////////////////
void ShadowVisibilityPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled ) {
        return;
    }

    if ( render_scene->active_lights != last_active_lights_count ) {
        GpuDevice& gpu = *renderer->gpu;
        recreate_textures( gpu, render_scene->active_lights );

        FrameGraphResourceInfo resource_info{ };
        const u32 adjusted_width = ceilu32( gpu.swapchain_width * texture_scale );
        const u32 adjusted_height = ceilu32( gpu.swapchain_height * texture_scale );
        resource_info.set_external_texture_3d( adjusted_width, adjusted_height, render_scene->active_lights, VK_FORMAT_R16_SFLOAT, 0, filtered_visibility_texture );

        shadow_visibility_resource->resource_info = resource_info;
    }

    if ( clear_resources ) {
        VkClearColorValue clear_value{ };

        gpu_commands->clear_color_image( visibility_cache_texture, clear_value );

        gpu_commands->clear_color_image( variation_cache_texture, clear_value );

        gpu_commands->clear_color_image( variation_texture, clear_value );

        gpu_commands->clear_color_image( samples_count_cache_texture, clear_value );

        gpu_commands->clear_color_image( filtered_visibility_texture, clear_value );

        gpu_commands->clear_color_image( filtered_variation_texture, clear_value );

        clear_resources = false;
    }

    gpu_commands->issue_texture_barrier( visibility_cache_texture, ResourceState::RESOURCE_STATE_GENERIC_READ, 0, 1 );
    gpu_commands->issue_texture_barrier( variation_texture, ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

    // NOTE(marco): variance pass
    gpu_commands->bind_pipeline( variance_pipeline );

    gpu_commands->bind_descriptor_set( descriptor_set + current_frame_index, 1, 0, 0 );

    u32 x = ( ceilu32( gpu_commands->gpu_device->swapchain_width * texture_scale ) + 7 ) / 8;
    u32 y = ( ceilu32( gpu_commands->gpu_device->swapchain_height * texture_scale ) + 7 ) / 8;
    gpu_commands->dispatch( x, y, 1 );

    gpu_commands->issue_texture_barrier( variation_cache_texture, ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->issue_texture_barrier( samples_count_cache_texture, ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->issue_texture_barrier( filtered_variation_texture, ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->issue_texture_barrier( variation_texture, ResourceState::RESOURCE_STATE_GENERIC_READ, 0, 1 );

    // NOTE(marco): visiblity pass
    gpu_commands->bind_pipeline( visibility_pipeline );

    gpu_commands->dispatch( x, y, 1 );

    gpu_commands->issue_texture_barrier( visibility_cache_texture, ResourceState::RESOURCE_STATE_GENERIC_READ, 0, 1 );
    gpu_commands->issue_texture_barrier( filtered_variation_texture, ResourceState::RESOURCE_STATE_GENERIC_READ, 0, 1 );
    gpu_commands->issue_texture_barrier( filtered_visibility_texture, ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

    // NOTE(marco): visiblity filtering pass
    gpu_commands->bind_pipeline( visibility_filtering_pipeline );

    gpu_commands->dispatch( x, y, 1 );
}

void ShadowVisibilityPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {
    if ( !enabled ) {
        return;
    }

    const u32 adjusted_width = ceilu32( new_width * texture_scale );
    const u32 adjusted_height = ceilu32( new_height * texture_scale );

    gpu.resize_texture_3d( visibility_cache_texture, adjusted_width, adjusted_height, last_active_lights_count );
    gpu.resize_texture_3d( variation_cache_texture, adjusted_width, adjusted_height, last_active_lights_count );
    gpu.resize_texture_3d( variation_texture, adjusted_width, adjusted_height, last_active_lights_count );
    gpu.resize_texture_3d( filtered_visibility_texture, adjusted_width, adjusted_height, last_active_lights_count );
    gpu.resize_texture_3d( filtered_variation_texture, adjusted_width, adjusted_height, last_active_lights_count );
    gpu.resize_texture_3d( samples_count_cache_texture, adjusted_width, adjusted_height, last_active_lights_count );

    clear_resources = true;
}

void ShadowVisibilityPass::recreate_textures( GpuDevice& gpu, u32 lights_count ) {
    if ( last_active_lights_count != 0 ) {
        gpu.destroy_texture( visibility_cache_texture );
        gpu.destroy_texture( variation_cache_texture );
        gpu.destroy_texture( variation_texture );
        gpu.destroy_texture( samples_count_cache_texture );
        gpu.destroy_texture( filtered_visibility_texture );
        gpu.destroy_texture( filtered_variation_texture );
    }

    const u32 adjusted_width = ceilu32( gpu.swapchain_width * texture_scale );
    const u32 adjusted_height = ceilu32( gpu.swapchain_height * texture_scale );

    TextureCreation texture_creation{ };
    texture_creation.set_flags( TextureFlags::Compute_mask ).set_name( "visibility_cache" )
                    .set_format_type( VK_FORMAT_R16G16B16A16_SFLOAT, TextureType::Texture3D )
                    .set_size( adjusted_width, adjusted_height, lights_count )
                    .set_mips( 1 ).set_layers( 1 );

    // NOTE(marco): last 4 frames visibility values per light
    visibility_cache_texture = gpu.create_texture( texture_creation);

    // NOTE(marco): last 4 frames visibility variation per light
    texture_creation.set_name( "variation_cache" );
    variation_cache_texture = gpu.create_texture( texture_creation );

    // NOTE(marco): visibility delta
    texture_creation.set_name( "variation" ).set_format_type( VK_FORMAT_R16_SFLOAT, TextureType::Texture3D );
    variation_texture = gpu.create_texture( texture_creation );

    texture_creation.set_name( "filtered_visibility" );
    filtered_visibility_texture = gpu.create_texture( texture_creation );

    texture_creation.set_name( "filtered_variation" );
    filtered_variation_texture = gpu.create_texture( texture_creation );

    // NOTE(marco): last 4 frames samples count per light
    texture_creation.set_name( "samples_count_cache" ).set_format_type( VK_FORMAT_R8G8B8A8_UINT, TextureType::Texture3D );
    samples_count_cache_texture = gpu.create_texture( texture_creation );

    clear_resources = true;
    last_active_lights_count = lights_count;
}

void ShadowVisibilityPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    FrameGraphNode* node = frame_graph->get_node( "shadow_visibility_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;

    renderer = scene.renderer;

    GpuDevice& gpu = *renderer->gpu;

    // Use half resolution textures
    texture_scale = 0.5f;

    recreate_textures( gpu, scene.active_lights );

    cstring shadow_visibility_resource_name = "shadow_visibility";
    FrameGraphResourceInfo resource_info{ };

    const u32 adjusted_width = ceilu32( gpu.swapchain_width * texture_scale );
    const u32 adjusted_height = ceilu32( gpu.swapchain_height * texture_scale );
    resource_info.set_external_texture_3d( adjusted_width, adjusted_height, scene.active_lights, VK_FORMAT_R16_SFLOAT, 0, filtered_visibility_texture );

    shadow_visibility_resource = frame_graph->get_resource( shadow_visibility_resource_name );
    RASSERT( shadow_visibility_resource != nullptr );
    shadow_visibility_resource->resource_info = resource_info;

    BufferCreation buffer_creation{ };
    buffer_creation.set_name( "shadow_visiblity_constants" ).set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuShadowVisibilityConstants ) );

    gpu_pass_constants = gpu.create_buffer( buffer_creation );

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "pbr_lighting" ) );

    u32 pass_index = technique->get_pass_index( "shadow_visibility_variance" );
    GpuTechniquePass& variance_pass = technique->passes[ pass_index ];
    variance_pipeline = variance_pass.pipeline;

    pass_index = technique->get_pass_index( "shadow_visibility" );
    GpuTechniquePass& visiblity_pass = technique->passes[ pass_index ];
    visibility_pipeline = visiblity_pass.pipeline;

    pass_index = technique->get_pass_index( "shadow_visibility_filtering" );
    GpuTechniquePass& visiblity_filtering_pass = technique->passes[ pass_index ];
    visibility_filtering_pipeline = visiblity_filtering_pass.pipeline;

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        DescriptorSetCreation ds_creation{ };

        scene.add_scene_descriptors( ds_creation, variance_pass );
        scene.add_lighting_descriptors( ds_creation, variance_pass, i );
        ds_creation.buffer( gpu_pass_constants, 30 );

        ds_creation.set_layout( renderer->gpu->get_descriptor_set_layout( variance_pipeline, k_material_descriptor_set_index) );

        descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
    }

    FrameGraphResource* resource = frame_graph->get_resource( "gbuffer_normals" );
    RASSERT( resource != nullptr );
    normals_texture = resource->resource_info.texture.handle;
}

void ShadowVisibilityPass::upload_gpu_data( RenderScene& scene ) {
    if ( !enabled ) {
        return;
    }

    MapBufferParameters mb{ gpu_pass_constants, 0, 0 };
    GpuShadowVisibilityConstants* constants = ( GpuShadowVisibilityConstants* )renderer->gpu->map_buffer( mb );
    if ( constants != nullptr ) {
        constants->visibility_cache_texture_index = visibility_cache_texture.index;
        constants->variation_texture_index  = variation_texture.index;
        constants->variation_cache_texture_index  = variation_cache_texture.index;
        constants->samples_count_cache_texture_index = samples_count_cache_texture.index;
        constants->motion_vectors_texture_index = scene.visibility_motion_vector_texture.index;
        constants->normals_texture_index = normals_texture.index;
        constants->filtered_visibility_texture = filtered_visibility_texture.index;
        constants->filetered_variation_texture = filtered_variation_texture.index;
        constants->frame_index = renderer->gpu->absolute_frame % 4;
        constants->resolution_scale = texture_scale;
        constants->resolution_scale_rcp = 1.0f / texture_scale;

        renderer->gpu->unmap_buffer( mb );
    }
}

void ShadowVisibilityPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled ) {
        return;
    }

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        gpu.destroy_descriptor_set( descriptor_set[ i ] );
    }

    gpu.destroy_texture( visibility_cache_texture );
    gpu.destroy_texture( variation_cache_texture );
    gpu.destroy_texture( variation_texture );
    gpu.destroy_texture( samples_count_cache_texture );
    gpu.destroy_texture( filtered_visibility_texture );
    gpu.destroy_texture( filtered_variation_texture );

    gpu.destroy_buffer( gpu_pass_constants );
}

void ShadowVisibilityPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "pbr_lighting" ) );

    u32 pass_index = technique->get_pass_index( "shadow_visibility_variance" );
    GpuTechniquePass& variance_pass = technique->passes[ pass_index ];

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        gpu.destroy_descriptor_set( descriptor_set[ i ] );

        DescriptorSetCreation ds_creation{ };

        render_scene->add_scene_descriptors( ds_creation, variance_pass );
        render_scene->add_lighting_descriptors( ds_creation, variance_pass, i );
        ds_creation.buffer( gpu_pass_constants, 30 );

        ds_creation.set_layout( renderer->gpu->get_descriptor_set_layout( variance_pipeline, k_material_descriptor_set_index ) );

        descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
    }
}

// PointlightShadowPass ///////////////////////////////////////////////////
void PointlightShadowPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {

    if ( !render_scene->pointlight_rendering ) {
        return;
    }

    // Perform meshlet against light culling
    gpu_commands->bind_pipeline( meshlet_culling_pipeline );
    gpu_commands->bind_descriptor_set( &meshlet_culling_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

    u32 group_x = raptor::ceilu32( render_scene->mesh_instances.size * render_scene->active_lights / 32.0f );
    gpu_commands->dispatch( group_x, 1, 1 );

    gpu_commands->global_debug_barrier();

    // Write commands
    gpu_commands->bind_pipeline( meshlet_write_commands_pipeline );
    gpu_commands->bind_descriptor_set( &meshlet_write_commands_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

    group_x = raptor::ceilu32( render_scene->active_lights / 32.0f );
    gpu_commands->dispatch( group_x, 1, 1 );

    gpu_commands->global_debug_barrier();

    // Calculate shadow resolution
    // Upload lights aabbs
    GpuDevice* gpu = renderer->gpu;
    vec4s* gpu_light_aabbs = ( vec4s* )gpu->map_buffer( {light_aabbs, 0, 0} );
    if ( gpu_light_aabbs ) {

        for ( u32 l = 0; l < render_scene->active_lights; ++l ) {
            const Light& light = render_scene->lights[ l ];

            gpu_light_aabbs[ l * 2 ] = light.aabb_min;
            gpu_light_aabbs[ l * 2 + 1 ] = light.aabb_max;
        }

        gpu->unmap_buffer( { light_aabbs, 0, 0 } );
    }

    gpu_commands->bind_pipeline( shadow_resolution_pipeline );
    gpu_commands->bind_descriptor_set( &shadow_resolution_descriptor_set[ current_frame_index ], 1, nullptr, 0 );

    gpu_commands->push_constants( shadow_resolution_pipeline, 0, 16, &render_scene->mesh_draw_counts.depth_pyramid_texture_index );

    gpu_commands->issue_buffer_barrier( shadow_resolutions[ current_frame_index ], ResourceState::RESOURCE_STATE_COPY_SOURCE, ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, QueueType::Graphics, QueueType::Graphics );

    gpu_commands->fill_buffer( shadow_resolutions[ current_frame_index ], 0, sizeof( u32 ) * render_scene->active_lights, 0 );
    // 8 is the group size on both x and y for this shader.
    const f32 tile_size = 64.0f * 8.0f;
    const u32 tile_x_count = raptor::ceilu32( render_scene->scene_data.resolution_x / tile_size  );
    const u32 tile_y_count = raptor::ceilu32(render_scene->scene_data.resolution_y / tile_size );
    gpu_commands->dispatch( tile_x_count, tile_y_count, 1 );

    gpu_commands->issue_buffer_barrier( shadow_resolutions[ current_frame_index ], ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, ResourceState::RESOURCE_STATE_COPY_SOURCE, QueueType::Graphics, QueueType::Graphics );

    gpu_commands->copy_buffer( shadow_resolutions[ current_frame_index ], 0, shadow_resolutions_readback[ current_frame_index ], 0, sizeof( u32 ) * k_num_lights );
}

static void calculate_cubemap_view_projection( vec3s light_world_position, f32 light_radius, u32 face_index, mat4s& out_view_projection ) {

    const mat4s translation = glms_translate_make( glms_vec3_scale( light_world_position, -1.f ) );
    const mat4s projection = glms_perspective( glm_rad( 90.f ), 1.f, 0.01f, light_radius );

    switch ( face_index ) {
        case 0:
        {
            // Positive X
            mat4s rotation_matrix = glms_rotate( glms_mat4_identity(), glm_rad( 90.f ), { 0.f, 1.f, 0.f } );
            rotation_matrix = glms_rotate( rotation_matrix, glm_rad( 180.f ), { 1.f, 0.f, 0.f } );
            mat4s view = glms_mat4_mul( rotation_matrix, translation );

            out_view_projection = glms_mat4_mul( projection, view );

            break;
        }
        case 1:
        {
            // Negative X
            mat4s rotation_matrix = glms_rotate( glms_mat4_identity(), glm_rad( -90.f ), { 0.f, 1.f, 0.f } );
            rotation_matrix = glms_rotate( rotation_matrix, glm_rad( 180.f ), { 1.f, 0.f, 0.f } );
            mat4s view = glms_mat4_mul( rotation_matrix, translation );

            out_view_projection = glms_mat4_mul( projection, view );

            break;
        }
        case 2:
        {
            // Positive Y
            mat4s rotation_matrix = glms_rotate( glms_mat4_identity(), glm_rad( -90.f ), { 1.f, 0.f, 0.f } );
            mat4s view = glms_mat4_mul( rotation_matrix, translation );

            out_view_projection = glms_mat4_mul( projection, view );

            break;
        }
        case 3:
        {
            mat4s rotation_matrix = glms_rotate( glms_mat4_identity(), glm_rad( 90.f ), { 1.f, 0.f, 0.f } );
            mat4s view = glms_mat4_mul( rotation_matrix, translation );

            out_view_projection = glms_mat4_mul( projection, view );

            break;
        }
        case 4:
        {
            mat4s rotation_matrix = glms_rotate( glms_mat4_identity(), glm_rad( 180.f ), { 1.f, 0.f, 0.f } );
            mat4s view = glms_mat4_mul( rotation_matrix, translation );

            out_view_projection = glms_mat4_mul( projection, view );

            break;
        }
        case 5:
        {
            mat4s rotation_matrix = glms_rotate( glms_mat4_identity(), glm_rad( 180.f ), { 0.f, 0.f, 1.f } );
            mat4s view = glms_mat4_mul( rotation_matrix, translation );

            out_view_projection = glms_mat4_mul( projection, view );

            break;
        }
        default:
        {
            RASSERTM( false, "Error face index %u is invalid\n", face_index );
            break;
        }
    }
}

void PointlightShadowPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {

    if ( !render_scene->pointlight_rendering ) {
        return;
    }

    GpuDevice* gpu = renderer->gpu;

    // Tetrahedron mesh test
    if ( render_scene->use_tetrahedron_shadows ) {

        // TODO: recreate dependent resources

        // Clear
        Texture* depth_texture_array = gpu->access_texture( tetrahedron_shadow_texture );
        const u32 layer_count = 1;

        u32 width = depth_texture_array->width;
        u32 height = depth_texture_array->height;
        // Perform manual clear of active lights shadowmaps.
        {
            util_add_image_barrier_ext( gpu, gpu_commands->vk_command_buffer, depth_texture_array, RESOURCE_STATE_COPY_DEST, 0, 1, 0, layer_count, true );

            // TODO: Clearing 256 cubemaps is incredibly slow, for the future try with point sprites at far with depth test always.
            VkClearRect clear_rect;
            clear_rect.baseArrayLayer = 0;
            clear_rect.layerCount = layer_count;
            clear_rect.rect.extent.width = width;
            clear_rect.rect.extent.height = height;
            clear_rect.rect.offset.x = 0;
            clear_rect.rect.offset.y = 0;

            VkClearDepthStencilValue clear_depth_stencil_value;
            clear_depth_stencil_value.depth = 1.f;

            VkImageSubresourceRange clear_range;
            clear_range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            clear_range.baseArrayLayer = 0;
            clear_range.baseMipLevel = 0;
            clear_range.levelCount = 1;
            clear_range.layerCount = layer_count;
            vkCmdClearDepthStencilImage( gpu_commands->vk_command_buffer, depth_texture_array->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_depth_stencil_value, 1, &clear_range );

            util_add_image_barrier_ext( gpu, gpu_commands->vk_command_buffer, depth_texture_array, RESOURCE_STATE_DEPTH_WRITE, 0, 1, 0, layer_count, true );
        }

        depth_texture_array->state = RESOURCE_STATE_DEPTH_WRITE;

        // Setup scissor and viewport
        Rect2DInt scissor{ 0, 0,( u16 )width, ( u16 )height };
        gpu_commands->set_scissor( &scissor );

        Viewport viewport{ };
        viewport.rect = { 0, 0, ( u16 )width, ( u16 )height };
        viewport.min_depth = 0.0f;
        viewport.max_depth = 1.0f;

        gpu_commands->set_viewport( &viewport );

        gpu_commands->bind_pass( cubemap_render_pass, tetrahedron_framebuffer, false );

        if ( render_scene->shadow_constants_cpu_update ) {
            // TODO:

            // From the paper, we add extra room to support soft shadows.
            const f32 fov0 = 143.98570868f + 1.99273682f;
            const f32 fov1 = 125.26438968f + 2.78596497f;

            MapBufferParameters view_projections_cb_map = { pointlight_view_projections_cb[ current_frame_index ], 0, 0 };
            MapBufferParameters light_spheres_cb_map = { pointlight_spheres_cb[ current_frame_index ], 0, 0 };

            mat4s* gpu_view_projections = ( mat4s* )gpu->map_buffer( view_projections_cb_map );
            vec4s* gpu_light_spheres = ( vec4s* )gpu->map_buffer( light_spheres_cb_map );

            if ( gpu_view_projections && gpu_light_spheres ) {

                mat4s face_rotation_matrices[ 4 ];
                /*Matrix4 xRotMatrix, yRotMatrix, zRotMatrix;
                xRotMatrix.SetRotationY( 180.0f );
                yRotMatrix.SetRotationX( 27.36780516f );
                tiledShadowRotMatrices[ 0 ] = yRotMatrix * xRotMatrix;*/
                mat4s rotation_matrix_x = glms_rotate_x( glms_mat4_identity(), glm_rad( 27.36780516f ) );
                mat4s rotation_matrix_y = glms_rotate_y( glms_mat4_identity(), glm_rad( 180.f ) );
                face_rotation_matrices[ 0 ] = glms_mat4_mul( rotation_matrix_y, rotation_matrix_x );

                /*xRotMatrix.SetRotationY( 0.0f );
                yRotMatrix.SetRotationX( 27.36780516f );
                zRotMatrix.SetRotationZ( 90.0f );
                tiledShadowRotMatrices[ 1 ] = zRotMatrix * yRotMatrix * xRotMatrix;*/
                rotation_matrix_x = glms_rotate_x( glms_mat4_identity(), glm_rad( 27.36780516f ) );
                rotation_matrix_y = glms_rotate_y( glms_mat4_identity(), glm_rad( 0.f ) );
                mat4s rotation_matrix_z = glms_rotate_y( glms_mat4_identity(), glm_rad( 90.f ) );
                face_rotation_matrices[ 1 ] = glms_mat4_mul( rotation_matrix_z, glms_mat4_mul( rotation_matrix_y, rotation_matrix_x ) );

                /*xRotMatrix.SetRotationY( 270.0f );
                yRotMatrix.SetRotationX( -27.36780516f );
                tiledShadowRotMatrices[ 2 ] = yRotMatrix * xRotMatrix;*/
                rotation_matrix_x = glms_rotate_x( glms_mat4_identity(), glm_rad( -27.36780516f ) );
                rotation_matrix_y = glms_rotate_y( glms_mat4_identity(), glm_rad( 270.f ) );
                face_rotation_matrices[ 2 ] = glms_mat4_mul( rotation_matrix_y, rotation_matrix_x );

                /*xRotMatrix.SetRotationY( 90.0f );
                yRotMatrix.SetRotationX( -27.36780516f );
                zRotMatrix.SetRotationZ( 90.0f );
                tiledShadowRotMatrices[ 3 ] = zRotMatrix * yRotMatrix * xRotMatrix;*/
                rotation_matrix_x = glms_rotate_x( glms_mat4_identity(), glm_rad( -27.36780516f ) );
                rotation_matrix_y = glms_rotate_y( glms_mat4_identity(), glm_rad( 90.f ) );
                rotation_matrix_z = glms_rotate_y( glms_mat4_identity(), glm_rad( 90.f ) );
                face_rotation_matrices[ 3 ] = glms_mat4_mul( rotation_matrix_z, glms_mat4_mul( rotation_matrix_y, rotation_matrix_x ) );

                /*Matrix4 shadowTexMatrices[ 4 ];
                shadowTexMatrices[ 0 ].Set( tile.size, 0.0f, 0.0f, 0.0f,  0.0f, tile.size * 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                            tile.position.x, tile.position.y - ( tile.size * 0.5f ), 0.0f, 1.0f );
                shadowTexMatrices[ 1 ].Set( tile.size * 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, tile.size, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                            tile.position.x + ( tile.size * 0.5f ), tile.position.y, 0.0f, 1.0f );
                shadowTexMatrices[ 2 ].Set( tile.size, 0.0f, 0.0f, 0.0f, 0.0f, tile.size * 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                            tile.position.x, tile.position.y + ( tile.size * 0.5f ), 0.0f, 1.0f );
                shadowTexMatrices[ 3 ].Set( tile.size * 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, tile.size, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                            tile.position.x - ( tile.size * 0.5f ), tile.position.y, 0.0f, 1.0f );*/
                mat4s shadow_texture_matrices[ 4 ];
                f32 tile_size = 1.f;
                f32 tile_position_x = 0.f;
                f32 tile_position_y = 0.f;
                shadow_texture_matrices[ 0 ].col[ 0 ] = { tile_size, 0.f ,0.f, 0.f };
                shadow_texture_matrices[ 0 ].col[ 1 ] = { 0.f, tile_size * .5f, 0.f, 0.f };
                shadow_texture_matrices[ 0 ].col[ 2 ] = { 0.f, 0.f, 1.f, 0.f };
                shadow_texture_matrices[ 0 ].col[ 3 ] = { tile_position_x, tile_position_y - (tile_size * .5f), 0.f, 1.f };
                shadow_texture_matrices[ 1 ].col[ 0 ] = { tile_size * .5f, 0.f ,0.f, 0.f };
                shadow_texture_matrices[ 1 ].col[ 1 ] = { 0.f, tile_size, 0.f, 0.f };
                shadow_texture_matrices[ 1 ].col[ 2 ] = { 0.f, 0.f, 1.f, 0.f };
                shadow_texture_matrices[ 1 ].col[ 3 ] = { tile_position_x + (tile_size * .5f), tile_position_y, 0.f, 1.f };
                shadow_texture_matrices[ 2 ].col[ 0 ] = { tile_size, 0.f ,0.f, 0.f };
                shadow_texture_matrices[ 2 ].col[ 1 ] = { 0.f, tile_size * .5f, 0.f, 0.f };
                shadow_texture_matrices[ 2 ].col[ 2 ] = { 0.f, 0.f, 1.f, 0.f };
                shadow_texture_matrices[ 2 ].col[ 3 ] = { tile_position_x, tile_position_y + (tile_size * .5f), 0.f, 1.f };
                shadow_texture_matrices[ 3 ].col[ 0 ] = { tile_size * .5f, 0.f, 0.f, 0.f };
                shadow_texture_matrices[ 3 ].col[ 1 ] = { 0.f, tile_size, 0.f, 0.f };
                shadow_texture_matrices[ 3 ].col[ 2 ] = { 0.f, 0.f, 0.f, 1.f };
                shadow_texture_matrices[ 3 ].col[ 3 ] = { tile_position_x - (tile_size * .5f), tile_position_y, 0.f, 1.f };

                for ( u32 l = 0; l < render_scene->active_lights; ++l ) {
                    const Light& light = render_scene->lights[ l ];

                    // Update camera spheres
                    gpu_light_spheres[ l ] = glms_vec4( light.world_position, light.radius );

                    mat4s shadow_projections[ 2 ];
                    /*tiledShadowProjMatrices[ 0 ].SetPerspective( Vector2( fov0, fov1 ), 0.2f, radius );
                    tiledShadowProjMatrices[ 1 ].SetPerspective( Vector2( fov1, fov0 ), 0.2f, radius );*/
                    shadow_projections[ 0 ] = glms_perspective( glm_rad( fov1 ), fov0 / fov1, 0.01f, light.radius );
                    shadow_projections[ 1 ] = glms_perspective( glm_rad( fov0 ), fov1 / fov0, 0.01f, light.radius );

                    /*Matrix4 shadowTransMatrix, shadowViewMatrix;
                    shadowTransMatrix.SetTranslation( -lightBD.position );
                    for ( unsigned int i = 0; i < 4; i++ ) {
                        shadowViewMatrix = tiledShadowRotMatrices[ i ] * shadowTransMatrix;
                        unsigned int index = i & 1;
                        tiledShadowBD.shadowViewProjTexMatrices[ i ] = shadowTexMatrices[ i ] * tiledShadowProjMatrices[ index ] * shadowViewMatrix;
                    }*/

                    mat4s translation = glms_translate_make( glms_vec3_scale( light.world_position, -1.f ) );

                    // 0
                    mat4s view = glms_mat4_mul( face_rotation_matrices[ 0 ], translation );
                    mat4s view_projection = glms_mat4_mul( shadow_texture_matrices[ 0 ], glms_mat4_mul( shadow_projections[ 0 ], view ) );
                    gpu_view_projections[ l * 6 + 0 ] = view_projection;

                    // 1
                    view = glms_mat4_mul( face_rotation_matrices[ 1 ], translation );
                    view_projection = glms_mat4_mul( shadow_texture_matrices[ 1 ], glms_mat4_mul( shadow_projections[ 1 ], view ) );
                    gpu_view_projections[ l * 6 + 1 ] = view_projection;

                    // 2
                    view = glms_mat4_mul( face_rotation_matrices[ 2 ], translation );
                    view_projection = glms_mat4_mul( shadow_texture_matrices[ 2 ], glms_mat4_mul( shadow_projections[ 0 ], view ) );
                    gpu_view_projections[ l * 6 + 2 ] = view_projection;

                    // 3
                    view = glms_mat4_mul( face_rotation_matrices[ 3 ], translation );
                    view_projection = glms_mat4_mul( shadow_texture_matrices[ 3 ], glms_mat4_mul( shadow_projections[ 1 ], view ) );
                    gpu_view_projections[ l * 6 + 3 ] = view_projection;
                }

                gpu->unmap_buffer( view_projections_cb_map );
                gpu->unmap_buffer( light_spheres_cb_map );
            }
        }

        if ( render_scene->use_meshlets_emulation ) {
            // TODO:
        } else if ( render_scene->pointlight_use_meshlets ) {

            // Render ALL meshlets shadows
            gpu_commands->bind_pipeline( tetrahedron_meshlet_pipeline );

            DescriptorSetHandle handles[] = { render_scene->mesh_shader_early_descriptor_set[ current_frame_index ], cubemap_meshlet_draw_descriptor_set[ current_frame_index ] };
            gpu_commands->bind_descriptor_set( handles, 2, nullptr, 0 );

            gpu_commands->draw_mesh_task_indirect_count( meshlet_shadow_indirect_cb[ current_frame_index ], 0, per_light_meshlet_instances[ current_frame_index ], sizeof( u32 ) * k_num_lights, layer_count, sizeof( vec4s ) );
        } else {
            // Support for non-meshlet pointlights needed ?
        }

        gpu_commands->end_current_render_pass();
    }
    else {

        // Cubemap shadows

        // Recreate texture and framebuffer
        recreate_lightcount_dependent_resources( *render_scene );

        Texture* depth_texture_array = gpu->access_texture( cubemap_shadow_array_texture );
        const u32 layer_count = 6 * render_scene->active_lights;

        u32 width = depth_texture_array->width;
        u32 height = depth_texture_array->height;
        // Perform manual clear of active lights shadowmaps.
        {
            util_add_image_barrier_ext( gpu, gpu_commands->vk_command_buffer, depth_texture_array, RESOURCE_STATE_COPY_DEST, 0, 1, 0, layer_count, true );

            // TODO: Clearing 256 cubemaps is incredibly slow, for the future try with point sprites at far with depth test always.
            VkClearRect clear_rect;
            clear_rect.baseArrayLayer = 0;
            clear_rect.layerCount = layer_count;
            clear_rect.rect.extent.width = width;
            clear_rect.rect.extent.height = height;
            clear_rect.rect.offset.x = 0;
            clear_rect.rect.offset.y = 0;

            VkClearDepthStencilValue clear_depth_stencil_value;
            clear_depth_stencil_value.depth = 1.f;

            VkImageSubresourceRange clear_range;
            clear_range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            clear_range.baseArrayLayer = 0;
            clear_range.baseMipLevel = 0;
            clear_range.levelCount = 1;
            clear_range.layerCount = layer_count;
            vkCmdClearDepthStencilImage( gpu_commands->vk_command_buffer, depth_texture_array->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_depth_stencil_value, 1, &clear_range );

            util_add_image_barrier_ext( gpu, gpu_commands->vk_command_buffer, depth_texture_array, RESOURCE_STATE_DEPTH_WRITE, 0, 1, 0, layer_count, true );
        }

        depth_texture_array->state = RESOURCE_STATE_DEPTH_WRITE;

        // Setup scissor and viewport
        Rect2DInt scissor{ 0, 0,( u16 )width, ( u16 )height };
        gpu_commands->set_scissor( &scissor );

        Viewport viewport{ };
        viewport.rect = { 0, 0, ( u16 )width, ( u16 )height };
        viewport.min_depth = 0.0f;
        viewport.max_depth = 1.0f;

        gpu_commands->set_viewport( &viewport );

        gpu_commands->bind_pass( cubemap_render_pass, cubemap_framebuffer, false );

        // Update view projection matrices and camera spheres.
        // NOTE: this operation can be slow on CPU if many lights are casting shadows, thus
        // a GPU implementation is also given.
        if ( render_scene->shadow_constants_cpu_update ) {

            MapBufferParameters view_projections_cb_map = { pointlight_view_projections_cb[ current_frame_index ], 0, 0 };
            MapBufferParameters light_spheres_cb_map = { pointlight_spheres_cb[ current_frame_index ], 0, 0 };

            mat4s* gpu_view_projections = ( mat4s* )gpu->map_buffer( view_projections_cb_map );
            vec4s* gpu_light_spheres = ( vec4s* )gpu->map_buffer( light_spheres_cb_map );

            const mat4s left_handed_scale_matrix = glms_scale_make( { 1,1,-1 } );

            if ( gpu_view_projections && gpu_light_spheres ) {

                for ( u32 l = 0; l < render_scene->active_lights; ++l ) {
                    const Light& light = render_scene->lights[ l ];

                    // Update camera spheres
                    gpu_light_spheres[ l ] = glms_vec4( light.world_position, light.radius );

                    const mat4s projection = glms_perspective( glm_rad( 90.f ), 1.f, 0.01f, light.radius );

                    // Positive X matrices
                    mat4s view = glms_look( light.world_position, { -1,0,0 }, { 0,1,0 } );
                    view = glms_mat4_mul( left_handed_scale_matrix, view );
                    mat4s view_projection = glms_mat4_mul( projection, view );

                    gpu_view_projections[ l * 6 + 0 ] = view_projection;

                    // Negative X
                    view = glms_look( light.world_position, { 1,0,0 }, { 0,1,0 } );
                    view = glms_mat4_mul( left_handed_scale_matrix, view );
                    view_projection = glms_mat4_mul( projection, view );

                    gpu_view_projections[ l * 6 + 1 ] = view_projection;

                    // Positive Y
                    view = glms_look( light.world_position, { 0,-1,0 }, { 0,0,-1 } );
                    view = glms_mat4_mul( left_handed_scale_matrix, view );
                    view_projection = glms_mat4_mul( projection, view );

                    gpu_view_projections[ l * 6 + 2 ] = view_projection;

                    // Negative Y
                    view = glms_look( light.world_position, { 0,1,0 }, { 0,0,1 } );
                    view = glms_mat4_mul( left_handed_scale_matrix, view );
                    view_projection = glms_mat4_mul( projection, view );

                    gpu_view_projections[ l * 6 + 3 ] = view_projection;

                    // Positive Z
                    view = glms_look( light.world_position, { 0,0,-1 }, { 0,1,0 } );
                    view = glms_mat4_mul( left_handed_scale_matrix, view );
                    view_projection = glms_mat4_mul( projection, view );

                    gpu_view_projections[ l * 6 + 4 ] = view_projection;

                    // Negative Z
                    view = glms_look( light.world_position, { 0,0,1 }, { 0,1,0 } );
                    view = glms_mat4_mul( left_handed_scale_matrix, view );
                    view_projection = glms_mat4_mul( projection, view );

                    gpu_view_projections[ l * 6 + 5 ] = view_projection;
                }

                gpu->unmap_buffer( view_projections_cb_map );
                gpu->unmap_buffer( light_spheres_cb_map );
            }
        }

        MapBufferParameters shadow_resolution_map = { shadow_resolutions_readback[ current_frame_index ], 0, 0 };
        u32* shadow_resolution_read = ( u32* )gpu->map_buffer( shadow_resolution_map );

        if ( render_scene->use_meshlets_emulation ) {
            // TODO:
        } else if ( render_scene->pointlight_use_meshlets ) {

            // Render ALL meshlets shadows
            gpu_commands->bind_pipeline( cubemap_meshlets_pipeline );

            DescriptorSetHandle handles[] = { render_scene->mesh_shader_early_descriptor_set[ current_frame_index ], cubemap_meshlet_draw_descriptor_set[ current_frame_index ] };
            gpu_commands->bind_descriptor_set( handles, 2, nullptr, 0 );

            // Draw each light individually
            for ( u32 l = 0; l < render_scene->active_lights; ++l ) {
                const Light& light = render_scene->lights[ l ];

                //rprint( "Shadow resolution %u, light %u\n", shadow_resolution_read[ l ], l );

                gpu_commands->set_viewport( &viewport );

                const u32 argument_offset = sizeof( f32 ) * 4 * 6 * l;
                u32 draw_offset = l * 6;
                gpu_commands->push_constants( cubemap_meshlets_pipeline, 0, 16, &draw_offset );
                gpu_commands->draw_mesh_task_indirect( meshlet_shadow_indirect_cb[ current_frame_index ], argument_offset, 6, sizeof( vec4s ) );
            }
        } else {
            // Support for non-meshlet pointlights needed ?
        }

        gpu->unmap_buffer( shadow_resolution_map );

        gpu_commands->end_current_render_pass();

        // Copy debug texture
        // TODO: subresource state complains a lot.
        if ( render_scene->cubemap_face_debug_enabled ) {
            u16 source_cubemap_face = render_scene->cubemap_debug_array_index * 6 + render_scene->cubemap_debug_face_index;
            gpu_commands->copy_texture( cubemap_shadow_array_texture, { 0, 1, source_cubemap_face, 1 }, cubemap_debug_face_texture, { 0, 1, 0, 1 }, RESOURCE_STATE_SHADER_RESOURCE );
        }
    }
}

void PointlightShadowPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph,
                                          Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "point_shadows_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    recreate_lightcount_dependent_resources( scene );

    // Create render pass
    RenderPassCreation render_pass_creation;
    render_pass_creation.reset().set_name( node->name ).set_depth_stencil_texture( VK_FORMAT_D16_UNORM, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL )
        .set_depth_stencil_operations( RenderPassOperation::DontCare, RenderPassOperation::DontCare );

    cubemap_render_pass = gpu.create_render_pass( render_pass_creation );

    RASSERTM( 6 * k_num_lights <= gpu.max_framebuffer_layers, "Creating framebuffer with more layers than possible (max :%u, trying to create count %u). Refactor to have more layers", gpu.max_framebuffer_layers, 6 * k_num_lights );

    // Create view constant buffer
    raptor::BufferCreation buffer_creation;

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        buffer_creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( mat4s ) * 6 * k_num_lights ).set_name( "pointlight_pass_view_projections" );
        pointlight_view_projections_cb[ i ] = gpu.create_buffer( buffer_creation );

        buffer_creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( vec4s ) * 6 * k_num_lights ).set_name( "pointlight_pass_spheres" );
        pointlight_spheres_cb[ i ] = gpu.create_buffer( buffer_creation );
    }

    const u64 hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    const u32 depth_cubemap_pass_index = main_technique->get_pass_index( "depth_cubemap" );

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
        mesh_instance_draw.material_pass_index = depth_cubemap_pass_index;

        mesh_instance_draws.push( mesh_instance_draw );
    }

    DescriptorSetCreation ds_creation;

    GpuTechnique* meshlet_technique = renderer->resource_cache.techniques.get( hash_calculate( "meshlet" ) );

    // Meshlet culling
    {
        u32 pass_index = meshlet_technique->get_pass_index( "meshlet_pointshadows_culling" );
        GpuTechniquePass& pass = meshlet_technique->passes[ pass_index ];

        meshlet_culling_pipeline = pass.pipeline;

        u32 max_per_light_meshlets = 45000;
        u32 total_light_meshlets = k_num_lights * max_per_light_meshlets * 2;

        for ( u32 i = 0; i < k_max_frames; ++i ) {

            meshlet_visible_instances[ i ] = renderer->gpu->create_buffer( buffer_creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, sizeof( u32 ) * total_light_meshlets ).set_name( "meshlet_visible_instances" ) );
            per_light_meshlet_instances[ i ] = renderer->gpu->create_buffer( buffer_creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, ResourceUsageType::Immutable, sizeof( u32 ) * ( k_num_lights + 1 ) * 2 ).set_name( "per_light_meshlet_instances" ) );

            ds_creation.reset();

            scene.add_scene_descriptors( ds_creation, pass );
            //scene.add_debug_descriptors( ds_creation, pass );
            scene.add_mesh_descriptors( ds_creation, pass );
            scene.add_meshlet_descriptors( ds_creation, pass );
            //scene.add_lighting_descriptors( ds_creation, pass, i );
            ds_creation.buffer( scene.lights_list_sb, 21 );
            ds_creation.buffer( meshlet_visible_instances[i], 30).buffer(per_light_meshlet_instances[i], 31).set_layout(renderer->gpu->get_descriptor_set_layout(meshlet_culling_pipeline, k_material_descriptor_set_index));

            meshlet_culling_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
        }
    }
    // Meshlet command writing
    {
        u32 pass_index = meshlet_technique->get_pass_index( "meshlet_pointshadows_commands_generation" );
        GpuTechniquePass& pass = meshlet_technique->passes[ pass_index ];

        meshlet_write_commands_pipeline = pass.pipeline;

        for ( u32 i = 0; i < k_max_frames; ++i ) {

            meshlet_shadow_indirect_cb[ i ] = renderer->gpu->create_buffer( buffer_creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, ResourceUsageType::Immutable, sizeof( vec4s ) * k_num_lights * 6 ).set_name( "per_light_meshlet_shadow_indirect" ) );

            ds_creation.reset();
            ds_creation.buffer( meshlet_visible_instances[ i ], 30 ).buffer( per_light_meshlet_instances[ i ], 31 ).buffer( meshlet_shadow_indirect_cb[ i ], 32 )
                .buffer( pointlight_spheres_cb[ i ], 33 ).buffer( pointlight_view_projections_cb[ i ], 34 ).buffer( scene.lights_list_sb, 35 )
                .set_layout( renderer->gpu->get_descriptor_set_layout( meshlet_write_commands_pipeline, k_material_descriptor_set_index ) );
            scene.add_scene_descriptors( ds_creation, pass );

            meshlet_write_commands_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
        }
    }
    // Meshlet drawing
    if ( gpu.mesh_shaders_extension_present ) {
        u32 pass_index = meshlet_technique->get_pass_index( "depth_cubemap" );
        // Cubemap rendering
        cubemap_meshlets_pipeline = meshlet_technique->passes[ pass_index ].pipeline;

        DescriptorSetLayoutHandle pass_layout_handle = renderer->gpu->get_descriptor_set_layout( meshlet_technique->passes[ pass_index ].pipeline, 2 );
        DescriptorSetLayout* pass_layout = renderer->gpu->access_descriptor_set_layout( pass_layout_handle );

        if ( pass_layout ) {

            for ( u32 i = 0; i < k_max_frames; ++i ) {
                ds_creation.reset().buffer( pointlight_spheres_cb[i], 0).buffer(meshlet_shadow_indirect_cb[i], 1)
                    .buffer( meshlet_visible_instances[ i ], 2 ).buffer( pointlight_view_projections_cb[ i ], 4 ).set_layout( pass_layout_handle ).set_set_index( 2 );
                cubemap_meshlet_draw_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
            }
        }

        // Tetrahedron rendering
        pass_index = meshlet_technique->get_pass_index( "depth_tetrahedron" );

        tetrahedron_meshlet_pipeline = meshlet_technique->passes[ pass_index ].pipeline;
    }
    // Shadow resolution computation
    {
        u32 pass_index = meshlet_technique->get_pass_index( "pointshadows_resolution_calculation" );

        GpuTechniquePass& pass = meshlet_technique->passes[ pass_index ];
        shadow_resolution_pipeline = pass.pipeline;
        // AABB is defined as 2 vec4, min and max vectors.
        light_aabbs = renderer->gpu->create_buffer( buffer_creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, sizeof( vec4s ) * k_num_lights * 2 ).set_name( "light_aabbs" ) );

        for ( u32 i = 0; i < k_max_frames; ++i ) {

            shadow_resolutions[ i ] = renderer->gpu->create_buffer( buffer_creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, sizeof( u32 ) * k_num_lights ).set_name( "shadow_resolutions" ) );
            shadow_resolutions_readback[ i ] = renderer->gpu->create_buffer( buffer_creation.set( VK_BUFFER_USAGE_TRANSFER_DST_BIT, ResourceUsageType::Readback, sizeof( u32 ) * k_num_lights ).set_name( "shadow_resolutions_readback" ) );

            DescriptorSetCreation ds_creation{ };
            ds_creation.reset();

            scene.add_scene_descriptors( ds_creation, pass );
            ds_creation.buffer( light_aabbs, 35 );
            ds_creation.buffer( shadow_resolutions[ i ], 36 );
            ds_creation.buffer( scene.lights_list_sb, 37 );

            ds_creation.set_layout( renderer->gpu->get_descriptor_set_layout( shadow_resolution_pipeline, k_material_descriptor_set_index ) );

            shadow_resolution_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
        }
    }
}

void PointlightShadowPass::upload_gpu_data( RenderScene& scene ) {
}

void PointlightShadowPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled )
        return;

    mesh_instance_draws.shutdown();

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        gpu.destroy_buffer( pointlight_view_projections_cb[ i ] );
        gpu.destroy_buffer( pointlight_spheres_cb[ i ] );
        gpu.destroy_descriptor_set( cubemap_meshlet_draw_descriptor_set[ i ] );
        gpu.destroy_descriptor_set( meshlet_culling_descriptor_set[ i ] );
        gpu.destroy_buffer( meshlet_visible_instances[ i ] );
        gpu.destroy_buffer( per_light_meshlet_instances[ i ] );
        gpu.destroy_descriptor_set( shadow_resolution_descriptor_set[ i ] );
        gpu.destroy_descriptor_set( meshlet_write_commands_descriptor_set[ i ] );
        gpu.destroy_buffer( meshlet_shadow_indirect_cb[ i ] );
        gpu.destroy_buffer( shadow_resolutions[ i ] );
        gpu.destroy_buffer( shadow_resolutions_readback[ i ] );
    }

    gpu.destroy_render_pass( cubemap_render_pass );

    gpu.destroy_buffer( light_aabbs );

    gpu.destroy_texture( tetrahedron_shadow_texture );
    gpu.destroy_texture( cubemap_debug_face_texture );
    gpu.destroy_texture( cubemap_shadow_array_texture );

    gpu.destroy_framebuffer( cubemap_framebuffer );
    gpu.destroy_framebuffer( tetrahedron_framebuffer );

    gpu.destroy_page_pool( shadow_maps_pool );
}

void PointlightShadowPass::recreate_lightcount_dependent_resources( RenderScene& scene ) {

    GpuDevice& gpu = *renderer->gpu;

    const u32 active_lights = scene.active_lights;

    if ( active_lights == last_active_lights ) {
        return;
    }

    // Destroy resources if they were created
    if ( last_active_lights > 0 ) {

        gpu.destroy_texture( cubemap_debug_face_texture );
        gpu.destroy_texture( cubemap_shadow_array_texture );
        gpu.destroy_texture( tetrahedron_shadow_texture );

        gpu.destroy_framebuffer( cubemap_framebuffer );
        gpu.destroy_framebuffer( tetrahedron_framebuffer );
    }

    last_active_lights = active_lights;

    // Create new resources
    // Create cube depth array texture
    raptor::TextureCreation texture_creation;
    // TODO: layer count should be the maximum
    u32 layer_width = 512;
    u32 layer_height = layer_width;

    VkFormat depth_texture_format = VK_FORMAT_D16_UNORM;

    // Create cubemap debug texture
    // TODO(marco): these textures should only be created once, we only need to change
    // the pages that are bound
    texture_creation.reset().set_size( layer_width, layer_height, 1 ).set_format_type( depth_texture_format, TextureType::Texture2D )
        .set_flags( TextureFlags::RenderTarget_mask ).set_name( "cubemap_array_debug" );
    cubemap_debug_face_texture = gpu.create_texture( texture_creation );

    u32 max_width = 512;
    u32 max_height = max_width;
    u32 max_layers = 256 * 6; // NOTE(marco): we can support at maximum 256 lights

    texture_creation.set_size( max_width, max_height, 1 ).set_layers( max_layers ).set_mips( 1 ).set_format_type( depth_texture_format, TextureType::Texture_Cube_Array )
        .set_flags( TextureFlags::RenderTarget_mask | TextureFlags::Sparse_mask ).set_name( "depth_cubemap_array" );
    cubemap_shadow_array_texture = gpu.create_texture( texture_creation );

    if ( shadow_maps_pool.index == k_invalid_index ) {
        shadow_maps_pool = gpu.allocate_texture_pool( cubemap_shadow_array_texture, rgiga( 1 ) );
    }

    gpu.reset_pool( shadow_maps_pool );

    for ( u32 light = 0; light < active_lights; ++light ) {
        // TODO(marco): use light resolution
        for ( u32 face = 0; face < 6; ++face ) {
            gpu.bind_texture_pages( shadow_maps_pool, cubemap_shadow_array_texture, 0, 0, layer_width, layer_height, ( light * 6 ) + face );
        }
    }

    // Create framebuffer
    raptor::FramebufferCreation frame_buffer_creation;
    frame_buffer_creation.reset().set_depth_stencil_texture( cubemap_shadow_array_texture ).set_name( "depth_cubemap_array_fb" ).set_width_height( max_width, max_height ).set_layers( max_layers );
    cubemap_framebuffer = gpu.create_framebuffer( frame_buffer_creation );

    // Cache shadow depth view index
    scene.cubemap_shadows_index = cubemap_shadow_array_texture.index;

    // Tetrahedron mapping
    texture_creation.reset().set_size( layer_width, layer_height, 1 ).set_format_type( depth_texture_format, TextureType::Texture2D )
        .set_flags( TextureFlags::RenderTarget_mask ).set_name( "tetrahedron_shadow_texture" );
    tetrahedron_shadow_texture = gpu.create_texture( texture_creation );

    frame_buffer_creation.reset().set_depth_stencil_texture( tetrahedron_shadow_texture ).set_name( "depth_tetrahedron_fb" ).set_width_height( layer_width, layer_height );
    tetrahedron_framebuffer = gpu.create_framebuffer( frame_buffer_creation );
}

void PointlightShadowPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {
}

// VolumetricFogPass //////////////////////////////////////////////////////
void VolumetricFogPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    Renderer* renderer = render_scene->renderer;

    static i32 times = 3;
    if ( times >= 0 ) {
        --times;
        has_baked_noise = true;

        gpu_commands->issue_texture_barrier( volumetric_noise_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

        gpu_commands->bind_pipeline( volumetric_noise_baking );
        gpu_commands->bind_descriptor_set( &fog_descriptor_set, 1, nullptr, 0 );
        gpu_commands->push_constants( volumetric_noise_baking, 0, 4, &volumetric_noise_texture.index );
        gpu_commands->dispatch( 64 / 8, 64 / 8, 64 );

        gpu_commands->issue_texture_barrier( volumetric_noise_texture, RESOURCE_STATE_SHADER_RESOURCE, 0, 1 );
    }

    previous_light_scattering_texture_index = current_light_scattering_texture_index;
    current_light_scattering_texture_index = ( current_light_scattering_texture_index + 1 ) % 2;

    // Inject data
    gpu_commands->push_marker( "VolFog Inject" );
    gpu_commands->issue_texture_barrier( froxel_data_texture_0, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

    gpu_commands->bind_pipeline( inject_data_pipeline );
    gpu_commands->bind_descriptor_set( &fog_descriptor_set, 1, nullptr, 0 );

    const u32 dispatch_group_x = ceilu32( render_scene->volumetric_fog_tile_count_x / 8.0f );
    const u32 dispatch_group_y = ceilu32( render_scene->volumetric_fog_tile_count_y / 8.0f );
    gpu_commands->dispatch( dispatch_group_x, dispatch_group_y, render_scene->volumetric_fog_slices );

    gpu_commands->issue_texture_barrier( froxel_data_texture_0, RESOURCE_STATE_SHADER_RESOURCE, 0, 1 );

    gpu_commands->global_debug_barrier();
    gpu_commands->pop_marker();

    gpu_commands->push_marker( "VolFog Scattering" );
    TextureHandle current_light_scattering_texture = light_scattering_texture[ current_light_scattering_texture_index ];

    // Light scattering
    gpu_commands->issue_texture_barrier( light_scattering_texture[ previous_light_scattering_texture_index ], RESOURCE_STATE_SHADER_RESOURCE, 0, 1 );
    gpu_commands->issue_texture_barrier( current_light_scattering_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->issue_texture_barrier( integrated_light_scattering_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

    gpu_commands->bind_pipeline( light_scattering_pipeline );
    gpu_commands->bind_descriptor_set( &light_scattering_descriptor_set[ current_frame_index ], 1, nullptr, 0 );
    gpu_commands->dispatch( dispatch_group_x, dispatch_group_y, render_scene->volumetric_fog_slices );

    gpu_commands->issue_texture_barrier( current_light_scattering_texture, RESOURCE_STATE_SHADER_RESOURCE, 0, 1 );

    gpu_commands->global_debug_barrier();
    gpu_commands->pop_marker();

    // Spatial filtering
    gpu_commands->push_marker( "VolFog Spatial" );
    gpu_commands->issue_texture_barrier( froxel_data_texture_0, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

    // Reads light scattering texture and writes froxel_data_0
    gpu_commands->bind_pipeline( spatial_filtering_pipeline );
    gpu_commands->bind_descriptor_set( &fog_descriptor_set, 1, nullptr, 0 );
    gpu_commands->dispatch( dispatch_group_x, dispatch_group_y, render_scene->volumetric_fog_slices );

    gpu_commands->pop_marker();

    gpu_commands->push_marker( "VolFog Temporal" );
    gpu_commands->issue_texture_barrier( current_light_scattering_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->issue_texture_barrier( froxel_data_texture_0, RESOURCE_STATE_SHADER_RESOURCE, 0, 1 );

    // Temporal filtering
    // Reads froxel_data_0 and writes light scattering texture
    gpu_commands->bind_pipeline( temporal_filtering_pipeline );
    gpu_commands->dispatch( dispatch_group_x, dispatch_group_y, render_scene->volumetric_fog_slices );
    gpu_commands->pop_marker();

    gpu_commands->push_marker( "VolFog Integration" );
    gpu_commands->issue_texture_barrier( current_light_scattering_texture, RESOURCE_STATE_SHADER_RESOURCE, 0, 1 );

    // Light integration
    gpu_commands->bind_pipeline( light_integration_pipeline );
    gpu_commands->bind_descriptor_set( &fog_descriptor_set, 1, nullptr, 0 );

    // NOTE: Z = 1 as we integrate inside the shader.
    gpu_commands->dispatch( dispatch_group_x, dispatch_group_y, 1 );

    gpu_commands->global_debug_barrier();

    gpu_commands->issue_texture_barrier( integrated_light_scattering_texture, RESOURCE_STATE_SHADER_RESOURCE, 0, 1 );
    gpu_commands->pop_marker();
}

void VolumetricFogPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    Renderer* renderer = render_scene->renderer;
}

void VolumetricFogPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {
    if ( !enabled )
        return;

    // TODO: resizable volumetric fog texture
}

void VolumetricFogPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {

    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "volumetric_fog_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;

    GpuDevice& gpu = *renderer->gpu;

    raptor::TextureCreation texture_creation;
    texture_creation.reset().set_size( scene.volumetric_fog_tile_count_x, scene.volumetric_fog_tile_count_y, scene.volumetric_fog_slices )
        .set_format_type( VK_FORMAT_R16G16B16A16_SFLOAT, TextureType::Texture3D ).set_flags( raptor::TextureFlags::Compute_mask ).set_name( "froxel_data_texture_0" );

    froxel_data_texture_0 = gpu.create_texture( texture_creation );

    // Temporal reprojection uses those two textures.
    texture_creation.set_name( "light_scattering_texture_0" );
    light_scattering_texture[ 0 ] = gpu.create_texture( texture_creation );
    texture_creation.set_name( "light_scattering_texture_1" );
    light_scattering_texture[ 1 ] = gpu.create_texture( texture_creation );

    texture_creation.set_name( "integrated_light_scattering_texture" );
    integrated_light_scattering_texture = gpu.create_texture( texture_creation );

    // Create volumetric noise texture
    texture_creation.reset().set_size( 64, 64, 64 ).set_format_type( VK_FORMAT_R8_UNORM, TextureType::Texture3D )
                            .set_flags( raptor::TextureFlags::Compute_mask ).set_name( "volumetric_noise" );
    volumetric_noise_texture = gpu.create_texture( texture_creation );

    // Create tiling sampler for volumetric noise texture
    SamplerCreation sampler_creation;
    sampler_creation.set_address_mode_uvw( VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT )
                    .set_min_mag_mip( VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR ).set_name( "volumetric_tiling_sampler" );
    volumetric_tiling_sampler = gpu.create_sampler( sampler_creation );
    gpu.link_texture_sampler( volumetric_noise_texture, volumetric_tiling_sampler );

    // Cache texture index
    scene.volumetric_fog_texture_index = integrated_light_scattering_texture.index;

    raptor::BufferCreation buffer_creation;
    buffer_creation.set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuVolumetricFogConstants ) ).set_name( "volumetric_fog_constants" );
    fog_constants = gpu.create_buffer( buffer_creation );

    // Cache frustum cull shader
    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "volumetric_fog" ) );
    if ( technique ) {
        // Inject Data
        u32 pass_index = technique->get_pass_index( "inject_data" );
        GpuTechniquePass& inject_data_pass = technique->passes[ pass_index ];

        inject_data_pipeline = inject_data_pass.pipeline;

        // Layout for simpler shaders. For now just light scattering needs lighting bindings.
        DescriptorSetLayoutHandle common_layout = gpu.get_descriptor_set_layout( inject_data_pipeline, k_material_descriptor_set_index );

        DescriptorSetCreation ds_creation{};
        ds_creation.reset().set_layout( common_layout );
        ds_creation.buffer( fog_constants, 40 );
        scene.add_scene_descriptors( ds_creation, inject_data_pass );
        fog_descriptor_set = gpu.create_descriptor_set( ds_creation );

        // Light integration
        pass_index = technique->get_pass_index( "light_integration" );
        GpuTechniquePass& light_integration_pass = technique->passes[ pass_index ];

        light_integration_pipeline = light_integration_pass.pipeline;

        pass_index = technique->get_pass_index( "spatial_filtering" );
        GpuTechniquePass& spatial_filtering_pass = technique->passes[ pass_index ];

        spatial_filtering_pipeline = spatial_filtering_pass.pipeline;

        pass_index = technique->get_pass_index( "temporal_filtering" );
        GpuTechniquePass& temporal_filtering_pass = technique->passes[ pass_index ];

        temporal_filtering_pipeline = temporal_filtering_pass.pipeline;

        pass_index = technique->get_pass_index( "volumetric_noise_baking" );
        GpuTechniquePass& noise_baking_pass = technique->passes[ pass_index ];

        volumetric_noise_baking = noise_baking_pass.pipeline;

        // Light scattering
        pass_index = technique->get_pass_index( "light_scattering" );
        GpuTechniquePass& light_scattering_pass = technique->passes[ pass_index ];

        light_scattering_pipeline = light_scattering_pass.pipeline;

        DescriptorSetLayoutHandle light_scattering_layout = gpu.get_descriptor_set_layout( light_scattering_pipeline, k_material_descriptor_set_index );

        for ( u32 i = 0; i < k_max_frames; ++i ) {
            ds_creation.reset().set_layout( light_scattering_layout );
            ds_creation.buffer( fog_constants, 40 );
            scene.add_scene_descriptors( ds_creation, light_scattering_pass );
            scene.add_lighting_descriptors( ds_creation, light_scattering_pass, i );
            light_scattering_descriptor_set[ i ] = gpu.create_descriptor_set( ds_creation );
        }
    }
}

void VolumetricFogPass::upload_gpu_data( RenderScene& scene ) {
    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    // Update per mesh material buffer
    // TODO: update only changed stuff, this is now dynamic so it can't be done.
    MapBufferParameters cb_map = { fog_constants, 0, 0 };
    GpuVolumetricFogConstants* gpu_constants = ( GpuVolumetricFogConstants* )gpu.map_buffer( cb_map );
    if ( gpu_constants ) {

        const mat4s& view = scene.scene_data.world_to_camera;
        // TODO: custom near and far for froxels
        //mat4s froxel_ortho = glms_perspective( glm_rad( field_of_view_y ), aspect_ratio, near_plane, far_plane );
        //gpu_constants->froxel_inverse_view_projection = glms_mat4_inv( glms_mat4_mul( projection, view ) );
        // TODO: customize near/far and recalculate projection.
        gpu_constants->froxel_inverse_view_projection = scene.scene_data.inverse_view_projection;
        gpu_constants->light_scattering_texture_index = light_scattering_texture[current_light_scattering_texture_index].index;
        gpu_constants->previous_light_scattering_texture_index = light_scattering_texture[ previous_light_scattering_texture_index ].index;
        gpu_constants->froxel_data_texture_index = froxel_data_texture_0.index;
        gpu_constants->integrated_light_scattering_texture_index = integrated_light_scattering_texture.index;

        gpu_constants->froxel_near = scene.scene_data.z_near;
        gpu_constants->froxel_far = scene.scene_data.z_far;

        // TODO: add tweakability for this
        gpu_constants->density_modifier = scene.volumetric_fog_density;
        gpu_constants->scattering_factor = scene.volumetric_fog_scattering_factor;
        gpu_constants->temporal_reprojection_percentage = scene.volumetric_fog_temporal_reprojection_percentage;
        gpu_constants->use_temporal_reprojection = scene.volumetric_fog_use_temporal_reprojection ? 1 : 0;
        gpu_constants->time_random_01 = get_random_value( 0.0f, 1.0f );
        gpu_constants->phase_anisotropy_01 = scene.volumetric_fog_phase_anisotropy_01;

        gpu_constants->froxel_dimension_x = scene.volumetric_fog_tile_count_x;
        gpu_constants->froxel_dimension_y = scene.volumetric_fog_tile_count_y;
        gpu_constants->froxel_dimension_z = scene.volumetric_fog_slices;
        gpu_constants->phase_function_type = scene.volumetric_fog_phase_function_type;

        gpu_constants->height_fog_density = scene.volumetric_fog_height_fog_density;
        gpu_constants->height_fog_falloff = scene.volumetric_fog_height_fog_falloff;
        gpu_constants->noise_scale = scene.volumetric_fog_noise_scale;
        gpu_constants->lighting_noise_scale = scene.volumetric_fog_lighting_noise_scale;
        gpu_constants->noise_type = scene.volumetric_fog_noise_type;
        gpu_constants->use_spatial_filtering = scene.volumetric_fog_use_spatial_filtering;
        gpu_constants->temporal_reprojection_jitter_scale = scene.volumetric_fog_temporal_reprojection_jittering_scale;

        gpu_constants->volumetric_noise_texture_index = volumetric_noise_texture.index;
        gpu_constants->volumetric_noise_position_multiplier = scene.volumetric_fog_noise_position_scale;
        gpu_constants->volumetric_noise_speed_multiplier = scene.volumetric_fog_noise_speed_scale * 0.001f;

        gpu_constants->box_color = scene.volumetric_fog_box_color;
        gpu_constants->box_fog_density = scene.volumetric_fog_box_density;
        gpu_constants->box_position = scene.volumetric_fog_box_position;
        gpu_constants->box_half_size = glms_vec3_scale( scene.volumetric_fog_box_size, 0.5f );

        gpu.unmap_buffer( cb_map );
    }

}

void VolumetricFogPass::free_gpu_resources( GpuDevice& gpu ) {

    gpu.destroy_texture( froxel_data_texture_0 );
    gpu.destroy_texture( light_scattering_texture[ 0 ] );
    gpu.destroy_texture( light_scattering_texture[ 1 ] );
    gpu.destroy_texture( integrated_light_scattering_texture );

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        gpu.destroy_descriptor_set( light_scattering_descriptor_set[ i ] );
    }

    gpu.destroy_texture( volumetric_noise_texture );
    gpu.destroy_sampler( volumetric_tiling_sampler );

    gpu.destroy_descriptor_set( fog_descriptor_set );
    gpu.destroy_buffer( fog_constants );
}

void VolumetricFogPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "volumetric_fog" ) );
    if ( technique ) {
        // Light scattering
        u32 pass_index = technique->get_pass_index( "light_scattering" );
        GpuTechniquePass& pass = technique->passes[ pass_index ];

        DescriptorSetLayoutHandle light_scattering_layout = gpu.get_descriptor_set_layout( light_scattering_pipeline, k_material_descriptor_set_index );

        DescriptorSetCreation ds_creation{};

        for ( u32 i = 0; i < k_max_frames; ++i ) {

            gpu.destroy_descriptor_set( light_scattering_descriptor_set[ i ] );

            ds_creation.reset().set_layout( light_scattering_layout );
            ds_creation.buffer( fog_constants, 40 );
            render_scene->add_scene_descriptors( ds_creation, pass );
            render_scene->add_lighting_descriptors( ds_creation, pass, i );
            light_scattering_descriptor_set[ i ] = gpu.create_descriptor_set( ds_creation );
        }
    }
}

// TemporalAntiAliasingPass ///////////////////////////////////////////////
static TextureHandle temp_taa_output;
static TextureHandle current_color_texture;

void TemporalAntiAliasingPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {

    previous_history_texture_index = current_history_texture_index;
    current_history_texture_index = ( current_history_texture_index + 1 ) % 2;

    // TODO: fix.
    temp_taa_output = history_textures[ current_history_texture_index ];

    FrameGraphResource* resource = frame_graph->get_resource( "final" );
    if ( resource ) {
        current_color_texture = resource->resource_info.texture.handle;
    }

    gpu_commands->issue_texture_barrier( history_textures[ current_history_texture_index ], RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

    gpu_commands->bind_pipeline( taa_pipeline );
    gpu_commands->bind_descriptor_set( &taa_descriptor_set, 1, nullptr, 0 );
    gpu_commands->dispatch( raptor::ceilu32( renderer->width / 8.0f ), raptor::ceilu32( renderer->height / 8.0f ), 1 );

    gpu_commands->issue_texture_barrier( history_textures[ current_history_texture_index ], RESOURCE_STATE_SHADER_RESOURCE, 0, 1 );
}

void TemporalAntiAliasingPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
}

void TemporalAntiAliasingPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {

    gpu.resize_texture( history_textures[ 0 ], new_width, new_height );
    gpu.resize_texture( history_textures[ 1 ], new_width, new_height );
}

void TemporalAntiAliasingPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {

    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "temporal_anti_aliasing_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;

    GpuDevice& gpu = *renderer->gpu;

    TextureCreation texture_creation;
    texture_creation.reset().set_name( "history_texture_0" ).set_size( gpu.swapchain_width, gpu.swapchain_height, 1 )
        .set_flags( TextureFlags::Compute_mask ).set_format_type( VK_FORMAT_R16G16B16A16_SFLOAT, TextureType::Texture2D );
    history_textures[ 0 ] = gpu.create_texture( texture_creation );

    texture_creation.set_name( "history_texture_1" );
    history_textures[ 1 ] = gpu.create_texture( texture_creation );

    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuTaaConstants ) ).set_name("taa_constants");
    taa_constants = gpu.create_buffer( buffer_creation );

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "fullscreen" ) );
    if ( technique ) {
        u32 pass_index = technique->get_pass_index( "temporal_aa" );
        GpuTechniquePass& pass = technique->passes[ pass_index ];

        taa_pipeline = pass.pipeline;

        DescriptorSetLayoutHandle common_layout = gpu.get_descriptor_set_layout( taa_pipeline, k_material_descriptor_set_index );

        DescriptorSetCreation ds_creation{};
        ds_creation.reset().set_layout( common_layout );
        ds_creation.buffer( taa_constants, 50 );
        scene.add_scene_descriptors( ds_creation, pass );
        taa_descriptor_set = gpu.create_descriptor_set( ds_creation );
    }
}

void TemporalAntiAliasingPass::upload_gpu_data( RenderScene& scene ) {

    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    // Update per mesh material buffer
    // TODO: update only changed stuff, this is now dynamic so it can't be done.
    MapBufferParameters cb_map = { taa_constants, 0, 0 };
    GpuTaaConstants* gpu_constants = ( GpuTaaConstants* )gpu.map_buffer( cb_map );
    if ( gpu_constants ) {

        gpu_constants->history_color_texture_index = history_textures[ previous_history_texture_index ].index;
        gpu_constants->taa_output_texture_index = history_textures[ current_history_texture_index ].index;
        gpu_constants->velocity_texture_index = scene.motion_vector_texture.index;
        gpu_constants->current_color_texture_index = current_color_texture.index;

        gpu_constants->taa_modes = scene.taa_mode;
        gpu_constants->options = ( ( scene.taa_use_inverse_luminance_filtering ? 1 : 0) ) |
                                 ( ( scene.taa_use_temporal_filtering ? 1 : 0) << 1 ) |
                                 ( ( scene.taa_use_luminance_difference_filtering ? 1 : 0 ) << 2 ) |
                                 ( ( scene.taa_use_ycocg ? 1 : 0 ) << 3 );

        gpu_constants->current_color_filter = scene.taa_current_color_filter;
        gpu_constants->history_sampling_filter = scene.taa_history_sampling_filter;
        gpu_constants->history_constraint_mode = scene.taa_history_constraint_mode;
        gpu_constants->velocity_sampling_mode = scene.taa_velocity_sampling_mode;

        gpu.unmap_buffer( cb_map );
    }
}

void TemporalAntiAliasingPass::free_gpu_resources( GpuDevice& gpu ) {

    gpu.destroy_buffer( taa_constants );
    gpu.destroy_descriptor_set( taa_descriptor_set );
    gpu.destroy_texture( history_textures[ 0 ] );
    gpu.destroy_texture( history_textures[ 1 ] );
}

void TemporalAntiAliasingPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {
}


// MotionVectorPass ///////////////////////////////////////////////////////
void MotionVectorPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    //gpu_commands->issue_texture_barrier( render_scene->motion_vector_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

    gpu_commands->bind_pipeline( camera_composite_pipeline );
    gpu_commands->bind_descriptor_set( &camera_composite_descriptor_set, 1, nullptr, 0 );
    gpu_commands->dispatch( raptor::ceilu32( renderer->width / 8.0f ), raptor::ceilu32( renderer->height / 8.0f ), 1 );

    //gpu_commands->issue_texture_barrier( render_scene->motion_vector_texture, RESOURCE_STATE_SHADER_RESOURCE, 0, 1 );
}

void MotionVectorPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;
}

void MotionVectorPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {
    if ( !enabled )
        return;
}

void MotionVectorPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "motion_vector_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;

    GpuDevice& gpu = *renderer->gpu;

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "fullscreen" ) );
    if ( technique ) {
        FrameGraphResource* gubffer_normals_resource = frame_graph->get_resource( "gbuffer_normals" );
        RASSERT( gubffer_normals_resource != nullptr );

        u32 pass_index = technique->get_pass_index( "composite_camera_motion" );
        GpuTechniquePass& pass = technique->passes[ pass_index ];

        camera_composite_pipeline = pass.pipeline;

        DescriptorSetLayoutHandle common_layout = gpu.get_descriptor_set_layout( camera_composite_pipeline, k_material_descriptor_set_index );

        DescriptorSetCreation ds_creation{};
        ds_creation.reset().set_layout( common_layout );
        ds_creation.texture( scene.motion_vector_texture, 51 );
        ds_creation.texture( scene.visibility_motion_vector_texture, 52 );
        ds_creation.texture( gubffer_normals_resource->resource_info.texture.handle, 53 );
        scene.add_scene_descriptors( ds_creation, pass );
        camera_composite_descriptor_set = gpu.create_descriptor_set( ds_creation );
    }
}

void MotionVectorPass::upload_gpu_data( RenderScene& scene ) {
    if ( !enabled )
        return;
}

void MotionVectorPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled )
        return;

    gpu.destroy_descriptor_set( camera_composite_descriptor_set );
}

void MotionVectorPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    gpu.destroy_descriptor_set( camera_composite_descriptor_set );

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "fullscreen" ) );
    if ( technique ) {
        FrameGraphResource* gubffer_normals_resource = frame_graph->get_resource( "gbuffer_normals" );
        RASSERT( gubffer_normals_resource != nullptr );

        u32 pass_index = technique->get_pass_index( "composite_camera_motion" );
        GpuTechniquePass& pass = technique->passes[ pass_index ];

        DescriptorSetLayoutHandle common_layout = gpu.get_descriptor_set_layout( camera_composite_pipeline, k_material_descriptor_set_index );
        DescriptorSetCreation ds_creation{};
        ds_creation.reset().set_layout( common_layout );
        ds_creation.texture( render_scene->motion_vector_texture, 51 );
        ds_creation.texture( render_scene->visibility_motion_vector_texture, 52 );
        ds_creation.texture( gubffer_normals_resource->resource_info.texture.handle, 53 );
        render_scene->add_scene_descriptors( ds_creation, pass );
        camera_composite_descriptor_set = gpu.create_descriptor_set( ds_creation );
    }
}

// IndirectPass ///////////////////////////////////////////////////////////

struct alignas(16) GpuDDGIConstants {
    u32         radiance_output_index;
    u32         grid_irradiance_output_index;
    u32         indirect_output_index;
    u32         normal_texture_index;

    u32         depth_pyramid_texture_index;
    u32         depth_fullscreen_texture_index;
    u32         grid_visibility_texture_index;
    u32         probe_offset_texture_index;

    f32         hysteresis;
    f32         infinte_bounces_multiplier;
    i32         probe_update_offset;
    i32         probe_update_count;

    vec3s       probe_grid_position;
    f32         probe_sphere_scale;

    vec3s       probe_spacing;
    f32         max_probe_offset;   // [0,0.5] max offset for probes

    vec3s       reciprocal_probe_spacing;
    f32         self_shadow_bias;

    i32         probe_counts[ 3 ];
    u32         debug_options;

    i32         irradiance_texture_width;
    i32         irradiance_texture_height;
    i32         irradiance_side_length;
    i32         probe_rays;

    i32         visibility_texture_width;
    i32         visibility_texture_height;
    i32         visibility_side_length;
    u32         pad1;

    mat4s       random_rotation;
}; // struct DDGIConstants

void IndirectPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {

}

void IndirectPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {

    if ( !enabled )
        return;

    static i32 offsets_calculations_count = 24;
    if ( render_scene->gi_recalculate_offsets ) {
        offsets_calculations_count = 24;
    }

    // Probe raytrace
    gpu_commands->push_marker( "RT" );
    gpu_commands->issue_texture_barrier( probe_raytrace_radiance_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->bind_pipeline( probe_raytrace_pipeline );
    gpu_commands->bind_descriptor_set( &probe_raytrace_descriptor_set, 1, nullptr, 0 );
    
    // When calculating offsets, needs all the probes to be updated.
    const u32 probe_count = offsets_calculations_count >= 0 ? get_total_probes() : per_frame_probe_updates;
    gpu_commands->trace_rays( probe_raytrace_pipeline, probe_rays, probe_count, 1 );

    gpu_commands->issue_texture_barrier( probe_raytrace_radiance_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->pop_marker();

    // Calculate probe offsets
    if ( offsets_calculations_count >= 0 ) {
        --offsets_calculations_count;
        gpu_commands->push_marker( "Offsets" );

        gpu_commands->issue_texture_barrier( probe_offsets_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
        gpu_commands->bind_pipeline( calculate_probe_offset_pipeline );
        gpu_commands->bind_descriptor_set( &sample_irradiance_descriptor_set, 1, nullptr, 0 );

        u32 first_frame = offsets_calculations_count == 23 ? 1 : 0;
        gpu_commands->push_constants( calculate_probe_offset_pipeline, 0, 4, &first_frame );
        gpu_commands->dispatch( raptor::ceilu32( probe_count / 32.f ), 1, 1 );
        gpu_commands->pop_marker();
    }

    gpu_commands->push_marker( "Statuses" );

    gpu_commands->issue_texture_barrier( probe_offsets_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->bind_pipeline( calculate_probe_statuses_pipeline );
    gpu_commands->bind_descriptor_set( &sample_irradiance_descriptor_set, 1, nullptr, 0 );

    u32 first_frame = 0;
    gpu_commands->push_constants( calculate_probe_statuses_pipeline, 0, 4, &first_frame );
    gpu_commands->dispatch( raptor::ceilu32( probe_count / 32.f ), 1, 1 );
    gpu_commands->pop_marker();

    gpu_commands->push_marker( "Blend Irr" );
    // Probe grid update: irradiance
    gpu_commands->issue_texture_barrier( probe_grid_irradiance_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

    gpu_commands->bind_pipeline( probe_grid_update_irradiance_pipeline );
    gpu_commands->bind_descriptor_set( &probe_grid_update_descriptor_set, 1, nullptr, 0 );
    gpu_commands->dispatch( raptor::ceilu32( irradiance_atlas_width / 8.f ),
                            raptor::ceilu32( irradiance_atlas_height / 8.f ), 1 );

    gpu_commands->pop_marker();

    gpu_commands->push_marker( "Blend Vis" );
    // Probe grid update: visibility
    gpu_commands->issue_texture_barrier( probe_grid_visibility_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

    gpu_commands->bind_pipeline( probe_grid_update_visibility_pipeline );
    gpu_commands->bind_descriptor_set( &probe_grid_update_descriptor_set, 1, nullptr, 0 );
    gpu_commands->dispatch( raptor::ceilu32( visibility_atlas_width / 8.f ),
                            raptor::ceilu32( visibility_atlas_height / 8.f ), 1 );

    gpu_commands->issue_texture_barrier( probe_grid_irradiance_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->issue_texture_barrier( probe_grid_visibility_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );

    gpu_commands->pop_marker();
    gpu_commands->global_debug_barrier();

    gpu_commands->push_marker( "Sample Irr" );
    // Sample irradiance
    gpu_commands->issue_texture_barrier( indirect_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->bind_pipeline( sample_irradiance_pipeline );
    gpu_commands->bind_descriptor_set( &sample_irradiance_descriptor_set, 1, nullptr, 0 );
    u32 half_resolution = render_scene->gi_use_half_resolution ? 1 : 0;
    gpu_commands->push_constants( sample_irradiance_pipeline, 0, 4, &half_resolution );

    const f32 resolution_divider = render_scene->gi_use_half_resolution ? 0.5f : 1.0f;
    gpu_commands->dispatch( raptor::ceilu32( renderer->width * resolution_divider / 8.0f ), raptor::ceilu32( renderer->height * resolution_divider / 8.0f ), 1 );

    gpu_commands->issue_texture_barrier( indirect_texture, RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, 1 );
    gpu_commands->pop_marker();
}

void IndirectPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {

    if ( !enabled ) {
        return;
    }

    new_width = half_resolution_output ? new_width / 2 : new_width;
    new_height = half_resolution_output ? new_height / 2 : new_height;
    gpu.resize_texture( indirect_texture, new_width, new_height );
}

void IndirectPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "indirect_lighting_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled ) {
        return;
    }

    GpuDevice& gpu = *renderer->gpu;

    per_frame_probe_updates = scene.gi_per_frame_probes_update;

    const u32 num_probes = get_total_probes();
    // Cache count of probes for debug probe spheres drawing.
    scene.gi_total_probes = num_probes;

    BufferCreation buffer_creation{};
    buffer_creation.set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuDDGIConstants ) ).set_name( "ddgi_constants" );
    ddgi_constants_buffer = gpu.create_buffer( buffer_creation );
    // Cache constant buffer used when drawing debug probe spheres.
    scene.ddgi_constants_cache = ddgi_constants_buffer;

    buffer_creation.set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, sizeof( u32 ) * num_probes ).set_name( "ddgi_probe_status" );
    ddgi_probe_status_buffer = gpu.create_buffer( buffer_creation );
    // Cache status buffer
    scene.ddgi_probe_status_cache = ddgi_probe_status_buffer;

    half_resolution_output = scene.gi_use_half_resolution;

    // Create external texture used as pass output.
    // Having normal attachment will cause a crash in vmaCreateAliasingImage.
    TextureCreation texture_creation{ };
    u32 adjusted_width = scene.gi_use_half_resolution ? ( renderer->width ) / 2 : renderer->width;
    u32 adjusted_height = scene.gi_use_half_resolution ? ( renderer->height ) / 2 : renderer->height;
    texture_creation.set_size( adjusted_width, adjusted_height, 1 ).set_format_type( VK_FORMAT_R16G16B16A16_SFLOAT, TextureType::Texture2D ).set_mips( 1 ).set_layers( 1 ).set_flags( TextureFlags::Compute_mask ).set_name( "indirect_texture" );

    indirect_texture = gpu.create_texture( texture_creation );

    FrameGraphResource* resource = frame_graph->get_resource( "indirect_lighting" );
    resource->resource_info.set_external_texture_2d( adjusted_width, adjusted_height, VK_FORMAT_R16G16B16A16_SFLOAT, 0, indirect_texture );

    // Radiance texture
    const u32 num_rays = probe_rays;
    texture_creation.set_size( num_rays, num_probes, 1 ).set_format_type( VK_FORMAT_R16G16B16A16_SFLOAT, TextureType::Texture2D )
                    .set_flags( TextureFlags::Compute_mask ).set_name( "probe_rt_radiance" );
    probe_raytrace_radiance_texture = gpu.create_texture( texture_creation );

    // Irradiance texture, 6x6 plus additional 2 pixel border to allow bilinear interpolation
    const i32 octahedral_irradiance_size = irradiance_probe_size + 2;
    irradiance_atlas_width = ( octahedral_irradiance_size * probe_count_x * probe_count_y );
    irradiance_atlas_height = ( octahedral_irradiance_size * probe_count_z );
    texture_creation.set_size( irradiance_atlas_width, irradiance_atlas_height, 1 ).set_name( "probe_irradiance" );
    probe_grid_irradiance_texture = gpu.create_texture( texture_creation );

    // Visibility texture
    const i32 octahedral_visibility_size = visibility_probe_size + 2;
    visibility_atlas_width = ( octahedral_visibility_size * probe_count_x * probe_count_y );
    visibility_atlas_height = ( octahedral_visibility_size * probe_count_z );
    texture_creation.set_format_type( VK_FORMAT_R16G16_SFLOAT, TextureType::Texture2D ).set_size( visibility_atlas_width, visibility_atlas_height, 1 ).set_name( "probe_visibility" );
    probe_grid_visibility_texture = gpu.create_texture( texture_creation );

    // Probe offsets texture
    texture_creation.set_format_type( VK_FORMAT_R16G16B16A16_SFLOAT, TextureType::Texture2D ).set_size( probe_count_x * probe_count_y, probe_count_z, 1 ).set_name( "probe_offsets" );
    probe_offsets_texture = gpu.create_texture( texture_creation );

    // Cache normals texture
    resource = frame_graph->get_resource( "gbuffer_normals" );
    normals_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "depth" );
    depth_fullscreen_texture = resource->resource_info.texture.handle;

    // TODO: at this point this resource is not created still.
    // Use manual assignment in FrameRenderer::upload_gpu_data as occlusion passes.
    //resource = frame_graph->get_resource( "depth_pyramid" );
    //depth_pyramid_texture = resource->resource_info.texture.handle;

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "ddgi" ) );
    if ( technique ) {
        // Probe raytracing
        u32 pass_index = technique->get_pass_index( "probe_rt" );
        GpuTechniquePass& pass = technique->passes[ pass_index ];

        probe_raytrace_pipeline = pass.pipeline;

        DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( probe_raytrace_pipeline, k_material_descriptor_set_index );
        DescriptorSetCreation ds_creation{};
        ds_creation.reset().set_layout( layout ).set_as( scene.tlas, 26 ).buffer( ddgi_constants_buffer, 55 )
                   .buffer( scene.lights_list_sb, 27).buffer( ddgi_probe_status_buffer, 43 );
        scene.add_scene_descriptors( ds_creation, pass );
        scene.add_mesh_descriptors( ds_creation, pass );

        probe_raytrace_descriptor_set = gpu.create_descriptor_set( ds_creation );

        // Probe update irradiance
        pass_index = technique->get_pass_index( "probe_update_irradiance" );
        GpuTechniquePass& pass1 = technique->passes[ pass_index ];

        probe_grid_update_irradiance_pipeline = pass1.pipeline;

        layout = gpu.get_descriptor_set_layout( probe_grid_update_irradiance_pipeline, k_material_descriptor_set_index );
        ds_creation.reset().set_layout( layout ).buffer(ddgi_constants_buffer, 55).buffer( ddgi_probe_status_buffer, 43 )
                   .texture(probe_grid_irradiance_texture, 41).texture(probe_grid_visibility_texture, 42);
        scene.add_scene_descriptors( ds_creation, pass1 );
        probe_grid_update_descriptor_set = gpu.create_descriptor_set( ds_creation );

        // Probe update visibility
        pass_index = technique->get_pass_index( "probe_update_visibility" );
        GpuTechniquePass& pass2 = technique->passes[ pass_index ];

        probe_grid_update_visibility_pipeline = pass2.pipeline;

        // Calculate probe offsets
        pass_index = technique->get_pass_index( "calculate_probe_offsets" );
        GpuTechniquePass& pass3 = technique->passes[ pass_index ];

        calculate_probe_offset_pipeline = pass3.pipeline;

        // Calculate probe statuses. Used after initial probe offsets
        pass_index = technique->get_pass_index( "calculate_probe_statuses" );
        GpuTechniquePass& pass4 = technique->passes[ pass_index ];

        calculate_probe_statuses_pipeline = pass4.pipeline;

        // Sample irradiance
        pass_index = technique->get_pass_index( "sample_irradiance" );
        GpuTechniquePass& pass5 = technique->passes[ pass_index ];

        sample_irradiance_pipeline = pass5.pipeline;

        layout = gpu.get_descriptor_set_layout( sample_irradiance_pipeline, k_material_descriptor_set_index );
        ds_creation.reset().set_layout( layout ).buffer( ddgi_constants_buffer, 55 ).buffer( ddgi_probe_status_buffer, 43 );
        scene.add_scene_descriptors( ds_creation, pass5 );
        sample_irradiance_descriptor_set = gpu.create_descriptor_set( ds_creation );
    }
}

void IndirectPass::upload_gpu_data( RenderScene& scene ) {

    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    MapBufferParameters cb_map = { ddgi_constants_buffer, 0, 0 };
    GpuDDGIConstants* gpu_constants = ( GpuDDGIConstants* )gpu.map_buffer( cb_map );
    if ( gpu_constants ) {
        gpu_constants->radiance_output_index = probe_raytrace_radiance_texture.index;
        gpu_constants->grid_irradiance_output_index = probe_grid_irradiance_texture.index;
        gpu_constants->indirect_output_index = indirect_texture.index;
        gpu_constants->normal_texture_index = normals_texture.index;

        gpu_constants->depth_pyramid_texture_index = depth_pyramid_texture.index;
        gpu_constants->depth_fullscreen_texture_index = depth_fullscreen_texture.index;
        gpu_constants->grid_visibility_texture_index = probe_grid_visibility_texture.index;
        gpu_constants->probe_offset_texture_index = probe_offsets_texture.index;

        gpu_constants->probe_grid_position = scene.gi_probe_grid_position;
        gpu_constants->probe_sphere_scale = scene.gi_probe_sphere_scale;

        gpu_constants->hysteresis = scene.gi_hysteresis;
        gpu_constants->infinte_bounces_multiplier = scene.gi_infinite_bounces_multiplier;
        gpu_constants->max_probe_offset = scene.gi_max_probe_offset;

        gpu_constants->probe_spacing = scene.gi_probe_spacing;
        gpu_constants->reciprocal_probe_spacing = { 1.f / scene.gi_probe_spacing.x, 1.f / scene.gi_probe_spacing.y, 1.f / scene.gi_probe_spacing.z };
        gpu_constants->self_shadow_bias = scene.gi_self_shadow_bias;

        gpu_constants->probe_counts[ 0 ] = probe_count_x;
        gpu_constants->probe_counts[ 1 ] = probe_count_y;
        gpu_constants->probe_counts[ 2 ] = probe_count_z;
        gpu_constants->debug_options = ( (scene.gi_debug_border ? 1 : 0) )
                                     | ( (scene.gi_debug_border_type ? 1 : 0 ) << 1 )
                                     | ( (scene.gi_debug_border_source ? 1 : 0 ) << 2 )
                                     | ( ( scene.gi_use_visibility ? 1 : 0 ) << 3 )
                                     | ( ( scene.gi_use_backface_smoothing ? 1 : 0 ) << 4 )
                                     | ( ( scene.gi_use_perceptual_encoding ? 1 : 0 ) << 5 )
                                     | ( ( scene.gi_use_backface_blending ? 1 : 0 ) << 6 )
                                     | ( ( scene.gi_use_probe_offsetting ? 1 : 0 ) << 7 )
                                     | ( ( scene.gi_use_probe_status ? 1 : 0 ) << 8 )
                                     | ( ( scene.gi_use_infinite_bounces ? 1 : 0 ) << 9 );

        gpu_constants->irradiance_texture_width = irradiance_atlas_width;
        gpu_constants->irradiance_texture_height = irradiance_atlas_height;
        gpu_constants->irradiance_side_length = irradiance_probe_size;
        gpu_constants->probe_rays = probe_rays;

        gpu_constants->visibility_texture_width = visibility_atlas_width;
        gpu_constants->visibility_texture_height = visibility_atlas_height;
        gpu_constants->visibility_side_length = visibility_probe_size;
        gpu_constants->probe_update_offset = probe_update_offset;
        gpu_constants->probe_update_count = per_frame_probe_updates;

        const f32 rotation_scaler = 0.001f;
        gpu_constants->random_rotation = glms_euler_xyz( { get_random_value( -1,1 ) * rotation_scaler, get_random_value( -1,1 ) * rotation_scaler, get_random_value( -1,1 ) * rotation_scaler } );

        gpu.unmap_buffer( cb_map );

        const u32 num_probes = probe_count_x * probe_count_y * probe_count_z;
        probe_update_offset = ( probe_update_offset + per_frame_probe_updates ) % num_probes;
        per_frame_probe_updates = scene.gi_per_frame_probes_update;
    }
}

void IndirectPass::free_gpu_resources( GpuDevice& gpu ) {

    gpu.destroy_buffer( ddgi_constants_buffer );
    gpu.destroy_buffer( ddgi_probe_status_buffer );
    gpu.destroy_descriptor_set( probe_raytrace_descriptor_set );
    gpu.destroy_texture( probe_raytrace_radiance_texture );
    gpu.destroy_descriptor_set( probe_grid_update_descriptor_set );
    gpu.destroy_texture( probe_grid_irradiance_texture );
    gpu.destroy_texture( probe_grid_visibility_texture );
    gpu.destroy_texture( probe_offsets_texture );
    gpu.destroy_descriptor_set( sample_irradiance_descriptor_set );
    gpu.destroy_texture( indirect_texture );
}

void IndirectPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {
}

// ReflectionsPass ///////////////////////////////////////////////////////////
void ReflectionsPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled )
        return;
}

void ReflectionsPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled )
        return;

    // TODO(marco): clear
    gpu_commands->issue_texture_barrier( reflections_texture, RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
    gpu_commands->bind_pipeline( reflections_pipeline );
    gpu_commands->bind_descriptor_set( &reflections_descriptor_set, 1, nullptr, 0 );

    gpu_commands->trace_rays( reflections_pipeline, renderer->width, renderer->height, 1 );
}

void ReflectionsPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {
    if ( !enabled )
        return;

    gpu.resize_texture( reflections_texture, new_width, new_height );
}

void ReflectionsPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "reflections_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled ) {
        return;
    }

    GpuDevice& gpu = *renderer->gpu;

    BufferCreation buffer_creation{};
    buffer_creation.set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuReflectionsConstants ) ).set_name( "reflections_constants" );
    reflections_constants_buffer = gpu.create_buffer( buffer_creation );

    // Cache normals texture
    FrameGraphResource* resource = frame_graph->get_resource( "gbuffer_normals" );
    normals_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "gbuffer_occlusion_roughness_metalness" );
    roughness_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "indirect_lighting" );
    indirect_texture = resource->resource_info.texture.handle;

    TextureCreation texture_creation{ };
    u32 adjusted_width = renderer->width;
    u32 adjusted_height = renderer->height;
    texture_creation.set_size( adjusted_width, adjusted_height, 1 ).set_format_type( VK_FORMAT_B10G11R11_UFLOAT_PACK32, TextureType::Texture2D ).set_mips( 1 ).set_layers( 1 ).set_flags( TextureFlags::Compute_mask ).set_name( "reflections_texture" );

    reflections_texture = gpu.create_texture( texture_creation );

    resource = frame_graph->get_resource( "reflections" );
    resource->resource_info.set_external_texture_2d( adjusted_width, adjusted_height, VK_FORMAT_B10G11R11_UFLOAT_PACK32, 0, reflections_texture );

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "reflections" ) );
    if ( technique ) {
        // Probe raytracing
        u32 pass_index = technique->get_pass_index( "reflections_rt" );
        GpuTechniquePass& pass = technique->passes[ pass_index ];

        reflections_pipeline = pass.pipeline;

        DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( reflections_pipeline, k_material_descriptor_set_index );
        DescriptorSetCreation ds_creation{};
        ds_creation.reset().set_layout( layout ).buffer( reflections_constants_buffer, 40 );
        scene.add_scene_descriptors( ds_creation, pass );
        scene.add_mesh_descriptors( ds_creation, pass );
        scene.add_lighting_descriptors( ds_creation, pass, 0 );
        scene.add_debug_descriptors( ds_creation, pass );

        reflections_descriptor_set = gpu.create_descriptor_set( ds_creation );
    }
}

void ReflectionsPass::upload_gpu_data( RenderScene& scene ) {
    if ( !enabled )
        return;

    GpuDevice& gpu = *renderer->gpu;

    MapBufferParameters cb_map = { reflections_constants_buffer, 0, 0 };
    GpuReflectionsConstants* gpu_constants = ( GpuReflectionsConstants* )gpu.map_buffer( cb_map );
    if ( gpu_constants ) {
        gpu_constants->sbt_offset = 0;
        gpu_constants->sbt_stride = renderer->gpu->ray_tracing_pipeline_properties.shaderGroupHandleAlignment;
        gpu_constants->miss_index = 0;
        gpu_constants->out_image_index = reflections_texture.index;

        gpu_constants->gbuffer_texures[ 0 ] = roughness_texture.index;
        gpu_constants->gbuffer_texures[ 1 ] = normals_texture.index;
        gpu_constants->gbuffer_texures[ 2 ] = indirect_texture.index;

        gpu.unmap_buffer( cb_map );
    }
}

void ReflectionsPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled )
        return;

    gpu.destroy_texture( reflections_texture );
    gpu.destroy_buffer( reflections_constants_buffer );
    gpu.destroy_descriptor_set( reflections_descriptor_set );
}

void ReflectionsPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled )
        return;
}

// SVGFAccumulationPass ///////////////////////////////////////////////////////////
void SVGFAccumulationPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled ) {
        return;
    }
}

void SVGFAccumulationPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled ) {
        return;
    }

    gpu_commands->bind_pipeline( pipeline );
    gpu_commands->bind_descriptor_set( &descriptor_set, 1, nullptr, 0 );

    gpu_commands->dispatch( raptor::ceilu32( renderer->width / 8.0f ), raptor::ceilu32( renderer->height / 8.0f ), 1 );
}

void SVGFAccumulationPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {
    if ( !enabled ) {
        return;
    }

    gpu.resize_texture( last_frame_normals_texture, new_width, new_height );
    gpu.resize_texture( last_frame_mesh_id_texture, new_width, new_height );
    gpu.resize_texture( last_frame_depth_texture, new_width, new_height );
    gpu.resize_texture( reflections_history_texture, new_width, new_height );
    gpu.resize_texture( moments_history_texture, new_width, new_height );
}

void SVGFAccumulationPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "svgf_accumulation_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled ) {
        return;
    }

    GpuDevice& gpu = *renderer->gpu;

    BufferCreation buffer_creation{};
    buffer_creation.set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuConstants ) ).set_name( "svgf_accumulation_constants" );
    gpu_constants = gpu.create_buffer( buffer_creation );

    // NOTE(marco): cache textures from previous passes
    FrameGraphResource* resource = frame_graph->get_resource( "gbuffer_normals" );
    normals_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "depth" );
    depth_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "mesh_id" );
    mesh_id_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "motion_vectors" );
    motion_vectors_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "reflections" );
    reflections_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "depth_normal_dd" );
    depth_normal_dd_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "integrated_reflection_color" );
    integrated_color_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "integrated_moments" );
    integrated_moments_texture = resource->resource_info.texture.handle;

    TextureCreation texture_creation{ };
    u32 adjusted_width = renderer->width;
    u32 adjusted_height = renderer->height;
    texture_creation.set_size( adjusted_width, adjusted_height, 1 ).set_format_type( VK_FORMAT_B10G11R11_UFLOAT_PACK32, TextureType::Texture2D ).set_mips( 1 ).set_layers( 1 ).set_flags( TextureFlags::Compute_mask ).set_name( "reflections_history_texture" );

    reflections_history_texture = gpu.create_texture( texture_creation );

    resource = frame_graph->get_resource( "reflections_history" );
    resource->resource_info.set_external_texture_2d( adjusted_width, adjusted_height, VK_FORMAT_B10G11R11_UFLOAT_PACK32, 0, reflections_history_texture );

    texture_creation.set_format_type( VK_FORMAT_R16G16_SFLOAT, TextureType::Texture2D ).set_name( "moments_history" );
    moments_history_texture = gpu.create_texture( texture_creation );
    resource = frame_graph->get_resource( "moments_history" );
    resource->resource_info.set_external_texture_2d( adjusted_width, adjusted_height, VK_FORMAT_R16G16_SFLOAT, 0, moments_history_texture );

    texture_creation.set_name( "normals_history" );
    last_frame_normals_texture = gpu.create_texture( texture_creation );
    resource = frame_graph->get_resource( "normals_history" );
    resource->resource_info.set_external_texture_2d( adjusted_width, adjusted_height, VK_FORMAT_R16G16_SFLOAT, 0, last_frame_normals_texture );

    texture_creation.set_format_type( VK_FORMAT_R32_UINT, TextureType::Texture2D ).set_name( "mesh_id_history" );
    last_frame_mesh_id_texture = gpu.create_texture( texture_creation );
    resource = frame_graph->get_resource( "mesh_id_history" );
    resource->resource_info.set_external_texture_2d( adjusted_width, adjusted_height, VK_FORMAT_R32_UINT, 0, last_frame_mesh_id_texture );

    texture_creation.set_format_type( VK_FORMAT_D32_SFLOAT, TextureType::Texture2D).set_flags( 0 ).set_name( "depth_history" );
    last_frame_depth_texture = gpu.create_texture( texture_creation );
    resource = frame_graph->get_resource( "depth_history" );
    resource->resource_info.set_external_texture_2d( adjusted_width, adjusted_height, VK_FORMAT_D32_SFLOAT, 0, last_frame_depth_texture );

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "reflections" ) );
    if ( technique ) {
        // Probe raytracing
        u32 pass_index = technique->get_pass_index( "svgf_accumulation" );
        GpuTechniquePass& pass = technique->passes[ pass_index ];

        pipeline = pass.pipeline;

        DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( pipeline, k_material_descriptor_set_index );
        DescriptorSetCreation ds_creation{};
        ds_creation.reset().set_layout( layout ).buffer( gpu_constants, 40 );
        scene.add_scene_descriptors( ds_creation, pass );

        descriptor_set = gpu.create_descriptor_set( ds_creation );
    }
}

void SVGFAccumulationPass::upload_gpu_data( RenderScene& scene ) {
    if ( !enabled ) {
        return;
    }

    GpuDevice& gpu = *renderer->gpu;

    MapBufferParameters cb_map = { gpu_constants, 0, 0 };
    GpuConstants* gpu_constants = ( GpuConstants* )gpu.map_buffer( cb_map );
    if ( gpu_constants ) {
        gpu_constants->motion_vectors_texture_index = motion_vectors_texture.index;
        gpu_constants->mesh_id_texture_index = mesh_id_texture.index;
        gpu_constants->normals_texture_index = normals_texture.index;
        gpu_constants->depth_normal_dd_texture_index = depth_normal_dd_texture.index;
        gpu_constants->history_mesh_id_texture_index = last_frame_mesh_id_texture.index;
        gpu_constants->history_normals_texture_index = last_frame_normals_texture.index;
        gpu_constants->history_depth_texture = last_frame_depth_texture.index;
        gpu_constants->reflections_texture_index = reflections_texture.index;
        gpu_constants->history_reflections_texture_index = reflections_history_texture.index;
        gpu_constants->history_moments_texture_index = moments_history_texture.index;
        gpu_constants->integrated_color_texture_index = integrated_color_texture.index;
        gpu_constants->integrated_moments_texture_index = integrated_moments_texture.index;

        // NOTE(marco): unused
        gpu_constants->variance_texture_index = 0;
        gpu_constants->filtered_color_texture_index = 0;
        gpu_constants->updated_variance_texture_index = 0;

        gpu.unmap_buffer( cb_map );
    }
}

void SVGFAccumulationPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled ) {
        return;
    }

    gpu.destroy_texture( last_frame_normals_texture );
    gpu.destroy_texture( last_frame_depth_texture );
    gpu.destroy_texture( last_frame_mesh_id_texture );
    gpu.destroy_texture( reflections_history_texture );
    gpu.destroy_texture( moments_history_texture );
    gpu.destroy_buffer( gpu_constants );
    gpu.destroy_descriptor_set( descriptor_set );
}

void SVGFAccumulationPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {

}

// SVGFVariancePass ///////////////////////////////////////////////////////////
void SVGFVariancePass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled ) {
        return;
    }
}

void SVGFVariancePass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled ) {
        return;
    }

    gpu_commands->bind_pipeline( pipeline );
    gpu_commands->bind_descriptor_set( &descriptor_set, 1, nullptr, 0 );

    gpu_commands->dispatch( raptor::ceilu32( renderer->width / 8.0f ), raptor::ceilu32( renderer->height / 8.0f ), 1 );

    // NOTE(marco): copy history textures
    gpu_commands->copy_texture( normals_texture, last_frame_normals_texture, ResourceState::RESOURCE_STATE_GENERIC_READ );
    gpu_commands->copy_texture( mesh_id_texture, last_frame_mesh_id_texture, ResourceState::RESOURCE_STATE_GENERIC_READ );
    gpu_commands->copy_texture( depth_texture, last_frame_depth_texture, ResourceState::RESOURCE_STATE_GENERIC_READ );
    gpu_commands->copy_texture( integrated_moments_texture, moments_history_texture, ResourceState::RESOURCE_STATE_GENERIC_READ );
}

void SVGFVariancePass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {
    if ( !enabled ) {
        return;
    }
}

void SVGFVariancePass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "svgf_variance_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled ) {
        return;
    }

    GpuDevice& gpu = *renderer->gpu;

    BufferCreation buffer_creation{};
    buffer_creation.set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuConstants ) ).set_name( "svgf_accumulation_constants" );
    gpu_constants = gpu.create_buffer( buffer_creation );

    // NOTE(marco): cache textures from previous passes
    FrameGraphResource* resource = frame_graph->get_resource( "gbuffer_normals" );
    normals_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "depth" );
    depth_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "mesh_id" );
    mesh_id_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "motion_vectors" );
    motion_vectors_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "reflections" );
    reflections_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "depth_normal_dd" );
    depth_normal_dd_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "integrated_reflection_color" );
    integrated_color_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "integrated_moments" );
    integrated_moments_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "svgf_variance" );
    variance_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "reflections_history" );
    reflections_history_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "moments_history" );
    moments_history_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "normals_history" );
    last_frame_normals_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "mesh_id_history" );
    last_frame_mesh_id_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "depth_history" );
    last_frame_depth_texture = resource->resource_info.texture.handle;

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "reflections" ) );
    if ( technique ) {
        // Probe raytracing
        u32 pass_index = technique->get_pass_index( "svgf_variance" );
        GpuTechniquePass& pass = technique->passes[ pass_index ];

        pipeline = pass.pipeline;

        DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( pipeline, k_material_descriptor_set_index );
        DescriptorSetCreation ds_creation{};
        ds_creation.reset().set_layout( layout ).buffer( gpu_constants, 40 );
        scene.add_scene_descriptors( ds_creation, pass );

        descriptor_set = gpu.create_descriptor_set( ds_creation );
    }
}

void SVGFVariancePass::upload_gpu_data( RenderScene& scene ) {
    if ( !enabled ) {
        return;
    }

    GpuDevice& gpu = *renderer->gpu;

    MapBufferParameters cb_map = { gpu_constants, 0, 0 };
    GpuConstants* gpu_constants = ( GpuConstants* )gpu.map_buffer( cb_map );
    if ( gpu_constants ) {
        gpu_constants->motion_vectors_texture_index = motion_vectors_texture.index;
        gpu_constants->mesh_id_texture_index = mesh_id_texture.index;
        gpu_constants->normals_texture_index = normals_texture.index;
        gpu_constants->depth_normal_dd_texture_index = depth_normal_dd_texture.index;
        gpu_constants->history_mesh_id_texture_index = last_frame_mesh_id_texture.index;
        gpu_constants->history_normals_texture_index = last_frame_normals_texture.index;
        gpu_constants->history_depth_texture = last_frame_depth_texture.index;
        gpu_constants->reflections_texture_index = reflections_texture.index;
        gpu_constants->history_reflections_texture_index = reflections_history_texture.index;
        gpu_constants->history_moments_texture_index = moments_history_texture.index;
        gpu_constants->integrated_color_texture_index = integrated_color_texture.index;
        gpu_constants->integrated_moments_texture_index = integrated_moments_texture.index;
        gpu_constants->variance_texture_index = variance_texture.index;

        // NOTE(marco): unused
        gpu_constants->filtered_color_texture_index = 0;
        gpu_constants->updated_variance_texture_index = 0;

        gpu.unmap_buffer( cb_map );
    }
}

void SVGFVariancePass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled ) {
        return;
    }

    gpu.destroy_buffer( gpu_constants );
    gpu.destroy_descriptor_set( descriptor_set );
}

void SVGFVariancePass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled ) {
        return;
    }
}

// SVGFWaveletPass ///////////////////////////////////////////////////////////
void SVGFWaveletPass::pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled ) {
        return;
    }
}

void SVGFWaveletPass::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {
    if ( !enabled ) {
        return;
    }

    gpu_commands->bind_pipeline( pipeline );
    for ( u32 i = 0; i < k_num_passes; ++i ) {
        gpu_commands->bind_descriptor_set( &descriptor_set[ i ], 1, nullptr, 0 );

        if ( ( i % 2 ) == 0 ) {
            gpu_commands->issue_texture_barrier( integrated_color_texture, ResourceState::RESOURCE_STATE_GENERIC_READ, 0, 1 );
            gpu_commands->issue_texture_barrier( variance_texture, ResourceState::RESOURCE_STATE_GENERIC_READ, 0, 1 );
            gpu_commands->issue_texture_barrier( ping_pong_color_texture, ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
            gpu_commands->issue_texture_barrier( integrated_color_texture, ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
        } else {
            gpu_commands->issue_texture_barrier( integrated_color_texture, ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
            gpu_commands->issue_texture_barrier( variance_texture, ResourceState::RESOURCE_STATE_UNORDERED_ACCESS, 0, 1 );
            gpu_commands->issue_texture_barrier( ping_pong_color_texture, ResourceState::RESOURCE_STATE_GENERIC_READ, 0, 1 );
            gpu_commands->issue_texture_barrier( integrated_color_texture, ResourceState::RESOURCE_STATE_GENERIC_READ, 0, 1 );
        }

        gpu_commands->dispatch( raptor::ceilu32( renderer->width / 8.0f ), raptor::ceilu32( renderer->height / 8.0f ), 1 );

        if ( i == 0 ) {
            gpu_commands->copy_texture( ping_pong_color_texture, reflections_history_texture, ResourceState::RESOURCE_STATE_GENERIC_READ );
        }
    }
}

void SVGFWaveletPass::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {
    if ( !enabled ) {
        return;
    }

    gpu.resize_texture( ping_pong_color_texture, new_width, new_height );
    gpu.resize_texture( ping_pong_variance_texture, new_width, new_height );
}

void SVGFWaveletPass::prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {
    renderer = scene.renderer;

    FrameGraphNode* node = frame_graph->get_node( "svgf_wavelet_pass" );
    if ( node == nullptr ) {
        enabled = false;

        return;
    }

    enabled = node->enabled;
    if ( !enabled ) {
        return;
    }

    GpuDevice& gpu = *renderer->gpu;

    BufferCreation buffer_creation{};
    buffer_creation.set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuConstants ) ).set_name( "svgf_accumulation_constants" );
    for ( u32 i = 0; i < k_num_passes; ++i ) {
        gpu_constants[ i ] = gpu.create_buffer( buffer_creation );
    }

    TextureCreation texture_creation{ };
    u32 adjusted_width = renderer->width;
    u32 adjusted_height = renderer->height;
    texture_creation.set_size( adjusted_width, adjusted_height, 1 ).set_format_type( VK_FORMAT_B10G11R11_UFLOAT_PACK32, TextureType::Texture2D ).set_mips( 1 ).set_layers( 1 ).set_flags( TextureFlags::Compute_mask ).set_name( "ping_pong_color_texture" );

    ping_pong_color_texture = gpu.create_texture( texture_creation );

    texture_creation.set_format_type( VK_FORMAT_R32_SFLOAT, TextureType::Texture2D ).set_name( "ping_pong_variance_texture" );
    ping_pong_variance_texture = gpu.create_texture( texture_creation );

    // NOTE(marco): cache textures from previous passes
    FrameGraphResource* resource = frame_graph->get_resource( "gbuffer_normals" );
    normals_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "depth" );
    depth_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "mesh_id" );
    mesh_id_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "motion_vectors" );
    motion_vectors_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "reflections" );
    reflections_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "depth_normal_dd" );
    depth_normal_dd_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "integrated_reflection_color" );
    integrated_color_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "integrated_moments" );
    integrated_moments_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "svgf_variance" );
    variance_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "reflections_history" );
    reflections_history_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "moments_history" );
    moments_history_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "normals_history" );
    last_frame_normals_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "mesh_id_history" );
    last_frame_mesh_id_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "depth_history" );
    last_frame_depth_texture = resource->resource_info.texture.handle;

    resource = frame_graph->get_resource( "svgf_output" );
    resource->resource_info.set_external_texture_2d( adjusted_width, adjusted_height, VK_FORMAT_B10G11R11_UFLOAT_PACK32, 0, ping_pong_color_texture );

    GpuTechnique* technique = renderer->resource_cache.techniques.get( hash_calculate( "reflections" ) );
    if ( technique ) {
        // Probe raytracing
        u32 pass_index = technique->get_pass_index( "svgf_wavelet" );
        GpuTechniquePass& pass = technique->passes[ pass_index ];

        pipeline = pass.pipeline;

        DescriptorSetLayoutHandle layout = gpu.get_descriptor_set_layout( pipeline, k_material_descriptor_set_index );
        DescriptorSetCreation ds_creation{};

        for ( u32 i = 0; i < k_num_passes; ++i ) {
            ds_creation.reset().set_layout( layout ).buffer( gpu_constants[ i ], 40 );
            scene.add_scene_descriptors( ds_creation, pass );

            descriptor_set[ i ] = gpu.create_descriptor_set( ds_creation );
        }
    }
}

void SVGFWaveletPass::upload_gpu_data( RenderScene& scene ) {
    if ( !enabled ) {
        return;
    }

    GpuDevice& gpu = *renderer->gpu;

    for ( u32 i = 0; i < k_num_passes; ++i ) {
        MapBufferParameters cb_map = { gpu_constants[ i ], 0, 0 };
        GpuConstants* gpu_constants = ( GpuConstants* )gpu.map_buffer( cb_map );
        if ( gpu_constants ) {
            gpu_constants->motion_vectors_texture_index = motion_vectors_texture.index;
            gpu_constants->mesh_id_texture_index = mesh_id_texture.index;
            gpu_constants->normals_texture_index = normals_texture.index;
            gpu_constants->depth_normal_dd_texture_index = depth_normal_dd_texture.index;
            gpu_constants->history_mesh_id_texture_index = last_frame_mesh_id_texture.index;
            gpu_constants->history_normals_texture_index = last_frame_normals_texture.index;
            gpu_constants->history_depth_texture = last_frame_depth_texture.index;
            gpu_constants->reflections_texture_index = reflections_texture.index;
            gpu_constants->history_reflections_texture_index = reflections_history_texture.index;
            gpu_constants->history_moments_texture_index = moments_history_texture.index;
            gpu_constants->integrated_moments_texture_index = integrated_moments_texture.index;

            gpu_constants->integrated_color_texture_index = ( i % 2 == 0 ) ? integrated_color_texture.index : ping_pong_color_texture.index;
            gpu_constants->variance_texture_index = ( i % 2 == 0 ) ? variance_texture.index : ping_pong_variance_texture.index;

            gpu_constants->filtered_color_texture_index = ( i % 2 == 1 ) ? integrated_color_texture.index : ping_pong_color_texture.index;
            gpu_constants->updated_variance_texture_index = ( i % 2 == 1 ) ? variance_texture.index : ping_pong_variance_texture.index;

            gpu.unmap_buffer( cb_map );
        }
    }
}

void SVGFWaveletPass::free_gpu_resources( GpuDevice& gpu ) {
    if ( !enabled ) {
        return;
    }

    gpu.destroy_texture( ping_pong_color_texture );
    gpu.destroy_texture( ping_pong_variance_texture );

    for ( u32 i = 0; i < k_num_passes; ++i ) {
        gpu.destroy_buffer( gpu_constants[ i ] );
        gpu.destroy_descriptor_set( descriptor_set[ i ] );
    }
}

void SVGFWaveletPass::update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) {
    if ( !enabled ) {
        return;
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
            copy_gpu_material_data( gpu, gpu_mesh_data[ mesh_index ], meshes[ mesh_index ] );
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
    sorted_lights.init( context.scratch_allocator, active_lights, active_lights );

    // Sort lights based on Z
    mat4s& world_to_camera = scene_data.world_to_camera;
    float z_far = scene_data.z_far;
    for ( u32 i = 0; i < active_lights; ++i ) {
        Light& light = lights[ i ];

        vec4s p{ light.world_position.x, light.world_position.y, light.world_position.z, 1.0f };

        vec4s projected_p = glms_mat4_mulv( world_to_camera, p );
        vec4s projected_p_min = glms_vec4_add( projected_p, { 0,0,-light.radius, 0 } );
        vec4s projected_p_max = glms_vec4_add( projected_p, { 0,0,light.radius, 0 } );

        // NOTE(marco): linearize depth
        SortedLight& sorted_light = sorted_lights[ i ];
        sorted_light.light_index = i;
        // Remove negative numbers as they cause false negatives for bin 0.
        sorted_light.projected_z = (  ( projected_p.z - scene_data.z_near ) / ( z_far - scene_data.z_near ) );
        sorted_light.projected_z_min = (  ( projected_p_min.z - scene_data.z_near ) / ( z_far - scene_data.z_near ) );
        sorted_light.projected_z_max = (  ( projected_p_max.z - scene_data.z_near ) / ( z_far - scene_data.z_near ) );

        //rprint( "Light Z %f, Zmin %f, Zmax %f\n", sorted_light.projected_z, sorted_light.projected_z_min, sorted_light.projected_z_max );
    }

    qsort( sorted_lights.data, active_lights, sizeof( SortedLight ), sorting_light_fn );

    // Upload light list
    cb_map.buffer = lights_list_sb;
    GpuLight* gpu_lights_data = ( GpuLight* )gpu.map_buffer( cb_map );
    if ( gpu_lights_data ) {
        for ( u32 i = 0; i < active_lights; ++i ) {
            Light& light = lights[ i ];
            GpuLight& gpu_light = gpu_lights_data[ i ];

            gpu_light.world_position = light.world_position;
            gpu_light.radius = light.radius;
            gpu_light.color = light.color;
            gpu_light.intensity = light.intensity;
            gpu_light.shadow_map_resolution = light.shadow_map_resolution;
            // NOTE: calculation used to retrieve depth for cubemaps.
            // near = 0.01f as a static value, if you change here change also
            // method vector_to_depth_value in lighting.h in the shaders!
            gpu_light.rcp_n_minus_f = 1.0f / ( 0.01f - light.radius );
        }

        gpu.unmap_buffer( cb_map );
    }

    // Calculate lights LUT
    // NOTE(marco): it might be better to use logarithmic slices to have better resolution
    // closer to the camera. We could also use a different far plane and discard any lights
    // that are too far
    const f32 bin_size = 1.0f / k_light_z_bins;

    Array<u32> bin_range_per_light;
    bin_range_per_light.init( context.scratch_allocator, active_lights, active_lights );

    for ( u32 i = 0; i < active_lights; ++i ) {
        const SortedLight& light = sorted_lights[ i ];

        if ( light.projected_z_min < 0.0f && light.projected_z_max < 0.0f ) {
            // NOTE(marco): this light is behind the camera
            bin_range_per_light[ i ] = u32_max;

            continue;
        }

        const u32 min_bin = raptor::max( 0, raptor::floori32( light.projected_z_min * k_light_z_bins ) );
        const u32 max_bin = raptor::max( 0, raptor::ceili32( light.projected_z_max * k_light_z_bins ) );

        bin_range_per_light[ i ] = ( min_bin & 0xffff ) | ( ( max_bin & 0xffff ) << 16 );
        // rprint( "Light %u min %u, max %u, linear z min %f max %f\n", i, min_bin, max_bin, light.projected_z_min * z_far, light.projected_z_max * z_far );
    }

    for ( u32 bin = 0; bin < k_light_z_bins; ++bin ) {
        u32 min_light_id = k_num_lights + 1;
        u32 max_light_id = 0;

        f32 bin_min = bin_size * bin;
        f32 bin_max = bin_min + bin_size;

        for ( u32 i = 0; i < active_lights; ++i ) {
            const SortedLight& light = sorted_lights[ i ];
            const u32 light_bins = bin_range_per_light[ i ];

            if ( light_bins == u32_max ) {
                continue;
            }

            const u32 min_bin = light_bins & 0xffff;
            const u32 max_bin = light_bins >> 16;

            if ( bin >= min_bin && bin <= max_bin ) {
                if ( i < min_light_id ) {
                    min_light_id = i;
                }

                if ( i > max_light_id ) {
                    max_light_id = i;
                }
            }
            // OLD: left as a reference if new implementation breaks too much.
            //if ( ( light.projected_z >= bin_min && light.projected_z <= bin_max ) ||
            //     ( light.projected_z_min >= bin_min && light.projected_z_min <= bin_max ) ||
            //     ( light.projected_z_max >= bin_min && light.projected_z_max <= bin_max ) ) {
            //    if ( i < min_light_id ) {
            //        min_light_id = i;

            //        //rprint( "Light in bin %u\n", bin );
            //    }

            //    if ( i > max_light_id ) {
            //        max_light_id = i;
            //    }
            //}
        }

        //if (min_light_id != k_num_lights + 1)
            //rprint( "Bin %u, light ids min %u, max %u\n", bin, min_light_id, max_light_id );

        lights_lut[ bin ] = min_light_id | ( max_light_id << 16 );
    }

    // Upload light indices
    cb_map.buffer = lights_indices_sb[ gpu.current_frame ];

    u32* gpu_light_indices = ( u32* )gpu.map_buffer( cb_map );
    if ( gpu_light_indices ) {
        // TODO: improve
        //memcpy( gpu_light_indices, lights_lut.data, lights_lut.size * sizeof( u32 ) );
        for ( u32 i = 0; i < active_lights; ++i ) {
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

    for ( u32 i = 0; i < active_lights; ++i ) {
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

#if 0
    // Too slow to work on CPU.
    // Cluster debug draw
    auto clip_to_view = [&]( vec4s clip ) -> vec4s {
        vec4s view = glms_mat4_mulv( glms_mat4_inv( game_camera.camera.projection ), clip );
        view = glms_vec4_divs( view, view.w );
        return view;
    };

    auto screen_to_view = [&]( vec4s screen ) -> vec4s {
        vec2s uv{ screen.x / gpu.swapchain_width, screen.y / gpu.swapchain_height };

        //vec4s clip{ uv.x * 2.f - .1f, (1.f - uv.y) * 2.f - 1.f, screen.z, screen.w };
        vec4s clip{ uv.x * 2.f - .1f, uv.y * 2.f - 1.f, screen.z, screen.w };
        return clip_to_view( clip );
    };

    auto line_intersection_to_depth_plane = [&]( vec3s a, vec3s b, f32 z_distance ) -> vec3s {
        vec3s normal{ 0,0,1 };

        vec3s ab = glms_vec3_sub( b, a );

        f32 t = ( z_distance - glms_vec3_dot( normal, a ) ) / glms_vec3_dot( normal, ab );

        vec3s result = glms_vec3_add( glms_vec3_scale( ab, t ), a );
        return result;
    };

    for ( u32 x = 0; x < tile_x_count; ++x ) {
        for ( u32 y = 0; y < tile_y_count; ++y ) {
            for ( u32 z = 0; z < k_light_z_bins; ++z ) {

                // Skip empty z bins
                u32 z_bin = lights_lut[ z ];
                if ( ( z_bin & 0xffff ) == k_num_lights + 1 ) {
                    continue;
                }

                // top right
                vec4s maxpoint_screen_space{ ( x + 1 ) * 8.f, ( y + 1 ) * 8.f, 0.f, 1.f };
                // bottom left
                vec4s minpoint_screen_space{ ( x ) * 8.f, ( y ) * 8.f, 0.f, 1.f };

                vec3s maxpoint_view_space = glms_vec3( screen_to_view( maxpoint_screen_space ) );
                vec3s minpoint_view_space = glms_vec3( screen_to_view( minpoint_screen_space ) );

                f32 zNear = game_camera.camera.near_plane;
                f32 zFar = game_camera.camera.far_plane;

                f32 tileNear = (bin_size * z) * zFar + zNear; // -zNear * pow( zFar / zNear, z / float( z ) );
                f32 tileFar = tileNear + (bin_size * (zFar - zNear)); //-zNear * pow( zFar / zNear, ( z + 1 ) / float( z ) );

                //Finding the 4 intersection points made from the maxPoint to the cluster near/far plane
                vec3s eyePos = glms_vec3_zero();
                vec3s minPointNear = line_intersection_to_depth_plane( eyePos, minpoint_view_space, tileNear );
                vec3s minPointFar = line_intersection_to_depth_plane( eyePos, minpoint_view_space, tileFar );
                vec3s maxPointNear = line_intersection_to_depth_plane( eyePos, maxpoint_view_space, tileNear );
                vec3s maxPointFar = line_intersection_to_depth_plane( eyePos, maxpoint_view_space, tileFar );

                // Transform points in world space
                const mat4s view_to_world = glms_mat4_inv( scene_data.world_to_camera_debug );
                minPointNear = glms_mat4_mulv3( view_to_world, minPointNear, 1.f );
                minPointFar = glms_mat4_mulv3( view_to_world, minPointFar, 1.f );
                maxPointNear = glms_mat4_mulv3( view_to_world, maxPointNear, 1.f );
                maxPointFar = glms_mat4_mulv3( view_to_world, maxPointFar, 1.f );

                vec3s minPointAABB = glms_vec3_minv( glms_vec3_minv( minPointNear, minPointFar ), glms_vec3_minv( maxPointNear, maxPointFar ) );
                vec3s maxPointAABB = glms_vec3_maxv( glms_vec3_maxv( minPointNear, minPointFar ), glms_vec3_maxv( maxPointNear, maxPointFar ) );


                debug_renderer.aabb( minPointAABB, maxPointAABB, { Color::white } );
            }
        }
    }

#endif // 0

    context.scratch_allocator->free_marker( current_marker );
}

void RenderScene::on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) {

    for ( u32 i = 0; i < k_max_frames; ++i ) {

        gpu.destroy_buffer( lights_tiles_sb[ i ] );

        const u32 tile_x_count = ceilu32( renderer->width * 1.0f / k_tile_size );
        const u32 tile_y_count = ceilu32( renderer->height * 1.0f / k_tile_size );
        const u32 tiles_entry_count = tile_x_count * tile_y_count * k_num_words;
        const u32 buffer_size = tiles_entry_count * sizeof( u32 );

        BufferCreation buffer_creation;
        buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, buffer_size ).set_name( "light_tiles" );

        lights_tiles_sb[ i ] = renderer->gpu->create_buffer( buffer_creation );
    }

    if ( use_meshlets ) {
        GpuTechnique* transparent_technique = renderer->resource_cache.techniques.get( hash_calculate( "meshlet" ) );
        u32 meshlet_technique_index = transparent_technique->get_pass_index( "transparent_no_cull" );
        GpuTechniquePass& transparent_pass = transparent_technique->passes[ meshlet_technique_index ];

        DescriptorSetLayoutHandle transparent_layout = renderer->gpu->get_descriptor_set_layout( transparent_pass.pipeline, k_material_descriptor_set_index );
        DescriptorSetCreation ds_creation;

        for ( u32 i = 0; i < k_max_frames; ++i ) {

            renderer->gpu->destroy_descriptor_set( mesh_shader_transparent_descriptor_set[ i ] );

            ds_creation.reset().buffer( mesh_task_indirect_early_commands_sb[ i ], 6 ).buffer( mesh_task_indirect_count_early_sb[ i ], 7 ).set_layout( transparent_layout );
            ds_creation.buffer( lights_lut_sb[ i ], 20 ).buffer( lights_list_sb, 21 ).buffer( lights_tiles_sb[ i ], 22 ).buffer( lighting_constants_cb[ i ], 23 ).buffer( lights_indices_sb[ i ], 25 );

            add_mesh_descriptors( ds_creation, transparent_pass );
            add_scene_descriptors( ds_creation, transparent_pass );
            add_meshlet_descriptors( ds_creation, transparent_pass );
            add_lighting_descriptors( ds_creation, transparent_pass, i );
            add_debug_descriptors( ds_creation, transparent_pass );

            mesh_shader_transparent_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
        }
    }
}

void RenderScene::draw_mesh_instance( CommandBuffer* gpu_commands, MeshInstance& mesh_instance, bool transparent ) {

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
        gpu_commands->bind_descriptor_set( transparent ? &mesh.pbr_material.descriptor_set_transparent : &mesh.pbr_material.descriptor_set_main, 1, nullptr, 0 );
    }

    // Gpu mesh index used to retrieve mesh data
    gpu_commands->draw_indexed( TopologyType::Triangle, mesh.primitive_count, 1, 0, 0, mesh_instance.gpu_mesh_instance_index );
}

void RenderScene::add_scene_descriptors( DescriptorSetCreation& descriptor_set_creation, GpuTechniquePass& pass ) {
    const u16 binding = pass.get_binding_index( "SceneConstants" );
    descriptor_set_creation.buffer( scene_cb, binding );
}

void RenderScene::add_mesh_descriptors( DescriptorSetCreation& descriptor_set_creation, GpuTechniquePass& pass ) {
    //.buffer( meshes_sb, "MeshDraws").buffer(mesh_instances_sb, "MeshInstanceDraws").buffer(mesh_bounds_sb, "MeshBounds");

    // These are always defined together.
    const u16 binding_md = pass.get_binding_index( "MeshDraws" );
    const u16 binding_mid = pass.get_binding_index( "MeshInstanceDraws" );
    const u16 binding_mb = pass.get_binding_index( "MeshBounds" );

    descriptor_set_creation.buffer( meshes_sb, binding_md ).buffer( mesh_instances_sb, binding_mid ).buffer( mesh_bounds_sb, binding_mb );
}

void RenderScene::add_meshlet_descriptors( DescriptorSetCreation& descriptor_set_creation, GpuTechniquePass& pass ) {
    // .buffer(meshlets_sb, 1).buffer( meshlets_data_sb, 3 ).buffer( meshlets_vertex_pos_sb, 4 ).buffer( meshlets_vertex_data_sb, 5 )
    // Handle optional bindings
    u16 binding = pass.get_binding_index( "Meshlets" );
    if ( binding != u16_max ) {
        descriptor_set_creation.buffer( meshlets_sb, binding );
    }

    binding = pass.get_binding_index( "MeshletData" );
    if ( binding != u16_max ) {
        descriptor_set_creation.buffer( meshlets_data_sb, binding );
    }

    binding = pass.get_binding_index( "VertexPositions" );
    if ( binding != u16_max ) {
        descriptor_set_creation.buffer( meshlets_vertex_pos_sb, binding );
    }

    binding = pass.get_binding_index( "VertexData" );
    if ( binding != u16_max ) {
        descriptor_set_creation.buffer( meshlets_vertex_data_sb, binding );
    }

   // descriptor_set_creation.buffer( meshlets_sb, pass.get_binding_index( "Meshlets" ) ).buffer( meshlets_data_sb, pass.get_binding_index( "MeshletData" ) )
       // .buffer( meshlets_vertex_pos_sb, pass.get_binding_index( "VertexPositions" ) ).buffer( meshlets_vertex_data_sb, pass.get_binding_index( "VertexData" ) );
}

void RenderScene::add_debug_descriptors( DescriptorSetCreation& descriptor_set_creation, GpuTechniquePass& pass ) {
    // .buffer( debug_line_sb, 20 ).buffer( debug_line_count_sb, 21 ).buffer( debug_line_commands_sb, 22 )

    //  These are always defined all together, no need to check.
    const u16 binding_dl = pass.get_binding_index( "DebugLines" );
    const u16 binding_dlc = pass.get_binding_index( "DebugLinesCount" );
    const u16 binding_dlcmd = pass.get_binding_index( "DebugLineCommands" );

    descriptor_set_creation.buffer( debug_line_sb, binding_dl ).buffer( debug_line_count_sb, binding_dlc ).buffer( debug_line_commands_sb, binding_dlcmd );
}

void RenderScene::add_lighting_descriptors( DescriptorSetCreation& descriptor_set_creation, GpuTechniquePass& pass, u32 frame_index ) {
    // .buffer( scene.lights_lut_sb[ i ], 20 ).buffer( scene.lights_list_sb, 21 )
    // .buffer( scene.lights_tiles_sb[ i ], 22 ).buffer( scene.lights_indices_sb[ i ], 25 )
    u16 binding = pass.get_binding_index( "ZBins" );
    if ( binding != u16_max ) {
        descriptor_set_creation.buffer( lights_lut_sb[ frame_index ], binding );
    }

    binding = pass.get_binding_index( "Lights" );
    if ( binding != u16_max ) {
        descriptor_set_creation.buffer( lights_list_sb, binding );
    }

    binding = pass.get_binding_index( "Tiles" );
    if ( binding != u16_max ) {
        descriptor_set_creation.buffer( lights_tiles_sb[ frame_index ], binding );
    }

    binding = pass.get_binding_index( "LightIndices" );
    if ( binding != u16_max ) {
        descriptor_set_creation.buffer( lights_indices_sb[ frame_index ], binding );
    }

    binding = pass.get_binding_index( "LightConstants" );
    if ( binding != u16_max ) {
        descriptor_set_creation.buffer( lighting_constants_cb[ frame_index ], binding );
    }

    binding = pass.get_binding_index( "as" );
    if ( binding != u16_max ) {
        descriptor_set_creation.set_as( tlas, binding );
    }
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
    // TODO: proper handling.
    TextureHandle output_texture = texture->resource_info.texture.handle;
    if ( scene->taa_enabled ) {
        output_texture = temp_taa_output;
    }

    gpu_commands->bind_pipeline( frame_renderer->main_post_pipeline );
    gpu_commands->bind_descriptor_set( &frame_renderer->fullscreen_ds, 1, nullptr, 0 );
    gpu_commands->draw( TopologyType::Triangle, 0, 3, output_texture.index, 1 );

    imgui->render( *gpu_commands, false );

    gpu_commands->pop_marker(); // Fullscreen marker
    gpu_commands->pop_marker(); // Frame marker

    gpu_profiler->update( *gpu );

    // Send commands to GPU
    gpu->queue_command_buffer( gpu_commands );
}

// FrameRenderer //////////////////////////////////////////////////////////
struct alignas( 16 ) GpuPostConstants {

    u32             tonemap_type;
    f32             exposure;
    f32             sharpening_amount;
    f32             pad;

    vec2s           mouse_uv;
    f32             zoom_scale;
    u32             enable_zoom;
};

void FrameRenderer::init( Allocator* resident_allocator_, Renderer* renderer_,
                          FrameGraph* frame_graph_, SceneGraph* scene_graph_,
                          RenderScene* scene_ ) {
    resident_allocator = resident_allocator_;
    renderer = renderer_;
    frame_graph = frame_graph_;
    scene_graph = scene_graph_;
    scene = scene_;
    render_passes.init( resident_allocator, 16 );

    auto add_render_pass = [&]( cstring name, FrameGraphRenderPass* render_pass ) {
        frame_graph->builder->register_render_pass( name, render_pass );

        render_passes.push( render_pass );
    };

    add_render_pass( "depth_pre_pass", &depth_pre_pass );
    add_render_pass( "gbuffer_pass_early", &gbuffer_pass_early );
    add_render_pass( "gbuffer_pass_late", &gbuffer_pass_late );
    add_render_pass( "lighting_pass", &light_pass );
    add_render_pass( "transparent_pass", &transparent_pass );
    add_render_pass( "depth_of_field_pass", &dof_pass );
    add_render_pass( "debug_pass", &debug_pass );
    add_render_pass( "mesh_occlusion_early_pass", &mesh_occlusion_early_pass );
    add_render_pass( "mesh_occlusion_late_pass", &mesh_occlusion_late_pass );
    add_render_pass( "depth_pyramid_pass", &depth_pyramid_pass );
    add_render_pass( "point_shadows_pass", &pointlight_shadow_pass );
    add_render_pass( "volumetric_fog_pass", &volumetric_fog_pass );
    add_render_pass( "temporal_anti_aliasing_pass", &temporal_anti_aliasing_pass );
    add_render_pass( "motion_vector_pass", &motion_vector_pass );
    add_render_pass( "ray_tracing_test", &ray_tracing_test_pass );
    add_render_pass( "shadow_visibility_pass", &shadow_visiblity_pass );
    add_render_pass( "indirect_lighting_pass", &indirect_pass );
    add_render_pass( "reflections_pass", &reflections_pass );
    add_render_pass( "svgf_accumulation_pass", &svgf_accumulation_pass );
    add_render_pass( "svgf_variance_pass", &svgf_variance_pass );
    add_render_pass( "svgf_wavelet_pass", &svgf_wavelet_pass );
}

void FrameRenderer::shutdown() {

    for ( u32 i = 0; i < render_passes.size; ++i ) {
        render_passes[ i ]->free_gpu_resources( *renderer->gpu );
    }

    renderer->gpu->destroy_descriptor_set( fullscreen_ds );
    renderer->gpu->destroy_buffer( post_uniforms_buffer );

    render_passes.shutdown();
}

void FrameRenderer::upload_gpu_data( UploadGpuDataContext& context ) {
    for ( u32 i = 0; i < render_passes.size; ++i ) {
        render_passes[ i ]->upload_gpu_data( *scene );
    }

    scene->upload_gpu_data( context );

    // TODO: move this
    mesh_occlusion_early_pass.depth_pyramid_texture_index = depth_pyramid_pass.depth_pyramid.index;
    mesh_occlusion_late_pass.depth_pyramid_texture_index = depth_pyramid_pass.depth_pyramid.index;
    indirect_pass.depth_pyramid_texture = depth_pyramid_pass.depth_pyramid;

    GpuDevice& gpu = *renderer->gpu;

    // Update per mesh material buffer
    // TODO: update only changed stuff, this is now dynamic so it can't be done.
    MapBufferParameters cb_map = { post_uniforms_buffer, 0, 0 };
    GpuPostConstants* gpu_constants = ( GpuPostConstants* )gpu.map_buffer( cb_map );
    if ( gpu_constants ) {

        gpu_constants->tonemap_type = scene->post_tonemap_mode;
        gpu_constants->exposure = scene->post_exposure;
        gpu_constants->sharpening_amount = scene->post_sharpening_amount;

        gpu_constants->enable_zoom = scene->post_enable_zoom ? 1 : 0;
        gpu_constants->zoom_scale = scene->post_zoom_scale;

        if ( !scene->post_block_zoom_input ) {
            gpu_constants->mouse_uv = vec2s{ context.last_clicked_position_left_button.x / gpu.swapchain_width,
                                                     context.last_clicked_position_left_button.y / gpu.swapchain_height };
        }

        gpu.unmap_buffer( cb_map );
    }
}

void FrameRenderer::render( CommandBuffer* gpu_commands, RenderScene* render_scene ) {
}

void FrameRenderer::prepare_draws( StackAllocator* scratch_allocator ) {

    scene->prepare_draws( renderer, scratch_allocator, scene_graph );

    for ( u32 i = 0; i < render_passes.size; ++i ) {
        render_passes[ i ]->prepare_draws( *scene, frame_graph, renderer->gpu->allocator, scratch_allocator );
    }

    // Handle fullscreen pass.
    fullscreen_tech = renderer->resource_cache.techniques.get( hash_calculate( "fullscreen" ) );

    u32 pass_index = fullscreen_tech->get_pass_index( "main_triangle" );
    GpuTechniquePass& pass = fullscreen_tech->passes[ pass_index ];
    passthrough_pipeline = pass.pipeline;

    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuPostConstants ) );
    post_uniforms_buffer = renderer->gpu->create_buffer( buffer_creation );

    pass_index = fullscreen_tech->get_pass_index( "main_post" );
    GpuTechniquePass& post_pass = fullscreen_tech->passes[ pass_index ];
    main_post_pipeline = post_pass.pipeline;

    DescriptorSetCreation dsc;
    DescriptorSetLayoutHandle descriptor_set_layout = renderer->gpu->get_descriptor_set_layout( main_post_pipeline, k_material_descriptor_set_index );
    dsc.reset().buffer( scene->scene_cb, 0 ).buffer( post_uniforms_buffer, 11 ).set_layout( descriptor_set_layout );
    fullscreen_ds = renderer->gpu->create_descriptor_set( dsc );

    // TODO [gabriel]: cleanup code to have dependent resources created in the update_dependent_resources
    // method instead of the prepare draw.
    // For now call individually the debug method to cache ddgi stuff.
    debug_pass.update_dependent_resources( *renderer->gpu, frame_graph, scene );
}

void FrameRenderer::update_dependent_resources() {
    for ( u32 i = 0; i < render_passes.size; ++i ) {
        render_passes[ i ]->update_dependent_resources( *renderer->gpu, frame_graph, scene );
    }
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

void project_aabb_cubemap_positive_x( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max ) {
    f32 rd_min = 1.f / glm_max( FLT_EPSILON, aabb[ 0 ].x );
    f32 rd_max = 1.f / glm_max( FLT_EPSILON, aabb[ 1 ].x );

    s_min = glm_min( -aabb[ 1 ].z * rd_min, -aabb[ 1 ].z * rd_max );
    s_max = glm_max( -aabb[ 0 ].z * rd_min, -aabb[ 0 ].z * rd_max );

    t_min = glm_min( -aabb[ 1 ].y * rd_min, -aabb[ 1 ].y * rd_max );
    t_max = glm_max( -aabb[ 0 ].y * rd_min, -aabb[ 0 ].y * rd_max );
}

void project_aabb_cubemap_negative_x( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max ) {
    f32 rd_min = 1.f / glm_max( FLT_EPSILON, -aabb[ 0 ].x );
    f32 rd_max = 1.f / glm_max( FLT_EPSILON, -aabb[ 1 ].x );

    s_min = glm_min( aabb[ 0 ].z * rd_min, aabb[ 0 ].z * rd_max );
    s_max = glm_max( aabb[ 1 ].z * rd_min, aabb[ 1 ].z * rd_max );

    t_min = glm_min( -aabb[ 1 ].y * rd_min, -aabb[ 1 ].y * rd_max );
    t_max = glm_max( -aabb[ 0 ].y * rd_min, -aabb[ 0 ].y * rd_max );
}

void project_aabb_cubemap_positive_y( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max ) {
    f32 rd_min = 1.f / glm_max( FLT_EPSILON, aabb[ 0 ].y );
    f32 rd_max = 1.f / glm_max( FLT_EPSILON, aabb[ 1 ].y );

    s_min = glm_min( -aabb[ 1 ].x * rd_min, -aabb[ 1 ].x * rd_max );
    s_max = glm_max( -aabb[ 0 ].x * rd_min, -aabb[ 0 ].x * rd_max );

    t_min = glm_min( -aabb[ 1 ].z * rd_min, -aabb[ 1 ].z * rd_max );
    t_max = glm_max( -aabb[ 0 ].z * rd_min, -aabb[ 0 ].z * rd_max );
}

void project_aabb_cubemap_negative_y( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max ) {
    f32 rd_min = 1.f / glm_max( FLT_EPSILON, -aabb[ 0 ].y );
    f32 rd_max = 1.f / glm_max( FLT_EPSILON, -aabb[ 1 ].y );

    s_min = glm_min( aabb[ 0 ].x * rd_min, aabb[ 0 ].x * rd_max );
    s_max = glm_max( aabb[ 1 ].x * rd_min, aabb[ 1 ].x * rd_max );

    t_min = glm_min( -aabb[ 1 ].z * rd_min, -aabb[ 1 ].z * rd_max );
    t_max = glm_max( -aabb[ 0 ].z * rd_min, -aabb[ 0 ].z * rd_max );
}

void project_aabb_cubemap_positive_z( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max ) {
    f32 rd_min = 1.f / glm_max( FLT_EPSILON, aabb[ 0 ].z );
    f32 rd_max = 1.f / glm_max( FLT_EPSILON, aabb[ 1 ].z );

    s_min = glm_min( -aabb[ 1 ].x * rd_min, -aabb[ 1 ].x * rd_max );
    s_max = glm_max( -aabb[ 0 ].x * rd_min, -aabb[ 0 ].x * rd_max );

    t_min = glm_min( -aabb[ 1 ].y * rd_min, -aabb[ 1 ].y * rd_max );
    t_max = glm_max( -aabb[ 0 ].y * rd_min, -aabb[ 0 ].y * rd_max );
}

void project_aabb_cubemap_negative_z( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max ) {
    f32 rd_min = 1.f / glm_max( FLT_EPSILON, -aabb[ 0 ].z );
    f32 rd_max = 1.f / glm_max( FLT_EPSILON, -aabb[ 1 ].z );

    s_min = glm_min( aabb[ 0 ].x * rd_min, aabb[ 0 ].x * rd_max );
    s_max = glm_max( aabb[ 1 ].x * rd_min, aabb[ 1 ].x * rd_max );

    t_min = glm_min( -aabb[ 1 ].y * rd_min, -aabb[ 1 ].y * rd_max );
    t_max = glm_max( -aabb[ 0 ].y * rd_min, -aabb[ 0 ].y * rd_max );
}

// Numerical sequences ////////////////////////////////////////////////////
f32 halton( i32 i, i32 b ) {
    // Creates a halton sequence of values between 0 and 1.
    // https://en.wikipedia.org/wiki/Halton_sequence
    // Used for jittering based on a constant set of 2D points.
    f32 f = 1.0f;
    f32 r = 0.0f;
    while ( i > 0 ) {
        f = f / f32( b );
        r = r + f * f32( i % b );
        i = i / b;
    }
    return r;
}

// https://blog.demofox.org/2017/10/31/animating-noise-for-integration-over-time/
f32 interleaved_gradient_noise( vec2s pixel, i32 index ) {
    pixel = glms_vec2_adds( pixel, f32( index ) * 5.588238f );
    const f32 noise = fmodf( 52.9829189f * fmodf( 0.06711056f * pixel.x + 0.00583715f * pixel.y, 1.0f ), 1.0f );
    return noise;
}

vec2s halton23_sequence( i32 index ) {
    return vec2s{ halton( index, 2 ), halton( index, 3 ) };
}

// http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
vec2s m_robert_r2_sequence( i32 index ) {
    const f32 g = 1.32471795724474602596f;
    const f32 a1 = 1.0f / g;
    const f32 a2 = 1.0f / (g * g);

    const f32 x = fmod( 0.5f + a1 * index, 1.0f );
    const f32 y = fmod( 0.5f + a2 * index, 1.0f );
    return vec2s{ x, y };
}

vec2s interleaved_gradient_sequence( i32 index ) {
    return vec2s{ interleaved_gradient_noise( {1.f, 1.f}, index ), interleaved_gradient_noise( {1.f, 2.f}, index ) };
}

// Computes a radical inverse with base 2 using crazy bit-twiddling from "Hacker's Delight"
inline f32 radical_inverse_base2( u32 bits ) {
    bits = ( bits << 16u ) | ( bits >> 16u );
    bits = ( ( bits & 0x55555555u ) << 1u ) | ( ( bits & 0xAAAAAAAAu ) >> 1u );
    bits = ( ( bits & 0x33333333u ) << 2u ) | ( ( bits & 0xCCCCCCCCu ) >> 2u );
    bits = ( ( bits & 0x0F0F0F0Fu ) << 4u ) | ( ( bits & 0xF0F0F0F0u ) >> 4u );
    bits = ( ( bits & 0x00FF00FFu ) << 8u ) | ( ( bits & 0xFF00FF00u ) >> 8u );
    return f32( bits ) * 2.3283064365386963e-10f; // / 0x100000000
}

// Returns a single 2D point in a Hammersley sequence of length "numSamples", using base 1 and base 2
vec2s hammersley_sequence( i32 index, i32 num_samples ) {
    return vec2s{ index * 1.f / num_samples, radical_inverse_base2( u32( index ) ) };
}

// DebugRenderer //////////////////////////////////////////////////////////

//
//
struct LineVertex {
    vec3s                           position;
    Color                           color;

    void                            set( vec3s position_, Color color_ ) { position = position_; color = color_; }
    void                            set( vec2s position_, Color color_ ) { position = { position_.x, position_.y, 0 }; color = color_; }
}; // struct LineVertex

//
//
struct LineVertex2D {
    vec3s                           position;
    u32                             color;

    void                            set( vec2s position_, Color color_ ) { position = { position_.x, position_.y, 0 }, color = color_.abgr; }
}; // struct LineVertex2D

static const u32            k_max_lines = 1024 * 1024;

static LineVertex           s_line_buffer[ k_max_lines ];
static LineVertex2D         s_line_buffer_2d[ k_max_lines ];


void DebugRenderer::render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) {

    if ( current_line ) {

        const u32 mapping_size = sizeof( LineVertex ) * current_line;
        MapBufferParameters cb_map{ lines_vb, 0, mapping_size };
        LineVertex* vtx_dst = ( LineVertex* )renderer->gpu->map_buffer( cb_map );

        if ( vtx_dst ) {
            memcpy( vtx_dst, &s_line_buffer[ 0 ], mapping_size );

            renderer->gpu->unmap_buffer( cb_map );
        }

        gpu_commands->bind_pipeline( debug_lines_draw_pipeline );
        gpu_commands->bind_vertex_buffer( lines_vb, 0, 0 );
        gpu_commands->bind_descriptor_set( &debug_lines_draw_set, 1, nullptr, 0 );
        // Draw using instancing and 6 vertices.
        const uint32_t num_vertices = 6;
        gpu_commands->draw( TopologyType::Triangle, 0, num_vertices, 0, current_line / 2 );

        current_line = 0;
    }

    if ( current_line_2d ) {

        const u32 mapping_size = sizeof( LineVertex2D ) * current_line_2d;
        MapBufferParameters cb_map{ lines_vb_2d, 0, mapping_size };
        LineVertex2D* vtx_dst = ( LineVertex2D* )renderer->gpu->map_buffer( cb_map );

        if ( vtx_dst ) {
            memcpy( vtx_dst, &s_line_buffer_2d[ 0 ], mapping_size );

            renderer->gpu->unmap_buffer( cb_map );
        }

        gpu_commands->bind_pipeline( debug_lines_2d_draw_pipeline );
        gpu_commands->bind_vertex_buffer( lines_vb_2d, 0, 0 );
        gpu_commands->bind_descriptor_set( &debug_lines_draw_set, 1, nullptr, 0 );
        // Draw using instancing and 6 vertices.
        const uint32_t num_vertices = 6;
        gpu_commands->draw( TopologyType::Triangle, 0, num_vertices, 0, current_line_2d / 2 );

        current_line_2d = 0;
    }
}

void DebugRenderer::init( RenderScene& scene, Allocator* resident_allocator, StackAllocator* scratch_allocator ) {

    renderer = scene.renderer;

    current_line_2d = current_line = 0;

    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( LineVertex ) * k_max_lines ).set_name( "lines_vb" );
    lines_vb = renderer->gpu->create_buffer( buffer_creation );

    buffer_creation.reset().set( VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( LineVertex2D ) * k_max_lines ).set_name( "lines_vb_2d" );
    lines_vb_2d = renderer->gpu->create_buffer( buffer_creation );

    const u64 hashed_name = hash_calculate( "debug" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    // Prepare CPU debug line resources
    {
        DescriptorSetCreation set_creation{ };

        // Draw pass
        u32 pass_index = main_technique->get_pass_index( "debug_line_cpu" );
        GpuTechniquePass& pass = main_technique->passes[ pass_index ];
        debug_lines_draw_pipeline = pass.pipeline;
        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( pass.pipeline, k_material_descriptor_set_index );

        set_creation.reset().set_layout( layout );
        scene.add_scene_descriptors( set_creation, pass );
        scene.add_debug_descriptors( set_creation, pass );
        debug_lines_draw_set = renderer->gpu->create_descriptor_set( set_creation );

        pass_index = main_technique->get_pass_index( "debug_line_2d_cpu" );
        debug_lines_2d_draw_pipeline = main_technique->passes[ pass_index ].pipeline;
    }
}

void DebugRenderer::shutdown() {

    renderer->gpu->destroy_buffer( lines_vb );
    renderer->gpu->destroy_buffer( lines_vb_2d );
    renderer->gpu->destroy_descriptor_set( debug_lines_draw_set );
}

void DebugRenderer::line( const vec3s& from, const vec3s& to, Color color ) {
    line( from, to, color, color );
}

void DebugRenderer::line_2d( const vec2s& from, const vec2s& to, Color color ) {
    if ( current_line_2d >= k_max_lines ) {
        return;
    }

    s_line_buffer_2d[ current_line_2d++ ].set( from, color );
    s_line_buffer_2d[ current_line_2d++ ].set( to, color );
}

void DebugRenderer::line( const vec3s& from, const vec3s& to, Color color0, Color color1 ) {
    if ( current_line >= k_max_lines )
        return;

    s_line_buffer[ current_line++ ].set( from, color0 );
    s_line_buffer[ current_line++ ].set( to, color1 );
}

void DebugRenderer::aabb( const vec3s& min, const vec3s max, Color color ) {

    const f32 x0 = min.x;
    const f32 y0 = min.y;
    const f32 z0 = min.z;
    const f32 x1 = max.x;
    const f32 y1 = max.y;
    const f32 z1 = max.z;

    line( { x0, y0, z0 }, { x0, y1, z0 }, color, color );
    line( { x0, y1, z0 }, { x1, y1, z0 }, color, color );
    line( { x1, y1, z0 }, { x1, y0, z0 }, color, color );
    line( { x1, y0, z0 }, { x0, y0, z0 }, color, color );
    line( { x0, y0, z0 }, { x0, y0, z1 }, color, color );
    line( { x0, y1, z0 }, { x0, y1, z1 }, color, color );
    line( { x1, y1, z0 }, { x1, y1, z1 }, color, color );
    line( { x1, y0, z0 }, { x1, y0, z1 }, color, color );
    line( { x0, y0, z1 }, { x0, y1, z1 }, color, color );
    line( { x0, y1, z1 }, { x1, y1, z1 }, color, color );
    line( { x1, y1, z1 }, { x1, y0, z1 }, color, color );
    line( { x1, y0, z1 }, { x0, y0, z1 }, color, color );
}

} // namespace raptor
