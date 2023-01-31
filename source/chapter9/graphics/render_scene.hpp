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
        u32                     padding0;
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

        u8                      skip_invisible_lights       : 1;
        u8                      use_mcguire_method          : 1;
        u8                      use_view_aabb               : 1;
        u8                      enable_camera_inside        : 1;
        u8                      force_fullscreen_light_aabb : 1;
        u8                      pad000                      : 3;

    }; // struct UploadGpuDataContext

    // Render Passes //////////////////////////////////////////////////////

    //
    //
    struct DepthPrePass : public FrameGraphRenderPass {
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

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

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

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

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

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

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

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

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    upload_gpu_data( RenderScene& scene );
        void                    free_gpu_resources();

        Mesh                    mesh;
        Renderer*               renderer;
        bool                    use_compute;

        BufferHandle            last_lights_buffer = k_invalid_buffer;

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

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

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
        void                    upload_gpu_data( RenderScene& scene );
        void                    free_gpu_resources();

        void                    recreate_dependent_resources( RenderScene& scene );

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
        BufferHandle            last_lights_buffer = k_invalid_buffer;

        PipelineHandle          shadow_resolution_pipeline;
        DescriptorSetHandle     shadow_resolution_descriptor_set[ k_max_frames ];
        BufferHandle            light_aabbs;
        BufferHandle            shadow_resolutions[ k_max_frames ];
        BufferHandle            shadow_resolutions_readback[ k_max_frames ];

        PagePoolHandle          shadow_maps_pool = k_invalid_page_pool;

        TextureHandle           cubemap_debug_face_texture;

    }; // struct CubemapShadowsPass

    //
    //
    struct DebugPass : public FrameGraphRenderPass {
        void                    render( u32 current_frame_index, CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

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

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    upload_gpu_data();
        void                    free_gpu_resources();

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

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

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

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

        Renderer*               renderer;

        PipelineHandle          frustum_cull_pipeline;
        DescriptorSetHandle     frustum_cull_descriptor_set[k_max_frames];
        SamplerHandle           depth_pyramid_sampler;
        u32                     depth_pyramid_texture_index;

    }; // struct CullingLatePass

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

        virtual void            init( cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) { };
        virtual void            shutdown( Renderer* renderer ) { };

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

        TextureHandle           fragment_shading_rate_image;

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

        bool                    use_meshlets = true;
        bool                    use_meshlets_emulation = false;
        bool                    show_debug_gpu_draws = false;
        bool                    pointlight_rendering = true;
        bool                    pointlight_use_meshlets = true;
        bool                    use_tetrahedron_shadows = false;

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

        Allocator*              resident_allocator;
        SceneGraph*             scene_graph;

        Renderer*               renderer;
        FrameGraph*             frame_graph;

        RenderScene*            scene;

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

        // Fullscreen data
        GpuTechnique*           fullscreen_tech = nullptr;
        DescriptorSetHandle     fullscreen_ds;

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

} // namespace raptor
