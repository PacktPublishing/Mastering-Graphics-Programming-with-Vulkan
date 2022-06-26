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
        mat4s                   inverse_view_projection;

        vec4s                   eye;
        vec4s                   light_position;
        f32                     light_range;
        f32                     light_intensity;
        u32                     dither_texture_index;
        f32                     padding00;
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
        u32                     scene_graph_node_index  = u32_max;
        i32                     skin_index              = i32_max;

        bool                    has_skinning() const    { return skin_index != i32_max;}
        bool                    is_transparent() const  { return ( pbr_material.flags & ( DrawFlags_AlphaMask | DrawFlags_Transparent ) ) != 0; }
        bool                    is_double_sided() const { return ( pbr_material.flags & DrawFlags_DoubleSided ) == DrawFlags_DoubleSided; }
        bool                    is_cloth() const { return ( pbr_material.flags & DrawFlags_Cloth ) == DrawFlags_Cloth; }
    }; // struct Mesh

    //
    //
    struct MeshInstance {

        Mesh*                   mesh;
        u32                     material_pass_index;

    }; // struct MeshInstance

    //
    //
    struct GpuMeshData {
        mat4s                   world;
        mat4s                   inverse_world;

        u32                     textures[ 4 ]; // diffuse, roughness, normal, occlusion
        // PBR
        vec4s                   emissive; // emissive_color_factor + emissive texture index
        vec4s                   base_color_factor;
        vec4s                   metallic_roughness_occlusion_factor; // metallic, roughness, occlusion

        u32                     flags;
        f32                     alpha_cutoff;
        f32                     padding_[ 2 ];

        // Phong
        vec4s                   diffuse_colour;

        vec3s                   specular_colour;
        f32                     specular_exp;

        vec3s                   ambient_colour;
        f32                     padding2_;

    }; // struct GpuMeshData

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

        Array<MeshInstance>     mesh_instances;
        Renderer*               renderer;
    }; // struct DepthPrePass

    //
    //
    struct GBufferPass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

        Array<MeshInstance>     mesh_instances;
        Renderer*               renderer;
    }; // struct GBufferPass

    //
    //
    struct LighPass : public FrameGraphRenderPass {
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
    }; // struct LighPass

    //
    //
    struct TransparentPass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

        Array<MeshInstance>     mesh_instances;
        Renderer*               renderer;
    }; // struct TransparentPass

    //
    //
    struct DebugPass : public FrameGraphRenderPass {
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    free_gpu_resources();

        BufferResource*         sphere_mesh_buffer;
        BufferResource*         sphere_mesh_indices;
        BufferResource*         sphere_matrices;
        BufferResource*         line_buffer;

        u32                     sphere_index_count;

        DescriptorSetHandle     mesh_descriptor_set;
        DescriptorSetHandle     line_descriptor_set;

        Material*               debug_material;

        Array<MeshInstance>     mesh_instances;
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
        void                    on_resize( GpuDevice& gpu, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( RenderScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    upload_gpu_data();
        void                    free_gpu_resources();

        Mesh                    mesh;
        Renderer*               renderer;

        TextureResource*        scene_mips[ k_max_frames ];
        FrameGraphResource*     depth_texture;

        float                   znear;
        float                   zfar;
        float                   focal_length;
        float                   plane_in_focus;
        float                   aperture;
    }; // struct DoFPass

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
        void                    draw_mesh( CommandBuffer* gpu_commands, Mesh& mesh );

        Array<Mesh>             meshes;
        Array<Animation>        animations;
        Array<Skin>             skins;

        StringBuffer            names_buffer;   // Buffer containing all names of nodes, resources, etc.

        SceneGraph*             scene_graph;
        BufferHandle            scene_cb;
        BufferHandle            physics_cb = k_invalid_buffer;

        Allocator*              resident_allocator;
        Renderer*               renderer;

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
        GBufferPass             gbuffer_pass;
        LighPass                light_pass;
        TransparentPass         transparent_pass;
        DoFPass                 dof_pass;
        DebugPass               debug_pass;

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
