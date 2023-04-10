#pragma once

#include "graphics/gpu_device.hpp"

namespace raptor {

static const u32 k_secondary_command_buffers_count = 2;

//
//
struct CommandBuffer {

    void                            init( GpuDevice* gpu );
    void                            shutdown();

    //
    // Commands interface
    //

    DescriptorSetHandle             create_descriptor_set( const DescriptorSetCreation& creation );

    void                            begin();
    void                            begin_secondary( RenderPass* current_render_pass, Framebuffer* current_framebuffer );
    void                            end();
    void                            end_current_render_pass();

    void                            bind_pass( RenderPassHandle handle, FramebufferHandle framebuffer, bool use_secondary );
    void                            bind_pipeline( PipelineHandle handle );
    void                            bind_vertex_buffer( BufferHandle handle, u32 binding, u32 offset );
    void                            bind_vertex_buffers( BufferHandle* handles, u32 first_binding, u32 binding_count, u32* offsets );
    void                            bind_index_buffer( BufferHandle handle, u32 offset, VkIndexType index_type );
    void                            bind_descriptor_set( DescriptorSetHandle* handles, u32 num_lists, u32* offsets, u32 num_offsets );
    void                            bind_local_descriptor_set( DescriptorSetHandle* handles, u32 num_lists, u32* offsets, u32 num_offsets );

    void                            set_viewport( const Viewport* viewport );
    void                            set_scissor( const Rect2DInt* rect );

    void                            clear( f32 red, f32 green, f32 blue, f32 alpha, u32 attachment_index );
    void                            clear_depth_stencil( f32 depth, u8 stencil );

    void                            draw( TopologyType::Enum topology, u32 first_vertex, u32 vertex_count, u32 first_instance, u32 instance_count );
    void                            draw_indexed( TopologyType::Enum topology, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance );
    void                            draw_indirect( BufferHandle handle, u32 draw_count, u32 offset, u32 stride );
    void                            draw_indexed_indirect( BufferHandle handle, u32 offset, u32 stride );

    void                            dispatch( u32 group_x, u32 group_y, u32 group_z );
    void                            dispatch_indirect( BufferHandle handle, u32 offset );

    void                            barrier( const ExecutionBarrier& barrier );

    void                            fill_buffer( BufferHandle buffer, u32 offset, u32 size, u32 data );

    void                            push_marker( const char* name );
    void                            pop_marker();

    // Non-drawing methods
    void                            upload_texture_data( TextureHandle texture, void* texture_data, BufferHandle staging_buffer, sizet staging_buffer_offset );
    void                            copy_texture( TextureHandle src_, TextureHandle dst_, ResourceState dst_state );

    void                            upload_buffer_data( BufferHandle buffer, void* buffer_data, BufferHandle staging_buffer, sizet staging_buffer_offset );
    void                            upload_buffer_data( BufferHandle src, BufferHandle dst );

    void                            reset();

    static const u32                k_depth_stencil_clear_index = k_max_image_outputs;

    VkCommandBuffer                 vk_command_buffer;

    VkDescriptorPool                vk_descriptor_pool;
    ResourcePool                    descriptor_sets;

    GpuThreadFramePools*            thread_frame_pool;
    GpuDevice*                      device;

    VkDescriptorSet                 vk_descriptor_sets[16];

    RenderPass*                     current_render_pass;
    Framebuffer*                    current_framebuffer;
    Pipeline*                       current_pipeline;
    VkClearValue                    clear_values[ k_max_image_outputs + 1 ];    // Clear value for each attachment with depth/stencil at the end.
    bool                            is_recording;

    u32                             handle;

    u32                             current_command;
    ResourceHandle                  resource_handle;

}; // struct CommandBuffer


struct CommandBufferManager {

    void                    init( GpuDevice* gpu, u32 num_threads );
    void                    shutdown();

    void                    reset_pools( u32 frame_index );

    CommandBuffer*          get_command_buffer( u32 frame, u32 thread_index, bool begin, bool compute );
    CommandBuffer*          get_secondary_command_buffer( u32 frame, u32 thread_index );

    u16                     pool_from_index( u32 index ) { return (u16)index / num_pools_per_frame; }
    u32                     pool_from_indices( u32 frame_index, u32 thread_index );

    Array<CommandBuffer>    command_buffers;
    Array<CommandBuffer>    secondary_command_buffers;
    Array<CommandBuffer>    compute_command_buffers;
    Array<u8>               used_buffers;       // Track how many buffers were used per thread per frame.
    Array<u8>               used_secondary_command_buffers;

    GpuDevice*              gpu                     = nullptr;
    u32                     num_pools_per_frame     = 0;
    u32                     num_command_buffers_per_thread = 3;

}; // struct CommandBufferManager


} // namespace raptor
