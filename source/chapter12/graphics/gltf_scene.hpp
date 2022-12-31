#pragma once

#include "graphics/gpu_resources.hpp"
#include "graphics/render_scene.hpp"

#include "foundation/gltf.hpp"

namespace raptor {
    //
    //
    struct glTFScene : public RenderScene {

        void                    init( cstring filename, cstring path, SceneGraph* scene_graph, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) override;
        void                    shutdown( Renderer* renderer ) override;

        void                    prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph ) override;

        void                    get_mesh_vertex_buffer( i32 accessor_index, u32 flag, BufferHandle& out_buffer_handle, u32& out_buffer_offset, u32& out_flags );
        u16                     get_material_texture( GpuDevice& gpu, glTF::TextureInfo* texture_info );
        u16                     get_material_texture( GpuDevice& gpu, i32 gltf_texture_index );

        void                    fill_pbr_material( Renderer& renderer, glTF::Material& material, PBRMaterial& pbr_material );

        // All graphics resources used by the scene
        Array<TextureResource>  images;
        Array<SamplerResource>  samplers;
        Array<BufferResource>   buffers;

        glTF::glTF              gltf_scene; // Source gltf scene

    }; // struct GltfScene

} // namespace raptor
