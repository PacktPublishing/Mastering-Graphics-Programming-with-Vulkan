#pragma once

#include "foundation/memory.hpp"
#include "graphics/gpu_device.hpp"

namespace raptor {


//
// A single timestamp query, containing indices for the pool, resolved time, name and color.
struct GPUTimeQuery {

    f64                             elapsed_ms;

    u16                             start_query_index;  // Used to write timestamp in the query pool
    u16                             end_query_index;    // Used to write timestamp in the query pool

    u16                             parent_index;
    u16                             depth;

    u32                             color;
    u32                             frame_index;

    cstring                         name;
}; // struct GPUTimeQuery

//
// Query tree used mainly per thread-frame to retrieve time data.
struct GpuTimeQueryTree {

    void                            reset();
    void                            set_queries( GPUTimeQuery* time_queries, u32 count );

    GPUTimeQuery*                   push( cstring name );
    GPUTimeQuery*                   pop();

    ArrayView<GPUTimeQuery>         time_queries; // Allocated externally

    u16                             current_time_query   = 0;
    u16                             allocated_time_query = 0;
    u16                             depth                = 0;

}; // struct GpuTimeQueryTree

//
//
struct GpuPipelineStatistics {
    enum Statistics : u8 {
        VerticesCount,
        PrimitiveCount,
        VertexShaderInvocations,
        ClippingInvocations,
        ClippingPrimitives,
        FragmentShaderInvocations,
        ComputeShaderInvocations,
        Count
    };

    void                            reset();

    u64                             statistics[ Count ];
};

//
//
struct GPUTimeQueriesManager {

    void                            init( GpuThreadFramePools* thread_frame_pools, GpuThreadFramePools* compute_frame_pools, Allocator* allocator, u16 queries_per_thread, u16 num_threads, u16 max_frames );
    void                            shutdown();

    void                            reset();
    u32                             resolve( u32 current_frame, GPUTimeQuery* timestamps_to_fill );    // Returns the total queries for this frame.

    Array<GpuTimeQueryTree>         query_trees;

    Allocator*                      allocator                   = nullptr;
    GpuThreadFramePools*            thread_frame_pools          = nullptr;
    GpuThreadFramePools*            compute_frame_pools         = nullptr;
    GPUTimeQuery*                   timestamps                  = nullptr;

    GpuPipelineStatistics           frame_pipeline_statistics;  // Per frame statistics as sum of per-frame ones.

    u32                             queries_per_thread          = 0;
    u32                             queries_per_frame           = 0;
    u32                             num_threads                 = 0;

    bool                            current_frame_resolved      = false;    // Used to query the GPU only once per frame if get_gpu_timestamps is called more than once per frame.

}; // struct GPUTimeQueriesManager

// GpuVisualProfiler //////////////////////////////////////////////////////

//
// Collect per frame queries from GpuProfiler and create a visual representation.
struct GpuVisualProfiler {

    void                        init( Allocator* allocator, u32 max_frames, u32 max_queries_per_frame );
    void                        shutdown();

    void                        update( GpuDevice& gpu );

    void                        imgui_draw();

    Allocator*                  allocator;
    GPUTimeQuery*               timestamps;     // Per frame timestamps collected from the profiler.
    u16*                        per_frame_active;
    GpuPipelineStatistics*      pipeline_statistics;    // Per frame collected pipeline statistics.

    u32                         max_frames;
    u32                         max_queries_per_frame;
    u32                         current_frame;

    f32                         max_time;
    f32                         min_time;
    f32                         average_time;

    f32                         max_duration;
    bool                        paused;

}; // struct GPUProfiler

} // namespace raptor
