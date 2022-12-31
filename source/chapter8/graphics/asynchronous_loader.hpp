#pragma once

#include "foundation/array.hpp"
#include "foundation/platform.hpp"

#include "graphics/command_buffer.hpp"
#include "graphics/gpu_device.hpp"
#include "graphics/gpu_resources.hpp"

#include "external/cglm/types-struct.h"

#include <atomic>

namespace enki { class TaskScheduler; }

namespace raptor
{
    struct Allocator;
    struct FrameGraph;
    struct GpuVisualProfiler;
    struct ImGuiService;
    struct Renderer;
    struct StackAllocator;

    //
    //
    struct FileLoadRequest {

        char                                    path[ 512 ];
        TextureHandle                           texture     = k_invalid_texture;
        BufferHandle                            buffer      = k_invalid_buffer;
    }; // struct FileLoadRequest

    //
    //
    struct UploadRequest {

        void*                                   data        = nullptr;
        TextureHandle                           texture     = k_invalid_texture;
        BufferHandle                            cpu_buffer  = k_invalid_buffer;
        BufferHandle                            gpu_buffer  = k_invalid_buffer;
    }; // struct UploadRequest

    //
    //
    struct AsynchronousLoader {

        void                                    init( Renderer* renderer, enki::TaskScheduler* task_scheduler, Allocator* resident_allocator );
        void                                    update( Allocator* scratch_allocator );
        void                                    shutdown();

        void                                    request_texture_data( cstring filename, TextureHandle texture );
        void                                    request_buffer_upload( void* data, BufferHandle buffer );
        void                                    request_buffer_copy( BufferHandle src, BufferHandle dst );

        Allocator*                              allocator       = nullptr;
        Renderer*                               renderer        = nullptr;
        enki::TaskScheduler*                    task_scheduler  = nullptr;

        Array<FileLoadRequest>                  file_load_requests;
        Array<UploadRequest>                    upload_requests;

        Buffer*                                 staging_buffer  = nullptr;

        std::atomic_size_t                      staging_buffer_offset;
        TextureHandle                           texture_ready;
        BufferHandle                            cpu_buffer_ready;
        BufferHandle                            gpu_buffer_ready;

        VkCommandPool                           command_pools[ k_max_frames ];
        CommandBuffer                           command_buffers[ k_max_frames ];
        VkSemaphore                             transfer_complete_semaphore;
        VkFence                                 transfer_fence;

    }; // struct AsynchonousLoader

} // namespace raptor
