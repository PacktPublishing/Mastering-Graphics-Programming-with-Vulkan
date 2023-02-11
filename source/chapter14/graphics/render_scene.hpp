#pragma once

#include "foundation/array.hpp"
#include "foundation/platform.hpp"
#include "foundation/color.hpp"

#include "graphics/command_buffer.hpp"
#include "graphics/renderer.hpp"
#include "graphics/gpu_resources.hpp"
#include "graphics/frame_graph.hpp"

#include "external/cglm/types-struct.h"

#include "external/enkiTS/TaskScheduler.h"

#include <atomic>

namespace enki { class TaskScheduler; }

namespace raptor {

    struct Allocator;
    struct AsynchronousLoader;
    struct FrameGraph;
    struct GpuVisualProfiler;
    struct ImGuiService;
    struct Renderer;
    struct RenderScene;
    struct SceneGraph;
    struct StackAllocator;
    struct GameCamera;

    static const u16    k_invalid_scene_texture_index      = u16_max;
    static const u32    k_material_descriptor_set_index    = 1;
    static const u32    k_max_joint_count                  = 12;
    static const u32    k_max_depth_pyramid_levels         = 16;

    static const u32    k_num_lights                       = 256;
    static const u32    k_light_z_bins                     = 16;
    static const u32    k_tile_size                        = 8;
    static const u32    k_num_words                        = ( k_num_lights + 31 ) / 32;

    static bool         recreate_per_thread_descriptors = false;
    static bool         use_secondary_command_buffers   = false;

    //
    //
    enum DrawFlags {
        DrawFlags_AlphaMask     = 1 << 0,
        DrawFlags_DoubleSided   = 1 << 1,
        DrawFlags_Transparent   = 1 << 2,
        DrawFlags_Phong         = 1 << 3,
        DrawFlags_HasNormals    = 1 << 4,
        DrawFlags_HasTexCoords  = 1 << 5,
        DrawFlags_HasTangents   = 1 << 6,
        DrawFlags_HasJoints     = 1 << 7,
        DrawFlags_HasWeights    = 1 << 8,
        DrawFlags_AlphaDither   = 1 << 9,
        DrawFlags_Cloth         = 1 << 10,
    }; // enum DrawFlags

    //
    //
    struct alignas(16) GpuSceneData {
        mat4s                   view_projection;
        mat4s                   view_projection_debug;
        mat4s                   inverse_view_projection;
        mat4s                   world_to_camera;    // view matrix
        mat4s                   world_to_camera_debug;
        mat4s                   previous_view_projection;
        mat4s                   inverse_projection;
        mat4s                   inverse_view;

        vec4s                   camera_position;
        vec4s                   camera_position_debug;
        vec3s                   camera_direction;
        i32                     current_frame;

        u32                     active_lights;
        u32                     use_tetrahedron_shadows;
        u32                     dither_texture_index;
        f32                     z_near;

        f32                     z_far;
        f32                     projection_00;
        f32                     projection_11;
        u32                     culling_options;

        f32                     resolution_x;
        f32                     resolution_y;
        f32                     aspect_ratio;
        u32                     num_mesh_instances;

        f32                     halton_x;
        f32                     halton_y;
        u32                     depth_texture_index;
        u32                     blue_noise_128_rg_texture_index;

        vec2s                   jitter_xy;
        vec2s                   previous_jitter_xy;

        f32                     forced_metalness;
        f32                     forced_roughness;
        f32                     volumetric_fog_application_dithering_scale;
        u32                     volumetric_fog_application_options;

        vec4s                   frustum_planes[ 6 ];

        // Helpers for bit packing. Would be perfect for code generation
        // NOTE: must be in sync with scene.h!
        bool                    frustum_cull_meshes() const             { return ( culling_options &  1 ) ==  1; }
        bool                    frustum_cull_meshlets() const           { return ( culling_options &  2 ) ==  2; }
        bool                    occlusion_cull_meshes() const           { return ( culling_options &  4 ) ==  4; }
        bool                    occlusion_cull_meshlets() const         { return ( culling_options &  8 ) ==  8; }
        bool                    freeze_occlusion_camera() const         { return ( culling_options & 16 ) == 16; }
        bool                    shadow_meshlets_cone_cull() const         { return ( culling_options & 32 ) == 32; }
        bool                    shadow_meshlets_sphere_cull() const       { return ( culling_options & 64 ) == 64; }
        bool                    shadow_meshlets_cubemap_face_cull() const { return ( culling_options & 128 ) == 128; }
        bool                    shadow_mesh_sphere_cull() const         { return ( culling_options & 256 ) == 256; }

        void                    set_frustum_cull_meshes(bool value)     { value ? (culling_options |=  1) : (culling_options &= ~( 1)); }
        void                    set_frustum_cull_meshlets(bool value)   { value ? (culling_options |=  2) : (culling_options &= ~( 2)); }
        void                    set_occlusion_cull_meshes(bool value)   { value ? (culling_options |=  4) : (culling_options &= ~( 4)); }
        void                    set_occlusion_cull_meshlets(bool value) { value ? (culling_options |=  8) : (culling_options &= ~( 8)); }
        void                    set_freeze_occlusion_camera(bool value) { value ? (culling_options |= 16) : (culling_options &= ~(16)); }
        void                    set_shadow_meshlets_cone_cull( bool value )      { value ? ( culling_options |= 32 ) : ( culling_options &= ~( 32 ) ); }
        void                    set_shadow_meshlets_sphere_cull( bool value )    { value ? ( culling_options |= 64 ) : ( culling_options &= ~( 64 ) ); }
        void                    set_shadow_meshlets_cubemap_face_cull( bool value ) { value ? ( culling_options |= 128 ) : ( culling_options &= ~( 128 ) ); }
        void                    set_shadow_mesh_sphere_cull( bool value ) { value ? ( culling_options |= 256 ) : ( culling_options &= ~( 256 ) ); }

    }; // struct GpuSceneData

    struct alignas( 16 ) GpuLightingData {

        u32                     cubemap_shadows_index;
        u32                     debug_show_light_tiles;
        u32                     debug_show_tiles;
        u32                     debug_show_bins;

        u32                     disable_shadows;
        u32                     debug_modes;
        u32                     debug_texture_index;
        u32                     shadow_visibility_texture_index;

        u32                     volumetric_fog_texture_index;
        u32                     volumetric_fog_num_slices;
        f32                     volumetric_fog_near;
        f32                     volumetric_fog_far;

        f32                     volumetric_fog_distribution_scale;
        f32                     volumetric_fog_distribution_bias;
        f32                     gi_intensity;
        u32                     indirect_lighting_texture_index;

        u32                     bilateral_weights_texture_index;
        u32                     reflections_texture_index;
        u32                     raytraced_shadow_light_color_type;
        f32                     raytraced_shadow_light_radius;

        vec3s                   raytraced_shadow_light_position;
        f32                     raytraced_shadow_light_intensity;
    }; // GpuLightingData

    struct glTFScene;
    struct Material;

