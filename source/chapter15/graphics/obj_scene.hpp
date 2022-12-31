#pragma once

#include "graphics/gpu_resources.hpp"
#include "graphics/render_scene.hpp"
#include "graphics/renderer.hpp"

struct aiScene;

namespace raptor
{
    //
    //
    struct ObjScene :  public RenderScene {

        void                                    init( SceneGraph* scene_graph, Allocator* resident_allocator, Renderer* renderer_ ) override;
        void                                    add_mesh( cstring filename, cstring path, StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) override;
        void                                    shutdown( Renderer* renderer ) override;

        void                                    prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph ) override;

        u32                                     load_texture( cstring texture_path, cstring path, AsynchronousLoader* async_loader, StackAllocator* temp_allocator );

        // All graphics resources used by the scene
        Array<TextureResource>                  images;
        SamplerResource*                        sampler;
        Array<BufferResource>                   cpu_buffers;
        Array<BufferResource>                   gpu_buffers;

        Array<const aiScene*>                   assimp_scenes;

    }; // struct ObjScene

} // namespace raptor
