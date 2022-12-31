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

        void                                    init( cstring filename, cstring path, Allocator* resident_allocator, StackAllocator* temp_allocator, AsynchronousLoader* async_loader_ ) override;
        void                                    shutdown( Renderer* renderer ) override;

        void                                    prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph ) override;

        u32                                     load_texture( cstring texture_path, cstring path, StackAllocator* temp_allocator );

        // All graphics resources used by the scene
        Array<TextureResource>                  images;
        SamplerResource*                        sampler;
        Array<BufferResource>                   cpu_buffers;
        Array<BufferResource>                   gpu_buffers;

        const aiScene*                          assimp_scene;
        AsynchronousLoader*                     async_loader;

    }; // struct ObjScene

} // namespace raptor
