#pragma once

#include "graphics/gpu_device.hpp"

namespace raptor {

//
//
struct CommandBuffer {

    void                            init( QueueType::Enum type, u32 buffer_size, u32 submit_size, bool baked );
    void                            terminate();

    //
    // Commands interface
    //

    void                            bind_pass( RenderPassHandle handle );
    void                            bind_pipeline( PipelineHandle handle );
    void                            bind_vertex_buffer( BufferHandle handle, u32 binding, u32 offset );
    void                            bind_index_buffer( BufferHandle handle, u32 offset, VkIndexType index_type );
    void                            bind_descriptor_set( DescriptorSetHandle* handles, u32 num_lists, u32* offsets, u32 num_offsets );

    void                            set_viewport( const Viewport* viewport );
    void                            set_scissor( const Rect2DInt* rect );

    void                            clear( f32 red, f32 green, f32 blue, f32 alpha );
    void                            clear_depth_stencil( f32 depth, u8 stencil );

    void                            draw( TopologyType::Enum topology, u32 first_vertex, u32 vertex_count, u32 first_instance, u32 instance_count );
    void                            draw_indexed( TopologyType::Enum topology, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance );
    void                            draw_indirect( BufferHandle handle, u32 offset, u32 stride );
    void                            draw_indexed_indirect( BufferHandle handle, u32 offset, u32 stride );

    void                            dispatch( u32 group_x, u32 group_y, u32 group_z );
    void                            dispatch_indirect( BufferHandle handle, u32 offset );

    void                            barrier( const ExecutionBarrier& barrier );

    void                            fill_buffer( BufferHandle buffer, u32 offset, u32 size, u32 data );

    void                            push_marker( const char* name );
    void                            pop_marker();

    void                            reset();

    VkCommandBuffer                 vk_command_buffer;

    GpuDevice*                      device;

    VkDescriptorSet                 vk_descriptor_sets[16];

    RenderPass*                     current_render_pass;
    Pipeline*                       current_pipeline;
    VkClearValue                    clears[2];          // 0 = color, 1 = depth stencil
    bool                            is_recording;

    u32                             handle;

    u32                             current_command;
    ResourceHandle                  resource_handle;
    QueueType::Enum                 type                = QueueType::Graphics;
    u32                             buffer_size         = 0;

    bool                            baked               = false;        // If baked reset will affect only the read of the commands.

}; // struct CommandBuffer


} // namespace raptor