    //
    //
    struct PBRMaterial {

        Material*               material        = nullptr;

        BufferHandle            material_buffer = k_invalid_buffer;
        DescriptorSetHandle     descriptor_set_transparent  = k_invalid_set;
        DescriptorSetHandle     descriptor_set_main = k_invalid_set;

        // Indices used for bindless textures.
        u16                     diffuse_texture_index   = u16_max;
        u16                     roughness_texture_index = u16_max;
        u16                     normal_texture_index    = u16_max;
        u16                     occlusion_texture_index = u16_max;
        u16                     emissive_texture_index  = u16_max;

        // PBR
        vec4s                   base_color_factor   = {1.f, 1.f, 1.f, 1.f};
        vec3s                   emissive_factor     = {0.f, 0.f, 0.f};

        f32                     metallic            = 0.f;
        f32                     roughness           = 1.f;
        f32                     occlusion           = 0.f;
        f32                     alpha_cutoff        = 1.f;

        u32                     flags               = 0;
    }; // struct PBRMaterial

    //
    //
    struct PhysicsJoint {
        i32                     vertex_index = -1;

        // TODO(marco): for now this is only for cloth
        float                   stifness;
    };

    //
    //
    struct PhysicsVertex {
        void                    add_joint( u32 vertex_index );

        vec3s                   start_position;
        vec3s                   previous_position;
        vec3s                   position;
        vec3s                   normal;

        vec3s                   velocity;
        vec3s                   force;

        PhysicsJoint            joints[ k_max_joint_count ];
        u32                     joint_count;

        float                   mass;
        bool                    fixed;
    };

    //
    //
    struct PhysicsVertexGpuData {
        vec3s                   position;
        f32                     pad0_;

        vec3s                   start_position;
        f32                     pad1_;

        vec3s                   previous_position;
        f32                     pad2_;

        vec3s                   normal;
        u32                     joint_count;

        vec3s                   velocity;
        f32                     mass;

        vec3s                   force;

        // TODO(marco): better storage, values are never greater than 12
        u32                     joints[ k_max_joint_count ];
        u32                     pad3_;
    };

    //
    //
    struct PhysicsMeshGpuData {
        u32                     index_count;
        u32                     vertex_count;

        u32                     padding_[ 2 ];
    };

    //
    //
    struct PhysicsSceneData {
        vec3s                   wind_direction;
        u32                     reset_simulation;

        f32                     air_density;
        f32                     spring_stiffness;
        f32                     spring_damping;
        f32                     padding_;
    };

    //
    //
    struct PhysicsMesh {
        u32                     mesh_index;

        Array<PhysicsVertex>    vertices;

        BufferHandle            gpu_buffer;
        BufferHandle            draw_indirect_buffer;
        DescriptorSetHandle     descriptor_set;
        DescriptorSetHandle     debug_mesh_descriptor_set;
    };

    //
    //
    struct Mesh {

        PBRMaterial             pbr_material;

        PhysicsMesh*            physics_mesh;

        // Vertex data
        BufferHandle            position_buffer;
        BufferHandle            tangent_buffer;
        BufferHandle            normal_buffer;
        BufferHandle            texcoord_buffer;
        // TODO: separate
        BufferHandle            joints_buffer;
        BufferHandle            weights_buffer;

        u32                     position_offset;
        u32                     tangent_offset;
        u32                     normal_offset;
        u32                     texcoord_offset;
        u32                     joints_offset;
        u32                     weights_offset;

        // Index data
        BufferHandle            index_buffer;
        VkIndexType             index_type;
        u32                     index_offset;

        u32                     primitive_count;

        u32                     meshlet_offset;
        u32                     meshlet_count;
        u32                     meshlet_index_count;

        u32                     gpu_mesh_index          = u32_max;
        i32                     skin_index              = i32_max;

        vec4s                   bounding_sphere;

        bool                    has_skinning() const    { return skin_index != i32_max;}
        bool                    is_transparent() const  { return ( pbr_material.flags & ( DrawFlags_AlphaMask | DrawFlags_Transparent ) ) != 0; }
        bool                    is_double_sided() const { return ( pbr_material.flags & DrawFlags_DoubleSided ) == DrawFlags_DoubleSided; }
        bool                    is_cloth() const { return ( pbr_material.flags & DrawFlags_Cloth ) == DrawFlags_Cloth; }
    }; // struct Mesh

    //
    //
    struct MeshInstance {

        Mesh*                   mesh;

        u32                     gpu_mesh_instance_index = u32_max;
        u32                     scene_graph_node_index  = u32_max;

    }; // struct MeshInstance

    //
    //
    struct MeshInstanceDraw {
        MeshInstance*           mesh_instance;
        u32                     material_pass_index = u32_max;
    };

    //
    //
    struct alignas( 16 ) GpuMeshlet {

        vec3s                   center;
        f32                     radius;

        i8                      cone_axis[ 3 ];
        i8                      cone_cutoff;

        u32                     data_offset;
        u32                     mesh_index;
        u8                      vertex_count;
        u8                      triangle_count;
    }; // struct GpuMeshlet

    //
    //
    struct MeshletToMeshIndex {
        u32                     mesh_index;
        u32                     primitive_index;
    }; // struct MeshletToMeshIndex

    //
    //
    struct GpuMeshletVertexPosition {

        float                   position[3];
        float                   padding;
    }; // struct GpuMeshletVertexPosition


    //
    //
    struct GpuMeshletVertexData {

        u8                      normal[ 4 ];
        u8                      tangent[ 4 ];
        u16                     uv_coords[ 2 ];
        float                   padding;
    }; // struct GpuMeshletVertexData

    //
    //
    struct alignas( 16 ) GpuMaterialData {

        u32                     textures[ 4 ]; // diffuse, roughness, normal, occlusion
        // PBR
        vec4s                   emissive; // emissive_color_factor + emissive texture index
        vec4s                   base_color_factor;
        vec4s                   metallic_roughness_occlusion_factor; // metallic, roughness, occlusion

        u32                     flags;
        f32                     alpha_cutoff;
        u32                     vertex_offset;
        u32                     mesh_index;

        u32                     meshlet_offset;
        u32                     meshlet_count;
        u32                     meshlet_index_count;
        u32                     padding1_;

        VkDeviceAddress         position_buffer;
        VkDeviceAddress         uv_buffer;
        VkDeviceAddress         index_buffer;
        VkDeviceAddress         normals_buffer;

    }; // struct GpuMaterialData

    //
    //
    struct alignas( 16 ) GpuMeshInstanceData {
        mat4s                   world;
        mat4s                   inverse_world;

        u32                     mesh_index;
        u32                     pad000;
        u32                     pad001;
        u32                     pad002;
    }; // struct GpuMeshInstanceData

    //
    //
    struct alignas( 16 ) GpuMeshDrawCommand {
        u32                     drawId;
        VkDrawIndexedIndirectCommand indirect;          // 5 u32
        VkDrawMeshTasksIndirectCommandNV indirectMS;    // 2 u32
    }; // struct GpuMeshDrawCommand

