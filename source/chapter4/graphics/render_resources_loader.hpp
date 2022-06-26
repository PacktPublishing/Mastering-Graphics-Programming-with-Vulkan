#pragma once

#include "graphics/renderer.hpp"

namespace raptor {

    struct FrameGraph;

    //
    //
    struct RenderResourcesLoader {

        void            init( raptor::Renderer* renderer, raptor::StackAllocator* temp_allocator, raptor::FrameGraph* frame_graph );
        void            shutdown();

        void            load_gpu_technique( cstring json_path );
        void            load_texture( cstring path );

        Renderer*       renderer;
        FrameGraph*     frame_graph;
        StackAllocator* temp_allocator;

    }; // struct RenderResourcesLoader

} // namespace raptor
