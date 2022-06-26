#pragma once

#include "graphics/gpu_resources.hpp"
#include "graphics/frame_graph.hpp"
#include "graphics/render_scene.hpp"
#include "graphics/renderer.hpp"

#include "foundation/gltf.hpp"

#include "external/enkiTS/TaskScheduler.h"

namespace raptor
{
    struct glTFScene;
    struct Material;

    //
    //
    struct PBRMaterial {

        Material*           material;

        BufferHandle        material_buffer;
        DescriptorSetHandle descriptor_set;

        // Indices used for bindless textures.
        u16                 diffuse_texture_index;
        u16                 roughness_texture_index;
        u16                 normal_texture_index;
        u16                 occlusion_texture_index;

        vec4s               base_color_factor;
        vec4s               metallic_roughness_occlusion_factor;

        f32                 alpha_cutoff;
        u32                 flags;
    }; // struct PBRMaterial

    //
    //
    struct Mesh {

        PBRMaterial         pbr_material;

        BufferHandle        index_buffer;
        BufferHandle        position_buffer;
        BufferHandle        tangent_buffer;
        BufferHandle        normal_buffer;
        BufferHandle        texcoord_buffer;

        u32                 position_offset;
        u32                 tangent_offset;
        u32                 normal_offset;
        u32                 texcoord_offset;

        VkIndexType         index_type;
        u32                 index_offset;

        u32                 primitive_count;
        u32                 scene_graph_node_index  = u32_max;

        bool                is_transparent() const  { return ( pbr_material.flags & ( DrawFlags_AlphaMask | DrawFlags_Transparent ) ) != 0; }
        bool                is_double_sided() const { return ( pbr_material.flags & DrawFlags_DoubleSided ) == DrawFlags_DoubleSided; }
    }; // struct Mesh

    //
    //
    struct MeshInstance {

        Mesh*               mesh;
        u32                 material_pass_index;

    }; // struct MeshInstance

    //
    //
    struct GpuMeshData {
        mat4s               world;
        mat4s               inverse_world;

        u32                 textures[ 4 ]; // diffuse, roughness, normal, occlusion
        vec4s               base_color_factor;
        vec4s               metallic_roughness_occlusion_factor; // metallic, roughness, occlusion
        f32                 alpha_cutoff;
        f32                 padding_[ 3 ];

        u32                 flags;
        u32                 padding1_[ 3 ];
    }; // struct GpuMeshData

    // Render Passes //////////////////////////////////////////////////////////

    //
    //
    struct DepthPrePass : public FrameGraphRenderPass {
        void                render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                prepare_draws( glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                free_gpu_resources();

        Array<MeshInstance> mesh_instances;
        Renderer*           renderer;
    }; // struct DepthPrePass

    //
    //
    struct GBufferPass : public FrameGraphRenderPass {
        void                render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                prepare_draws( glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                free_gpu_resources();

        Array<MeshInstance> mesh_instances;
        Renderer*           renderer;
    }; // struct GBufferPass

    //
    //
    struct LighPass : public FrameGraphRenderPass {
        void                render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                prepare_draws( glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                upload_materials();
        void                free_gpu_resources();

        Mesh                mesh;
        Renderer*           renderer;
    }; // struct LighPass

    //
    //
    struct TransparentPass : public FrameGraphRenderPass {
        void                render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;

        void                prepare_draws( glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                free_gpu_resources();

        Array<MeshInstance> mesh_instances;
        Renderer*           renderer;
    }; // struct TransparentPass

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
        void                    pre_render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    render( CommandBuffer* gpu_commands, RenderScene* render_scene ) override;
        void                    on_resize( GpuDevice& gpu, u32 new_width, u32 new_height ) override;

        void                    prepare_draws( glTFScene& scene, FrameGraph* frame_graph, Allocator* resident_allocator, StackAllocator* scratch_allocator );
        void                    upload_materials();
        void                    free_gpu_resources();

        Mesh                    mesh;
        Renderer*               renderer;

        TextureResource*        scene_mips;

        float                   znear;
        float                   zfar;
        float                   focal_length;
        float                   plane_in_focus;
        float                   aperture;
    }; // struct DoFPass

    //
    //
    struct glTFScene : public RenderScene {

        void                    init( cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) override;
        void                    shutdown( Renderer* renderer ) override;

        void                    register_render_passes( FrameGraph* frame_graph ) override;
        void                    prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph ) override;
        void                    upload_materials() override;
        void                    submit_draw_task( ImGuiService* imgui, GPUProfiler* gpu_profiler, enki::TaskScheduler* task_scheduler ) override;

        void                    draw_mesh( CommandBuffer* gpu_commands, Mesh& mesh );

        void                    get_mesh_vertex_buffer( i32 accessor_index, BufferHandle& out_buffer_handle, u32& out_buffer_offset );
        u16                     get_material_texture( GpuDevice& gpu, glTF::TextureInfo* texture_info );
        u16                     get_material_texture( GpuDevice& gpu, i32 gltf_texture_index );

        void                    fill_pbr_material( Renderer& renderer, glTF::Material& material, PBRMaterial& pbr_material );

        Array<Mesh>             meshes;

        DepthPrePass            depth_pre_pass;
        GBufferPass             gbuffer_pass;
        LighPass                light_pass;
        TransparentPass         transparent_pass;
        DoFPass                 dof_pass;

        // Fullscreen data
        GpuTechnique*           fullscreen_tech = nullptr;
        DescriptorSetHandle     fullscreen_ds;
        u32                     fullscreen_input_rt = u32_max;

        // All graphics resources used by the scene
        Array<TextureResource>  images;
        Array<SamplerResource>  samplers;
        Array<BufferResource>   buffers;

        glTF::glTF              gltf_scene; // Source gltf scene

        Renderer*               renderer;
        FrameGraph*             frame_graph;

    }; // struct GltfScene

    // glTFDrawTask ///////////////////////////////////////////////////////

    //
    //
    struct glTFDrawTask : public enki::ITaskSet {

        GpuDevice*              gpu         = nullptr;
        FrameGraph*             frame_graph = nullptr;
        Renderer*               renderer    = nullptr;
        ImGuiService*           imgui       = nullptr;
        GPUProfiler*            gpu_profiler = nullptr;
        glTFScene*              scene       = nullptr;
        u32                     thread_id   = 0;

        void init( GpuDevice* gpu_, FrameGraph* frame_graph_, Renderer* renderer_, ImGuiService* imgui_, GPUProfiler* gpu_profiler_, glTFScene* scene_ );

        void ExecuteRange( enki::TaskSetPartition range_, uint32_t threadnum_ ) override;

    }; // struct glTFDrawTask
}