    //
    //
    struct alignas( 16 ) GpuMeshDrawCounts {
        u32                     opaque_mesh_visible_count;
        u32                     opaque_mesh_culled_count;
        u32                     transparent_mesh_visible_count;
        u32                     transparent_mesh_culled_count;

        u32                     total_count;
        u32                     depth_pyramid_texture_index;
        u32                     late_flag;
        u32                     meshlet_index_count;

        u32                     dispatch_task_x;
        u32                     dispatch_task_y;
        u32                     dispatch_task_z;
        u32                     pad001;

    }; // struct GpuMeshDrawCounts

    // Animation structs //////////////////////////////////////////////////
    //
    //
    struct AnimationChannel {

        enum TargetType {
            Translation, Rotation, Scale, Weights, Count
        };

        i32                     sampler;
        i32                     target_node;
        TargetType              target_type;

    }; // struct AnimationChannel

    struct AnimationSampler {

        enum Interpolation {
            Linear, Step, CubicSpline, Count
        };

        Array<f32>              key_frames;
        vec4s*                  data;       // Aligned-allocated data. Count is the same as key_frames.
        Interpolation           interpolation_type;

    }; // struct AnimationSampler

    //
    //
    struct Animation {

        f32                     time_start;
        f32                     time_end;

        Array<AnimationChannel> channels;
        Array<AnimationSampler> samplers;

    }; // struct Animation

    //
    //
    struct AnimationInstance {
        Animation*              animation;
        f32                     current_time;
    }; // struct AnimationInstance

    // Skinning ///////////////////////////////////////////////////////////
    //
    //
    struct Skin {

        u32                     skeleton_root_index;
        Array<i32>              joints;
        mat4s*                  inverse_bind_matrices;  // Align-allocated data. Count is same as joints.

        BufferHandle            joint_transforms;

    }; // struct Skin

    // Transform //////////////////////////////////////////////////////////

    //
    struct Transform {

        vec3s                   scale;
        versors                 rotation;
        vec3s                   translation;

        void                    reset();
        mat4s                   calculate_matrix() const;

    }; // struct Transform

    // Light //////////////////////////////////////////////////////////////

    //
    struct alignas( 16 ) Light {

        vec3s                   world_position;
        f32                     radius;

        vec3s                   color;
        f32                     intensity;

        vec4s                   aabb_min;
        vec4s                   aabb_max;

        f32                     shadow_map_resolution;
        u32                     tile_x;
        u32                     tile_y;
        f32                     solid_angle;

    }; // struct Light

    // Separated from Light struct as it could contain unpacked data.
    struct alignas( 16 ) GpuLight {

        vec3s                   world_position;
        f32                     radius;

        vec3s                   color;
        f32                     intensity;

        f32                     shadow_map_resolution;
        f32                     rcp_n_minus_f;          // Calculation of 1 / (n - f) used to retrieve cubemap shadows depth value.
        f32                     pad1;
        f32                     pad2;

    }; // struct GpuLight

    struct UploadGpuDataContext {
        GameCamera&             game_camera;
        StackAllocator*         scratch_allocator;

        vec2s                   last_clicked_position_left_button;

        u8                      skip_invisible_lights       : 1;
        u8                      use_mcguire_method          : 1;
        u8                      use_view_aabb               : 1;
        u8                      enable_camera_inside        : 1;
        u8                      force_fullscreen_light_aabb : 1;
        u8                      pad000                      : 3;

    }; // struct UploadGpuDataContext

    // Volumetric Fog /////////////////////////////////////////////////////
    struct alignas( 16 ) GpuVolumetricFogConstants {

        mat4s                   froxel_inverse_view_projection;

        f32                     froxel_near;
        f32                     froxel_far;
        f32                     scattering_factor;
        f32                     density_modifier;

        u32                     light_scattering_texture_index;
        u32                     integrated_light_scattering_texture_index;
        u32                     froxel_data_texture_index;
        u32                     previous_light_scattering_texture_index;

        u32                     use_temporal_reprojection;
        f32                     time_random_01;
        f32                     temporal_reprojection_percentage;
        f32                     phase_anisotropy_01;

        u32                     froxel_dimension_x;
        u32                     froxel_dimension_y;
        u32                     froxel_dimension_z;
        u32                     phase_function_type;

        f32                     height_fog_density;
        f32                     height_fog_falloff;
        f32                     pad1;
        f32                     noise_scale;

        f32                     lighting_noise_scale;
        u32                     noise_type;
        u32                     pad0;
        u32                     use_spatial_filtering;

        u32                     volumetric_noise_texture_index;
        f32                     volumetric_noise_position_multiplier;
        f32                     volumetric_noise_speed_multiplier;
        f32                     temporal_reprojection_jitter_scale;

        vec3s                   box_position;
        f32                     box_fog_density;

        vec3s                   box_half_size;
        u32                     box_color;

    }; // struct GpuVolumetricFogConstants

    //
    struct alignas( 16 ) GpuTaaConstants {

        u32                     history_color_texture_index;
        u32                     taa_output_texture_index;
        u32                     velocity_texture_index;
        u32                     current_color_texture_index;

        u32                     taa_modes;
        u32                     options;
        u32                     pad0;
        u32                     pad1;

        u32                     velocity_sampling_mode;
        u32                     history_sampling_filter;
        u32                     history_constraint_mode;
        u32                     current_color_filter;

    }; // struct GpuTaaConstants

    // Render Passes //////////////////////////////////////////////////////

    //
    //
    struct DepthPrePass : public FrameGraphRenderPass {
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        Array<MeshInstanceDraw> mesh_instance_draws;
        Renderer*               renderer;
        u32                     meshlet_technique_index;
    }; // struct DepthPrePass

    //
    //
    struct DepthPyramidPass : public FrameGraphRenderPass {
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;
        void                    post_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        void                    create_depth_pyramid_resource( Texture* depth_texture );

        Renderer*               renderer;

        PipelineHandle          depth_pyramid_pipeline;
        TextureHandle           depth_pyramid;
        SamplerHandle           depth_pyramid_sampler;
        TextureHandle           depth_pyramid_views[ k_max_depth_pyramid_levels ];
        DescriptorSetHandle     depth_hierarchy_descriptor_set[ k_max_depth_pyramid_levels ];

        u32                     depth_pyramid_levels = 0;

        bool                    update_depth_pyramid;
    }; // struct DepthPrePass

    //
    //
    struct GBufferPass : public FrameGraphRenderPass {
        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        Array<MeshInstanceDraw> mesh_instance_draws;
        Renderer*               renderer;

        PipelineHandle          meshlet_draw_pipeline;
        PipelineHandle          meshlet_emulation_draw_pipeline;

