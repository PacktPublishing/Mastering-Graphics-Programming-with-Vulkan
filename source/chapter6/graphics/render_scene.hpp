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

    static const u16    k_invalid_scene_texture_index      = u16_max;
    static const u32    k_material_descriptor_set_index    = 1;
    static const u32    k_max_joint_count                  = 12;
    static const u32    k_max_depth_pyramid_levels         = 16;

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
    struct GpuSceneData {
        mat4s                   view_projection;
        mat4s                   view_projection_debug;
        mat4s                   inverse_view_projection;
        mat4s                   world_to_camera;    // view matrix
        mat4s                   world_to_camera_debug;
        mat4s                   previous_view_projection;

        vec4s                   eye;
        vec4s                   eye_debug;
        vec4s                   light_position;

        f32                     light_range;
        f32                     light_intensity;
        u32                     dither_texture_index;
        f32                     z_near;

        f32                     z_far;
        f32                     projection_00;
        f32                     projection_11;
        u32                     frustum_cull_meshes;

        u32                     frustum_cull_meshlets;
        u32                     occlusion_cull_meshes;
        u32                     occlusion_cull_meshlets;
        u32                     freeze_occlusion_camera;

        f32                     resolution_x;
        f32                     resolution_y;
        f32                     aspect_ratio;
        f32                     pad0001;

        vec4s                   frustum_planes[ 6 ];

    }; // struct GpuSceneData

    struct glTFScene;
    struct Material;

    //
    //
    struct PBRMaterial {

        Material*               material        = nullptr;

        BufferHandle            material_buffer = k_invalid_buffer;
        DescriptorSetHandle     descriptor_set  = k_invalid_set;

        // Indices used for bindless textures.
        u16                     diffuse_texture_index   = u16_max;
        u16                     roughness_texture_index = u16_max;
        u16                     normal_texture_index    = u16_max;
        u16                     occlusion_texture_index = u16_max;
        u16                     emissive_texture_index  = u16_max;

        // PBR
        vec4s                   base_color_factor   = {1.f, 1.f, 1.f, 1.f};
        vec3s                   emissive_factor     = {0.f, 0.f, 0.f};
        vec4s                   metallic_roughness_occlusion_factor = {1.f, 1.f, 1.f, 1.f};
        f32                     alpha_cutoff        = 1.f;

        // Phong
        vec4s                   diffuse_colour      = { 1.f, 1.f, 1.f, 1.f };
        vec3s                   specular_colour     = { 1.f, 1.f, 1.f };
        f32                     specular_exp        = 1.f;
        vec3s                   ambient_colour      = { 0.f, 0.f, 0.f};

        u32                     flags               = 0;;
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
        u32                     padding0_;
        u32                     padding1_;

        // Phong
        vec4s                   diffuse_colour;

        vec3s                   specular_colour;
        f32                     specular_exp;

        vec3s                   ambient_colour;
        f32                     padding2_;

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
        VkDrawIndexedIndirectCommand indirect; // 5 uint32_t
        VkDrawMeshTasksIndirectCommandNV indirectMS; // 2 uint32_t
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
    struct Light {

        Color                   color;
        f32                     intensity;

        vec3s                   position;
        f32                     radius;

    }; // struct Light

    // Render Passes //////////////////////////////////////////////////////

    //
    //
    struct DepthPrePass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

        Array<MeshInstanceDraw> mesh_instance_draws;
        Renderer*               renderer;
        u32                     meshlet_technique_index;
    }; // struct DepthPrePass

    //
    //
    struct DepthPyramidPass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    on_resize( GpuDevice& gpu, FrameGraph* frame_graph, u32 new_width, u32 new_height ) override;
        void                    post_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph ) override;

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
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

        Array<MeshInstanceDraw> mesh_instance_draws;
        Renderer*               renderer;
        u32                     meshlet_technique_index;
    }; // struct GBufferPass

    //
    //
    struct LateGBufferPass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

        Array<MeshInstanceDraw> mesh_instance_draws;
        Renderer*               renderer;
        u32                     meshlet_technique_index;
    }; // struct LateGBufferPass

    //
    //
    struct LightPass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    upload_gpu_data();
        void                    free_gpu_resources();

        Mesh                    mesh;
        Renderer*               renderer;
        bool                    use_compute;

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
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

        Array<MeshInstanceDraw> mesh_instance_draws;
        Renderer*               renderer;
        u32                     meshlet_technique_index;
    }; // struct TransparentPass

    //
    //
    struct DebugPass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph ) override;

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
        void                    pre_render( u32 current_frame_index, CommandBuffer* gpu_commands, FrameGraph* frame_graph ) override;
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
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

    struct MeshPass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
    }; // struct MeshPass

    //
    //
    struct CullingEarlyPass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

        Renderer*               renderer;

        PipelineHandle          frustum_cull_pipeline;
        DescriptorSetHandle     frustum_cull_descriptor_set[k_max_frames];
        SamplerHandle           depth_pyramid_sampler;
        u32                     depth_pyramid_texture_index;

    }; // struct CullingPrePass

    struct CullingLatePass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

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
    struct RenderScene {
        virtual                 ~RenderScene() { };

        virtual void            init( cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) { };
        virtual void            shutdown( Renderer* renderer ) { };

        virtual void            prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph ) { };

        CommandBuffer*          update_physics( f32 delta_time, f32 air_density, f32 spring_stiffness, f32 spring_damping, vec3s wind_direction, bool reset_simulation );
        void                    update_animations( f32 delta_time );
        void                    update_joints();

        void                    upload_gpu_data();
        void                    draw_mesh_instance( CommandBuffer* gpu_commands, MeshInstance& mesh_instance );

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

        // Gpu debug draw
        BufferHandle            debug_line_sb    = k_invalid_buffer;
        BufferHandle            debug_line_count_sb = k_invalid_buffer;
        BufferHandle            debug_line_commands_sb = k_invalid_buffer;
        DescriptorSetHandle     debug_line_finalize_set;
        DescriptorSetHandle     debug_line_draw_set;

        // Indirect data
        BufferHandle            mesh_task_indirect_count_early_sb[ k_max_frames ];
        BufferHandle            mesh_task_indirect_early_commands_sb[ k_max_frames ];
        BufferHandle            mesh_task_indirect_culled_commands_sb[ k_max_frames ];

        BufferHandle            mesh_task_indirect_count_late_sb[ k_max_frames ];
        BufferHandle            mesh_task_indirect_late_commands_sb[ k_max_frames ];

        GpuMeshDrawCounts       mesh_draw_counts;

        DescriptorSetHandle     mesh_shader_early_descriptor_set[ k_max_frames ];
        DescriptorSetHandle     mesh_shader_late_descriptor_set[ k_max_frames ];

        Allocator*              resident_allocator;
        Renderer*               renderer;

        bool                    use_meshlets = false;
        bool                    show_debug_gpu_draws = false;

        f32                     global_scale = 1.f;
    }; // struct RenderScene

    //
    //
    struct FrameRenderer {

        void                    init( Allocator* resident_allocator, Renderer* renderer,
                                      FrameGraph* frame_graph, SceneGraph* scene_graph,
                                      RenderScene* scene );
        void                    shutdown();

        void                    upload_gpu_data();
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
        LightPass                light_pass;
        TransparentPass         transparent_pass;
        DoFPass                 dof_pass;
        DebugPass               debug_pass;
        MeshPass                mesh_pass;
        CullingEarlyPass        mesh_occlusion_early_pass;
        CullingLatePass         mesh_occlusion_late_pass;
        DepthPyramidPass        depth_pyramid_pass;

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
        GpuVisualProfiler*            gpu_profiler = nullptr;
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

    }; // struct glTFDrawTask

} // namespace raptor