        BufferHandle            generate_meshlet_dispatch_indirect_buffer[ k_max_frames ];
        PipelineHandle          generate_meshlet_index_buffer_pipeline;
        DescriptorSetHandle     generate_meshlet_index_buffer_descriptor_set[ k_max_frames ];
        PipelineHandle          generate_meshlets_instances_pipeline;
        DescriptorSetHandle     generate_meshlets_instances_descriptor_set[ k_max_frames ];
        BufferHandle            meshlet_instance_culling_indirect_buffer[ k_max_frames ];
        PipelineHandle          meshlet_instance_culling_pipeline;
        DescriptorSetHandle     meshlet_instance_culling_descriptor_set[ k_max_frames ];
        PipelineHandle          meshlet_write_counts_pipeline;

    }; // struct GBufferPass

    //
    //
    struct LateGBufferPass : public FrameGraphRenderPass {
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        Array<MeshInstanceDraw> mesh_instance_draws;
        Renderer*               renderer;
        u32                     meshlet_technique_index;
    }; // struct LateGBufferPass

    //
    //
    struct LightPass : public FrameGraphRenderPass {
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;
        void                    post_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;
        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        Mesh                    mesh;
        Renderer*               renderer;
        bool                    use_compute;

        DescriptorSetHandle     lighting_descriptor_set[ k_max_frames ];
        TextureHandle           lighting_debug_texture;

        DescriptorSetHandle     fragment_rate_descriptor_set[ k_max_frames ];
        BufferHandle            fragment_rate_texture_index[ k_max_frames ];

        FrameGraphResource*     color_texture;
        FrameGraphResource*     normal_texture;
        FrameGraphResource*     roughness_texture;
        FrameGraphResource*     depth_texture;
        FrameGraphResource*     emissive_texture;

        FrameGraphResource*     output_texture;
    }; // struct LightPass

    //
    //
    struct TransparentPass : public FrameGraphRenderPass {
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        Array<MeshInstanceDraw> mesh_instance_draws;
        Renderer*               renderer;
        u32                     meshlet_technique_index;
    }; // struct TransparentPass

    //
    //
    struct PointlightShadowPass : public FrameGraphRenderPass {
        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        void                    recreate_lightcount_dependent_resources( RenderScene& scene );
        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        Array<MeshInstanceDraw> mesh_instance_draws;
        Renderer*               renderer;

        u32                     last_active_lights = 0;

        BufferHandle            pointlight_view_projections_cb[ k_max_frames ];
        BufferHandle            pointlight_spheres_cb[ k_max_frames ];
        // Manual pass generation, add support in framegraph for special cases like this?
        RenderPassHandle        cubemap_render_pass;
        FramebufferHandle       cubemap_framebuffer;
        // Cubemap rendering
        TextureHandle           cubemap_shadow_array_texture;
        DescriptorSetHandle     cubemap_meshlet_draw_descriptor_set[ k_max_frames ];
        PipelineHandle          cubemap_meshlets_pipeline;
        // Tetrahedron rendering
        TextureHandle           tetrahedron_shadow_texture;
        PipelineHandle          tetrahedron_meshlet_pipeline;
        FramebufferHandle       tetrahedron_framebuffer;

        // Culling pass
        PipelineHandle          meshlet_culling_pipeline;
        DescriptorSetHandle     meshlet_culling_descriptor_set[ k_max_frames ];
        BufferHandle            meshlet_visible_instances[ k_max_frames ];
        BufferHandle            per_light_meshlet_instances[ k_max_frames ];

        // Write command pass
        PipelineHandle          meshlet_write_commands_pipeline;
        DescriptorSetHandle     meshlet_write_commands_descriptor_set[ k_max_frames ];
        BufferHandle            meshlet_shadow_indirect_cb[ k_max_frames ];

        // Shadow resolution pass
        PipelineHandle          shadow_resolution_pipeline;
        DescriptorSetHandle     shadow_resolution_descriptor_set[ k_max_frames ];
        BufferHandle            light_aabbs;
        BufferHandle            shadow_resolutions[ k_max_frames ];
        BufferHandle            shadow_resolutions_readback[ k_max_frames ];

        PagePoolHandle          shadow_maps_pool = k_invalid_page_pool;

        TextureHandle           cubemap_debug_face_texture;

    }; // struct PointlightShadowPass


    //
    //
    struct VolumetricFogPass : public FrameGraphRenderPass {
        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        // Inject Data
        PipelineHandle          inject_data_pipeline;
        TextureHandle           froxel_data_texture_0;

        // Light Scattering
        PipelineHandle          light_scattering_pipeline;
        TextureHandle           light_scattering_texture[ 2 ]; // Temporal reprojection between 2 textures
        DescriptorSetHandle     light_scattering_descriptor_set[ k_max_frames ];
        u32                     current_light_scattering_texture_index = 1;
        u32                     previous_light_scattering_texture_index = 0;

        // Light Integration
        PipelineHandle          light_integration_pipeline;
        TextureHandle           integrated_light_scattering_texture;

        // Spatial Filtering
        PipelineHandle          spatial_filtering_pipeline;
        // Temporal Filtering
        PipelineHandle          temporal_filtering_pipeline;
        // Volumetric Noise baking
        PipelineHandle          volumetric_noise_baking;
        TextureHandle           volumetric_noise_texture;
        SamplerHandle           volumetric_tiling_sampler;
        bool                    has_baked_noise = false;

        DescriptorSetHandle     fog_descriptor_set;
        BufferHandle            fog_constants;

        Renderer*               renderer;

    }; // struct VolumetricFogPass

    //
    //
    struct TemporalAntiAliasingPass : public FrameGraphRenderPass {

        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        PipelineHandle          taa_pipeline;
        TextureHandle           history_textures[ 2 ];
        DescriptorSetHandle     taa_descriptor_set;
        BufferHandle            taa_constants;

        u32                     current_history_texture_index = 1;
        u32                     previous_history_texture_index = 0;

        Renderer*               renderer;

    }; // struct TemporalAntiAliasingPass

    //
    //
    struct MotionVectorPass : public FrameGraphRenderPass {

        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        PipelineHandle          camera_composite_pipeline;
        DescriptorSetHandle     camera_composite_descriptor_set;
        Renderer*               renderer;

    }; // struct MotionVectorPass

    //
    //
    struct DebugPass : public FrameGraphRenderPass {
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;
        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        BufferResource*         sphere_mesh_buffer;
        BufferResource*         sphere_mesh_indices;
        BufferResource*         sphere_matrices_buffer;
        BufferResource*         sphere_draw_indirect_buffer;
        u32                     sphere_index_count;

        BufferResource*         cone_mesh_buffer;
        BufferResource*         cone_mesh_indices;
        BufferResource*         cone_matrices_buffer;
        BufferResource*         cone_draw_indirect_buffer;
        u32                     cone_index_count;

        BufferResource*         line_buffer;

        u32                     bounding_sphere_count;

        DescriptorSetHandle     sphere_mesh_descriptor_set;
        DescriptorSetHandle     cone_mesh_descriptor_set;
        DescriptorSetHandle     line_descriptor_set;

        PipelineHandle          debug_lines_finalize_pipeline;
        DescriptorSetHandle     debug_lines_finalize_set;

        PipelineHandle          debug_lines_draw_pipeline;
        PipelineHandle          debug_lines_2d_draw_pipeline;
        DescriptorSetHandle     debug_lines_draw_set;

        BufferHandle            debug_line_commands_sb_cache;

        Material*               debug_material;

        PipelineHandle          gi_debug_probes_pipeline;
        DescriptorSetHandle     gi_debug_probes_descriptor_set;

        SceneGraph*             scene_graph;
        Renderer*               renderer;
    }; // struct DebugPass

    //
    //
    struct DoFPass : public FrameGraphRenderPass {

        struct DoFData {
            u32                 textures[ 4 ]; // diffuse, depth
            float               znear;
            float               zfar;
            float               focal_length;
            float               plane_in_focus;
            float               aperture;
        }; // struct DoFData

        void                    add_ui() override;
        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        Mesh                    mesh;
        Renderer*               renderer;

        TextureResource*        scene_mips;
        FrameGraphResource*     depth_texture;

        float                   znear;
        float                   zfar;
        float                   focal_length;
        float                   plane_in_focus;
        float                   aperture;
    }; // struct DoFPass

    //
    //
    struct CullingEarlyPass : public FrameGraphRenderPass {
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        Renderer*               renderer;

        PipelineHandle          frustum_cull_pipeline;
        DescriptorSetHandle     frustum_cull_descriptor_set[k_max_frames];
        SamplerHandle           depth_pyramid_sampler;
        u32                     depth_pyramid_texture_index;

    }; // struct CullingPrePass

    //
    //
    struct CullingLatePass : public FrameGraphRenderPass {
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        Renderer*               renderer;

        PipelineHandle          frustum_cull_pipeline;
        DescriptorSetHandle     frustum_cull_descriptor_set[k_max_frames];
        SamplerHandle           depth_pyramid_sampler;
        u32                     depth_pyramid_texture_index;

    }; // struct CullingLatePass

    //
    //
    struct RayTracingTestPass : public FrameGraphRenderPass {
        struct GpuData {
            u32 sbt_offset; // shader binding table offset
            u32 sbt_stride; // shader binding table stride
            u32 miss_index;
            u32 out_image_index;
        };

        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        Renderer*               renderer;

        PipelineHandle          pipeline;
        DescriptorSetHandle     descriptor_set[ k_max_frames ];
        TextureHandle           render_target;
        bool                    owns_render_target;
        BufferHandle            uniform_buffer[ k_max_frames ];

    }; // struct RayTracingTestPass

    //
    //
    struct ShadowVisibilityPass : public FrameGraphRenderPass {
        struct GpuShadowVisibilityConstants {
            u32 visibility_cache_texture_index;
            u32 variation_texture_index;
            u32 variation_cache_texture_index;
            u32 samples_count_cache_texture_index;

            u32 motion_vectors_texture_index;
            u32 normals_texture_index;
            u32 filtered_visibility_texture;
            u32 filetered_variation_texture;

            u32 frame_index;
            f32 resolution_scale;
            f32 resolution_scale_rcp;
            u32 pad;
        };

        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;
        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene );

        void                    recreate_textures( GpuDevice& gpu, u32 lights_count );

        Renderer*               renderer;

        PipelineHandle          variance_pipeline;
        PipelineHandle          visibility_pipeline;
        PipelineHandle          visibility_filtering_pipeline;
        DescriptorSetHandle     descriptor_set[ k_max_frames ];

        TextureHandle           variation_texture;
        TextureHandle           variation_cache_texture;
        TextureHandle           visibility_cache_texture;
        TextureHandle           samples_count_cache_texture;

        TextureHandle           filtered_visibility_texture;
        TextureHandle           filtered_variation_texture;

        TextureHandle           normals_texture;

        BufferHandle            gpu_pass_constants;

        FrameGraphResource*     shadow_visibility_resource;

        bool                    clear_resources;
        u32                     last_active_lights_count = 0;

        f32                     texture_scale;

    }; // struct ShadowVisibilityPass


    //
    //
    struct IndirectPass : public FrameGraphRenderPass {

        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        u32                     get_total_probes()                  { return probe_count_x * probe_count_y * probe_count_z; }
        u32                     get_total_rays()                    { return probe_rays * probe_count_x * probe_count_y * probe_count_z; }

        Renderer*               renderer;

        BufferHandle            ddgi_constants_buffer;
        BufferHandle            ddgi_probe_status_buffer;

        PipelineHandle          probe_raytrace_pipeline;
        DescriptorSetHandle     probe_raytrace_descriptor_set;
        TextureHandle           probe_raytrace_radiance_texture;

        PipelineHandle          probe_grid_update_irradiance_pipeline;
        PipelineHandle          probe_grid_update_visibility_pipeline;
        DescriptorSetHandle     probe_grid_update_descriptor_set;
        TextureHandle           probe_grid_irradiance_texture;
        TextureHandle           probe_grid_visibility_texture;

        PipelineHandle          calculate_probe_offset_pipeline;
        PipelineHandle          calculate_probe_statuses_pipeline;
        TextureHandle           probe_offsets_texture;

        DescriptorSetHandle     sample_irradiance_descriptor_set;
        PipelineHandle          sample_irradiance_pipeline;

        TextureHandle           indirect_texture;
        TextureHandle           normals_texture;
        TextureHandle           depth_pyramid_texture;
        TextureHandle           depth_fullscreen_texture;

        u32                     probe_count_x = 20;
        u32                     probe_count_y = 12;
        u32                     probe_count_z = 20;

        i32                     per_frame_probe_updates = 0;
        i32                     probe_update_offset = 0;

        i32                     probe_rays = 128;
        i32                     irradiance_atlas_width;
        i32                     irradiance_atlas_height;
        i32                     irradiance_probe_size = 6;  // Irradiance is a 6x6 quad with 1 pixel borders for bilinear filtering, total 8x8

        i32                     visibility_atlas_width;
        i32                     visibility_atlas_height;
        i32                     visibility_probe_size = 6;

        bool                    half_resolution_output = false;

    }; // struct IndirectPass

    //
    //
    struct ReflectionsPass : public FrameGraphRenderPass {
        struct GpuReflectionsConstants {
            u32 sbt_offset; // shader binding table offset
            u32 sbt_stride; // shader binding table stride
            u32 miss_index;
            u32 out_image_index;

            u32 gbuffer_texures[4]; // x = roughness, y = normals, z = indirect lighting
        };

        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        Renderer*               renderer;

        BufferHandle            reflections_constants_buffer;

        TextureHandle           reflections_texture;
        TextureHandle           indirect_texture;
        TextureHandle           roughness_texture;
        TextureHandle           normals_texture;

        DescriptorSetHandle     reflections_descriptor_set;
        PipelineHandle          reflections_pipeline;

    }; // ReflectionsPass

    //
    //
    struct SVGFAccumulationPass : public FrameGraphRenderPass {
        struct GpuConstants {
            u32 motion_vectors_texture_index;
            u32 mesh_id_texture_index;
            u32 normals_texture_index;
            u32 depth_normal_dd_texture_index;

            u32 history_mesh_id_texture_index;
            u32 history_normals_texture_index;
            u32 history_depth_texture;
            u32 reflections_texture_index;

            u32 history_reflections_texture_index;
            u32 history_moments_texture_index;
            u32 integrated_color_texture_index;
            u32 integrated_moments_texture_index;

            u32 variance_texture_index;
            u32 filtered_color_texture_index;
            u32 updated_variance_texture_index;
        };

        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        Renderer*               renderer;

        BufferHandle            gpu_constants;

        TextureHandle           reflections_texture;
        TextureHandle           motion_vectors_texture;
        TextureHandle           depth_texture;
        TextureHandle           normals_texture;
        TextureHandle           mesh_id_texture;
        TextureHandle           depth_normal_dd_texture;
        TextureHandle           integrated_color_texture;
        TextureHandle           integrated_moments_texture;

        TextureHandle           last_frame_normals_texture;
        TextureHandle           last_frame_depth_texture;
        TextureHandle           last_frame_mesh_id_texture;
        TextureHandle           reflections_history_texture;
        TextureHandle           moments_history_texture;

        DescriptorSetHandle     descriptor_set;
        PipelineHandle          pipeline;

    }; // SVGFAccumulationPass

    //
    //
    struct SVGFVariancePass : public FrameGraphRenderPass {
        struct GpuConstants {
            u32 motion_vectors_texture_index;
            u32 mesh_id_texture_index;
            u32 normals_texture_index;
            u32 depth_normal_dd_texture_index;

            u32 history_mesh_id_texture_index;
            u32 history_normals_texture_index;
            u32 history_depth_texture;
            u32 reflections_texture_index;

            u32 history_reflections_texture_index;
            u32 history_moments_texture_index;
            u32 integrated_color_texture_index;
            u32 integrated_moments_texture_index;

            u32 variance_texture_index;
            u32 filtered_color_texture_index;
            u32 updated_variance_texture_index;
        };

        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        Renderer*               renderer;

        BufferHandle            gpu_constants;

        TextureHandle           variance_texture;
        TextureHandle           reflections_texture;
        TextureHandle           motion_vectors_texture;
        TextureHandle           depth_texture;
        TextureHandle           normals_texture;
        TextureHandle           mesh_id_texture;
        TextureHandle           depth_normal_dd_texture;
        TextureHandle           integrated_color_texture;
        TextureHandle           integrated_moments_texture;

        TextureHandle           last_frame_normals_texture;
        TextureHandle           last_frame_depth_texture;
        TextureHandle           last_frame_mesh_id_texture;
        TextureHandle           reflections_history_texture;
        TextureHandle           moments_history_texture;

        DescriptorSetHandle     descriptor_set;
        PipelineHandle          pipeline;

    }; // SVGFVariancePass

    //
    //
    struct SVGFWaveletPass : public FrameGraphRenderPass {
        static const u32 k_num_passes = 5;

        struct GpuConstants {
            u32 motion_vectors_texture_index;
            u32 mesh_id_texture_index;
            u32 normals_texture_index;
            u32 depth_normal_dd_texture_index;

            u32 history_mesh_id_texture_index;
            u32 history_normals_texture_index;
            u32 history_depth_texture;
            u32 reflections_texture_index;

            u32 history_reflections_texture_index;
            u32 history_moments_texture_index;
            u32 integrated_color_texture_index;
            u32 integrated_moments_texture_index;

            u32 variance_texture_index;
            u32 filtered_color_texture_index;
            u32 updated_variance_texture_index;
        };

        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator ) override;
        void                    upload_gpu_data( RenderScene& scene ) override;
        void                    free_gpu_resources( GpuDevice& gpu ) override;

        void                    update_dependent_resources( GpuDevice& gpu, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        Renderer*               renderer;

        TextureHandle           variance_texture;
        TextureHandle           reflections_texture;
        TextureHandle           motion_vectors_texture;
        TextureHandle           depth_texture;
        TextureHandle           normals_texture;
        TextureHandle           mesh_id_texture;
        TextureHandle           depth_normal_dd_texture;
        TextureHandle           integrated_color_texture;
        TextureHandle           integrated_moments_texture;

        TextureHandle           last_frame_normals_texture;
        TextureHandle           last_frame_depth_texture;
        TextureHandle           last_frame_mesh_id_texture;
        TextureHandle           reflections_history_texture;
        TextureHandle           moments_history_texture;

        TextureHandle           ping_pong_color_texture;
        TextureHandle           ping_pong_variance_texture;

        TextureHandle           svgf_output_texture;

        BufferHandle            gpu_constants[ k_num_passes ];

        DescriptorSetHandle     descriptor_set[ k_num_passes ];
        PipelineHandle          pipeline;

    }; // SVGFWaveletPass

    //
    //
    struct DebugRenderer {

        void                    init( RenderScene& scene, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    shutdown();

        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene );


        void                    line( const vec3s& from, const vec3s& to, Color color );
        void                    line_2d( const vec2s& from, const vec2s& to, Color color );
        void                    line( const vec3s& from, const vec3s& to, Color color0, Color color1 );

        void                    aabb( const vec3s& min, const vec3s max, Color color );

        Renderer*               renderer;

        // CPU rendering resources
        BufferHandle            lines_vb;
        BufferHandle            lines_vb_2d;

        u32                     current_line;
        u32                     current_line_2d;

        // Shared resources
        PipelineHandle          debug_lines_draw_pipeline;
        PipelineHandle          debug_lines_2d_draw_pipeline;
        DescriptorSetHandle     debug_lines_draw_set;

    }; // struct DebugRenderer

    //
    //
    struct RenderScene {
        virtual                 ~RenderScene() { };

        virtual void            init( SceneGraph* scene_graph, Allocator* resident_allocator, Renderer* renderer_ ) { };
        virtual void            add_mesh( cstring filename, cstring path, StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) { };
        virtual void            shutdown( Renderer* renderer ) { };

        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height );

        virtual void            prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph ) { };

        CommandBuffer*          update_physics( f32 delta_time, f32 air_density, f32 spring_stiffness, f32 spring_damping, vec3s wind_direction, bool reset_simulation );
        void                    update_animations( f32 delta_time );
        void                    update_joints();

        void                    upload_gpu_data( UploadGpuDataContext& context );
        void                    draw_mesh_instance( CommandBuffer* gpu_commands, MeshInstance& mesh_instance, bool transparent );

        // Helpers based on shaders. Ideally this would be coming from generated cpp files.
        void                    add_scene_descriptors( DescriptorSetCreation& descriptor_set_creation, GpuTechniquePass& pass );
        void                    add_mesh_descriptors( DescriptorSetCreation& descriptor_set_creation, GpuTechniquePass& pass );
        void                    add_meshlet_descriptors( DescriptorSetCreation& descriptor_set_creation, GpuTechniquePass& pass );
        void                    add_debug_descriptors( DescriptorSetCreation& descriptor_set_creation, GpuTechniquePass& pass );
        void                    add_lighting_descriptors( DescriptorSetCreation& descriptor_set_creation, GpuTechniquePass& pass, u32 frame_index );

        DebugRenderer           debug_renderer;

        // Mesh and MeshInstances
        Array<Mesh>             meshes;
        Array<MeshInstance>     mesh_instances;
        Array<u32>              gltf_mesh_to_mesh_offset;

        // Meshlet data
        Array<GpuMeshlet>       meshlets;
        Array<GpuMeshletVertexPosition> meshlets_vertex_positions;
        Array<GpuMeshletVertexData> meshlets_vertex_data;
        Array<u32>              meshlets_data;
        u32                     meshlets_index_count;

        // Animation and skinning data
        Array<Animation>        animations;
        Array<Skin>             skins;

        // Lights
        Array<Light>            lights;
        Array<u32>              lights_lut;
        vec3s                   mesh_aabb[2]; // 0 min, 1 max
        u32                     active_lights   = 1;
        bool                    shadow_constants_cpu_update = true;

        StringBuffer            names_buffer;   // Buffer containing all names of nodes, resources, etc.

        SceneGraph*             scene_graph;

        GpuSceneData            scene_data;

        // Gpu buffers
        BufferHandle            scene_cb        = k_invalid_buffer;
        BufferHandle            meshes_sb       = k_invalid_buffer;
        BufferHandle            mesh_bounds_sb  = k_invalid_buffer;
        BufferHandle            mesh_instances_sb = k_invalid_buffer;
        BufferHandle            physics_cb      = k_invalid_buffer;
        BufferHandle            meshlets_sb     = k_invalid_buffer;
        BufferHandle            meshlets_vertex_pos_sb = k_invalid_buffer;
        BufferHandle            meshlets_vertex_data_sb = k_invalid_buffer;
        BufferHandle            meshlets_data_sb = k_invalid_buffer;
        BufferHandle            meshlets_instances_sb[ k_max_frames ];
        BufferHandle            meshlets_index_buffer_sb[ k_max_frames ];
        BufferHandle            meshlets_visible_instances_sb[ k_max_frames ];

        // Light buffers
        BufferHandle            lights_list_sb  = k_invalid_buffer;
        BufferHandle            lights_lut_sb[ k_max_frames ];
        BufferHandle            lights_tiles_sb[ k_max_frames ];
        BufferHandle            lights_indices_sb[ k_max_frames ];
        BufferHandle            lighting_constants_cb[ k_max_frames ];

        // Gpu debug draw
        BufferHandle            debug_line_sb           = k_invalid_buffer;
        BufferHandle            debug_line_count_sb     = k_invalid_buffer;
        BufferHandle            debug_line_commands_sb  = k_invalid_buffer;
        DescriptorSetHandle     debug_line_finalize_set = k_invalid_set;
        DescriptorSetHandle     debug_line_draw_set     = k_invalid_set;

        // Indirect data
        BufferHandle            mesh_task_indirect_count_early_sb[ k_max_frames ];
        BufferHandle            mesh_task_indirect_early_commands_sb[ k_max_frames ];
        BufferHandle            mesh_task_indirect_culled_commands_sb[ k_max_frames ];

        BufferHandle            mesh_task_indirect_count_late_sb[ k_max_frames ];
        BufferHandle            mesh_task_indirect_late_commands_sb[ k_max_frames ];

        BufferHandle            meshlet_instances_indirect_count_sb[ k_max_frames ];

        Array<BufferHandle>     geometry_transform_buffers;

        TextureHandle           fragment_shading_rate_image;
        TextureHandle           motion_vector_texture;
        TextureHandle           visibility_motion_vector_texture;

        Array<VkAccelerationStructureGeometryKHR> geometries;
        Array<VkAccelerationStructureBuildRangeInfoKHR> build_range_infos;

        VkAccelerationStructureKHR blas;
        BufferHandle            blas_buffer;

        VkAccelerationStructureKHR tlas;
        BufferHandle            tlas_buffer;

        BufferHandle            ddgi_constants_cache{ k_invalid_buffer };
        BufferHandle            ddgi_probe_status_cache{ k_invalid_buffer };

        GpuMeshDrawCounts       mesh_draw_counts;

        DescriptorSetHandle     meshlet_emulation_descriptor_set[ k_max_frames ];
        DescriptorSetHandle     meshlet_visibility_descriptor_set[ k_max_frames ];
        DescriptorSetHandle     mesh_shader_early_descriptor_set[ k_max_frames ];
        DescriptorSetHandle     mesh_shader_late_descriptor_set[ k_max_frames ];
        DescriptorSetHandle     mesh_shader_transparent_descriptor_set[ k_max_frames ];

        Allocator*              resident_allocator;
        Renderer*               renderer;

        u32                     cubemap_shadows_index = 0;
        u32                     lighting_debug_texture_index = 0;
        u32                     cubemap_debug_array_index = 0;
        u32                     cubemap_debug_face_index = 5;
        bool                    cubemap_face_debug_enabled = false;
        u32                     blue_noise_128_rg_texture_index = 0;

        // PBR
        f32                     forced_metalness = -1.f;
        f32                     forced_roughness = -1.f;

        // Volumetric Fog controls
        u32                     volumetric_fog_texture_index = 0;
        u32                     volumetric_fog_tile_size = 16;
        u32                     volumetric_fog_tile_count_x = 128;
        u32                     volumetric_fog_tile_count_y = 128;
        u32                     volumetric_fog_slices = 128;
        f32                     volumetric_fog_density = 0.0f;
        f32                     volumetric_fog_scattering_factor = 0.1f;
        f32                     volumetric_fog_temporal_reprojection_percentage = 0.2f;
        f32                     volumetric_fog_phase_anisotropy_01 = 0.2f;
        bool                    volumetric_fog_use_temporal_reprojection = true;
        bool                    volumetric_fog_use_spatial_filtering = true;
        u32                     volumetric_fog_phase_function_type = 0;
        f32                     volumetric_fog_height_fog_density = 0.0f;
        f32                     volumetric_fog_height_fog_falloff = 1.0f;
        f32                     volumetric_fog_noise_scale = 0.5f;
        f32                     volumetric_fog_lighting_noise_scale = 0.11f;
        u32                     volumetric_fog_noise_type = 0;
        f32                     volumetric_fog_noise_position_scale = 1.0f;
        f32                     volumetric_fog_noise_speed_scale = 0.2f;
        vec3s                   volumetric_fog_box_position = vec3s{ 0, 0, 0 };
        vec3s                   volumetric_fog_box_size = vec3s{ 1.f, 2.f, 0.5f };
        f32                     volumetric_fog_box_density = 3.0f;
        u32                     volumetric_fog_box_color = raptor::Color::green;
        f32                     volumetric_fog_temporal_reprojection_jittering_scale = 0.2f;
        f32                     volumetric_fog_application_dithering_scale = 0.005f;
        bool                    volumetric_fog_application_apply_opacity_anti_aliasing = false;
        bool                    volumetric_fog_application_apply_tricubic_filtering = false;
        // Temporal Anti-Aliasing
        bool                    taa_enabled = true;
        bool                    taa_jittering_enabled = true;
        i32                     taa_mode = 1;
        bool                    taa_use_inverse_luminance_filtering = true;
        bool                    taa_use_temporal_filtering = true;
        bool                    taa_use_luminance_difference_filtering = true;
        bool                    taa_use_ycocg = false;
        i32                     taa_velocity_sampling_mode = 1;
        i32                     taa_history_sampling_filter = 1;
        i32                     taa_history_constraint_mode = 4;
        i32                     taa_current_color_filter = 1;
        // Post process
        i32                     post_tonemap_mode = 0;
        f32                     post_exposure = 1.0f;
        f32                     post_sharpening_amount = 0.2f;
        u32                     post_zoom_scale = 2;
        bool                    post_enable_zoom = false;
        bool                    post_block_zoom_input = false;
        // Global illumination
        bool                    gi_show_probes = false;
        vec3s                   gi_probe_grid_position{ -10.0,0.5,-10.0 };
        vec3s                   gi_probe_spacing{ 1.f, 1.f, 1.f };
        f32                     gi_probe_sphere_scale = 0.1f;
        f32                     gi_max_probe_offset = 0.4f;
        f32                     gi_self_shadow_bias = 0.3f;
        f32                     gi_hysteresis = 0.95f;
        bool                    gi_debug_border = false;
        bool                    gi_debug_border_type = false;
        bool                    gi_debug_border_source = false;
        u32                     gi_total_probes = 0;
        f32                     gi_intensity = 1.0f;
        bool                    gi_use_visibility = true;
        bool                    gi_use_backface_smoothing = true;
        bool                    gi_use_perceptual_encoding = true;
        bool                    gi_use_backface_blending = true;
        bool                    gi_use_probe_offsetting = true;
        bool                    gi_recalculate_offsets = false;     // When moving grid or changing spaces, recalculate offsets.
        bool                    gi_use_probe_status = false;
        bool                    gi_use_half_resolution = true;
        bool                    gi_use_infinite_bounces = true;
        f32                     gi_infinite_bounces_multiplier = 0.75f;
        i32                     gi_per_frame_probes_update = 1000;

        bool                    use_meshlets = true;
        bool                    use_meshlets_emulation = false;
        bool                    show_debug_gpu_draws = false;
        bool                    pointlight_rendering = true;
        bool                    pointlight_use_meshlets = true;
        bool                    use_tetrahedron_shadows = false;
        bool                    show_light_edit_debug_draws = false;

        bool                    cubeface_flip[ 6 ];

        f32                     global_scale = 1.f;
    }; // struct RenderScene

    //
    //
    struct FrameRenderer {

        void                    init( Allocator* resident_allocator, Renderer* renderer,
                                      FrameGraph* frame_graph, SceneGraph* scene_graph,
                                      RenderScene* scene );
        void                    shutdown();

        void                    upload_gpu_data( UploadGpuDataContext& context );
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene );

        void                    prepare_draws( StackAllocator* scratch_allocator );
        void                    update_dependent_resources();

        Allocator*              resident_allocator;
        SceneGraph*             scene_graph;

        Renderer*               renderer;
        FrameGraph*             frame_graph;

        RenderScene*            scene;

        Array<FrameGraphRenderPass*> render_passes;

        // Render passes
        DepthPrePass            depth_pre_pass;
        GBufferPass             gbuffer_pass_early;
        LateGBufferPass         gbuffer_pass_late;
        LightPass               light_pass;
        TransparentPass         transparent_pass;
        DoFPass                 dof_pass;
        DebugPass               debug_pass;
        CullingEarlyPass        mesh_occlusion_early_pass;
        CullingLatePass         mesh_occlusion_late_pass;
        DepthPyramidPass        depth_pyramid_pass;
        PointlightShadowPass    pointlight_shadow_pass;
        VolumetricFogPass       volumetric_fog_pass;
        TemporalAntiAliasingPass temporal_anti_aliasing_pass;
        MotionVectorPass        motion_vector_pass;
        RayTracingTestPass      ray_tracing_test_pass;
        ShadowVisibilityPass    shadow_visiblity_pass;
        IndirectPass            indirect_pass;
        ReflectionsPass         reflections_pass;
        SVGFAccumulationPass    svgf_accumulation_pass;
        SVGFVariancePass        svgf_variance_pass;
        SVGFWaveletPass         svgf_wavelet_pass;

        // Fullscreen data
        GpuTechnique*           fullscreen_tech = nullptr;
        DescriptorSetHandle     fullscreen_ds;
        PipelineHandle          passthrough_pipeline;
        PipelineHandle          main_post_pipeline;
        BufferHandle            post_uniforms_buffer;

    }; // struct FrameRenderer


    // DrawTask ///////////////////////////////////////////////////////////

    //
    //
    struct DrawTask : public enki::ITaskSet {

        GpuDevice*              gpu         = nullptr;
        FrameGraph*             frame_graph = nullptr;
        Renderer*               renderer    = nullptr;
        ImGuiService*           imgui       = nullptr;
        GpuVisualProfiler*      gpu_profiler = nullptr;
        RenderScene*            scene       = nullptr;
        FrameRenderer*          frame_renderer = nullptr;
        u32                     thread_id   = 0;
        // NOTE(marco): gpu state might change between init and execute!
        u32                     current_frame_index = 0;
        FramebufferHandle       current_framebuffer = { k_invalid_index };

        void init( GpuDevice* gpu_, FrameGraph* frame_graph_, Renderer* renderer_,
                   ImGuiService* imgui_, GpuVisualProfiler* gpu_profiler_, RenderScene* scene_,
                   FrameRenderer* frame_renderer );

        void ExecuteRange( enki::TaskSetPartition range_, uint32_t threadnum_ ) override;

    }; // struct DrawTask


    // Math utils /////////////////////////////////////////////////////////
    void                        get_bounds_for_axis( const vec3s& a, const vec3s& C, float r, float nearZ, vec3s& L, vec3s& U );
    vec3s                       project( const mat4s& P, const vec3s& Q );

    void                        project_aabb_cubemap_positive_x( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max );
    void                        project_aabb_cubemap_negative_x( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max );
    void                        project_aabb_cubemap_positive_y( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max );
    void                        project_aabb_cubemap_negative_y( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max );
    void                        project_aabb_cubemap_positive_z( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max );
    void                        project_aabb_cubemap_negative_z( const vec3s aabb[ 2 ], f32& s_min, f32& s_max, f32& t_min, f32& t_max );

    // Numerical sequences, used to calculate jittering values.
    f32                         halton( i32 i, i32 b );
    f32                         interleaved_gradient_noise( vec2s pixel, i32 index );

    vec2s                       halton23_sequence( i32 index );
    vec2s                       m_robert_r2_sequence( i32 index );
    vec2s                       interleaved_gradient_sequence( i32 index );
    vec2s                       hammersley_sequence( i32 index, i32 num_samples );

} // namespace raptor
