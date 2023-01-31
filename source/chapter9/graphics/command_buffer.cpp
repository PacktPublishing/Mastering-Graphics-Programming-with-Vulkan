#include "graphics/command_buffer.hpp"
#include "graphics/gpu_device.hpp"
#include "graphics/gpu_profiler.hpp"

#include "foundation/memory.hpp"
#include "foundation/numerics.hpp"

#include "external/tracy/tracy/Tracy.hpp"

#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace raptor {

void CommandBuffer::reset() {

    is_recording = false;
    current_render_pass = nullptr;
    current_framebuffer = nullptr;
    current_pipeline = nullptr;
    current_command = 0;

    vkResetDescriptorPool( gpu_device->vulkan_device, vk_descriptor_pool, 0 );

    u32 resource_count = descriptor_sets.free_indices_head;
    for ( u32 i = 0; i < resource_count; ++i) {
        DescriptorSet* v_descriptor_set = ( DescriptorSet* )descriptor_sets.access_resource( i );

        if ( v_descriptor_set ) {
            // Contains the allocation for all the resources, binding and samplers arrays.
            rfree( v_descriptor_set->resources, gpu_device->allocator );
        }
        descriptor_sets.release_resource( i );
    }
}

static const u32 k_descriptor_sets_pool_size = 4096;

void CommandBuffer::init( GpuDevice* gpu ) {

    gpu_device = gpu;

    // Create Descriptor Pools
    static const u32 k_global_pool_elements = 128;
    VkDescriptorPoolSize pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, k_global_pool_elements },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, k_global_pool_elements },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, k_global_pool_elements },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, k_global_pool_elements },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, k_global_pool_elements },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, k_global_pool_elements },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, k_global_pool_elements },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, k_global_pool_elements },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, k_global_pool_elements },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, k_global_pool_elements },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, k_global_pool_elements}
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = k_descriptor_sets_pool_size;
    pool_info.poolSizeCount = ( u32 )ArraySize( pool_sizes );
    pool_info.pPoolSizes = pool_sizes;
    VkResult result = vkCreateDescriptorPool( gpu_device->vulkan_device, &pool_info, gpu_device->vulkan_allocation_callbacks, &vk_descriptor_pool );
    RASSERT( result == VK_SUCCESS );

    descriptor_sets.init( gpu_device->allocator, k_descriptor_sets_pool_size, sizeof( DescriptorSet ) );

    reset();
}

void CommandBuffer::shutdown() {

    is_recording = false;

    reset();

    descriptor_sets.shutdown();

    vkDestroyDescriptorPool( gpu_device->vulkan_device, vk_descriptor_pool, gpu_device->vulkan_allocation_callbacks );
}

DescriptorSetHandle CommandBuffer::create_descriptor_set( const DescriptorSetCreation& creation ) {
    ZoneScoped;

    DescriptorSetHandle handle = gpu_device->create_descriptor_set( creation );

    return handle;
}

void CommandBuffer::begin() {

    if ( !is_recording ) {
        VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer( vk_command_buffer, &beginInfo );

        is_recording = true;
    }
}

void CommandBuffer::begin_secondary( RenderPass* current_render_pass_, Framebuffer* current_framebuffer_ ) {
    if ( !is_recording ) {
        VkCommandBufferInheritanceInfo inheritance{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
        inheritance.renderPass = current_render_pass_->vk_render_pass;
        inheritance.subpass = 0;
        inheritance.framebuffer = current_framebuffer_->vk_framebuffer;

        VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        beginInfo.pInheritanceInfo = &inheritance;

        vkBeginCommandBuffer( vk_command_buffer, &beginInfo );

        is_recording = true;

        current_render_pass = current_render_pass_;
    }
}

void CommandBuffer::end() {

    if ( is_recording ) {
        vkEndCommandBuffer( vk_command_buffer );

        is_recording = false;
    }
}

void CommandBuffer::end_current_render_pass() {
    if ( is_recording && current_render_pass != nullptr ) {
        if ( gpu_device->dynamic_rendering_extension_present ) {
            gpu_device->vkCmdEndRenderingKHR( vk_command_buffer );
        } else {
            vkCmdEndRenderPass( vk_command_buffer );
        }

        current_render_pass = nullptr;
    }
}

void CommandBuffer::bind_pass( RenderPassHandle handle_, FramebufferHandle framebuffer_, bool use_secondary ) {

    //if ( !is_recording )
    {
        is_recording = true;

        RenderPass* render_pass = gpu_device->access_render_pass( handle_ );

        // Begin/End render pass are valid only for graphics render passes.
        if ( current_render_pass && ( render_pass != current_render_pass ) ) {
            end_current_render_pass();
        }

        Framebuffer* framebuffer = gpu_device->access_framebuffer( framebuffer_ );

        if ( render_pass != current_render_pass ) {
            if ( gpu_device->dynamic_rendering_extension_present ) {
                Array<VkRenderingAttachmentInfoKHR> color_attachments_info;

                u64 marker = gpu_device->temporary_allocator->get_marker();
                color_attachments_info.init( gpu_device->temporary_allocator, framebuffer->num_color_attachments, framebuffer->num_color_attachments );
                memset( color_attachments_info.data, 0, sizeof( VkRenderingAttachmentInfoKHR ) * framebuffer->num_color_attachments );

                for ( u32 a = 0; a < framebuffer->num_color_attachments; ++a ) {
                    Texture* texture = gpu_device->access_texture( framebuffer->color_attachments[a] );

                    texture->state = RESOURCE_STATE_RENDER_TARGET;

                    VkAttachmentLoadOp color_op;
                    switch ( render_pass->output.color_operations[ a ] ) {
                        case RenderPassOperation::Load:
                            color_op = VK_ATTACHMENT_LOAD_OP_LOAD;
                            break;
                        case RenderPassOperation::Clear:
                            color_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
                            break;
                        default:
                            color_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                            break;
                    }

                    VkRenderingAttachmentInfoKHR& color_attachment_info = color_attachments_info[ a ];
                    color_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                    color_attachment_info.imageView = texture->vk_image_view;
                    color_attachment_info.imageLayout = gpu_device->synchronization2_extension_present ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    color_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;
                    color_attachment_info.loadOp = color_op;
                    color_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    color_attachment_info.clearValue = render_pass->output.color_operations[ a ] == RenderPassOperation::Enum::Clear ? clear_values[ a ] : VkClearValue{ };
                }

                VkRenderingAttachmentInfoKHR depth_attachment_info{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };

                bool has_depth_attachment = framebuffer->depth_stencil_attachment.index != k_invalid_index;

                if ( has_depth_attachment ) {
                    Texture* texture = gpu_device->access_texture( framebuffer->depth_stencil_attachment );

                    VkAttachmentLoadOp depth_op;
                    switch ( render_pass->output.depth_operation ) {
                        case RenderPassOperation::Load:
                            depth_op = VK_ATTACHMENT_LOAD_OP_LOAD;
                            break;
                        case RenderPassOperation::Clear:
                            depth_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
                            break;
                        default:
                            depth_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                            break;
                    }

                    texture->state = RESOURCE_STATE_DEPTH_WRITE;

                    depth_attachment_info.imageView = texture->vk_image_view;
                    depth_attachment_info.imageLayout = gpu_device->synchronization2_extension_present ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    depth_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;
                    depth_attachment_info.loadOp = depth_op;
                    depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    depth_attachment_info.clearValue = render_pass->output.depth_operation == RenderPassOperation::Enum::Clear ? clear_values[ k_depth_stencil_clear_index ] : VkClearValue{ };
                }

                VkRenderingInfoKHR rendering_info{ VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
                rendering_info.flags = use_secondary ? VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT_KHR : 0;
                rendering_info.renderArea = { 0, 0, framebuffer->width, framebuffer->height };
                rendering_info.layerCount = framebuffer->layers;
                rendering_info.viewMask = render_pass->multiview_mask;
                rendering_info.colorAttachmentCount = framebuffer->num_color_attachments;
                rendering_info.pColorAttachments = framebuffer->num_color_attachments > 0 ? color_attachments_info.data : nullptr;
                rendering_info.pDepthAttachment =  has_depth_attachment ? &depth_attachment_info : nullptr;
                rendering_info.pStencilAttachment = nullptr;

                VkRenderingFragmentShadingRateAttachmentInfoKHR shading_rate_info { VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR };
                if ( framebuffer->shader_rate_attachment.index != k_invalid_index ) {
                    Texture* texture = gpu_device->access_texture( framebuffer->shader_rate_attachment );

                    shading_rate_info.imageView = texture->vk_image_view;
                    shading_rate_info.imageLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
                    shading_rate_info.shadingRateAttachmentTexelSize = gpu_device->min_fragment_shading_rate_texel_size;

                    rendering_info.pNext = ( void* )&shading_rate_info;
                }

                gpu_device->vkCmdBeginRenderingKHR( vk_command_buffer, &rendering_info );

                gpu_device->temporary_allocator->free_marker( marker );
            } else {
                VkRenderPassBeginInfo render_pass_begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
                render_pass_begin.framebuffer = framebuffer->vk_framebuffer;
                render_pass_begin.renderPass = render_pass->vk_render_pass;

                render_pass_begin.renderArea.offset = { 0, 0 };
                render_pass_begin.renderArea.extent = { framebuffer->width, framebuffer->height };

                u32 clear_values_count = render_pass->output.num_color_formats;
                // Copy final depth/stencil clear
                if ( (render_pass->output.depth_stencil_format != VK_FORMAT_UNDEFINED) && ( render_pass->output.depth_operation == RenderPassOperation::Enum::Clear ) ) {
                    clear_values[ clear_values_count++ ] = clear_values[ k_depth_stencil_clear_index ];
                }

                render_pass_begin.clearValueCount =  clear_values_count;
                render_pass_begin.pClearValues = clear_values;

                vkCmdBeginRenderPass( vk_command_buffer, &render_pass_begin, use_secondary ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE );
            }
        }

        // Cache render pass
        current_render_pass = render_pass;
        current_framebuffer = framebuffer;
    }
}

void CommandBuffer::bind_pipeline( PipelineHandle handle_ ) {

    Pipeline* pipeline = gpu_device->access_pipeline( handle_ );
    vkCmdBindPipeline( vk_command_buffer, pipeline->vk_bind_point, pipeline->vk_pipeline );

    // Cache pipeline
    current_pipeline = pipeline;
}

void CommandBuffer::bind_vertex_buffer( BufferHandle handle_, u32 binding, u32 offset ) {

    Buffer* buffer = gpu_device->access_buffer( handle_ );
    VkDeviceSize offsets[] = { offset };

    VkBuffer vk_buffer = buffer->vk_buffer;
    // TODO: add global vertex buffer ?
    if ( buffer->parent_buffer.index != k_invalid_index ) {
        Buffer* parent_buffer = gpu_device->access_buffer( buffer->parent_buffer );
        vk_buffer = parent_buffer->vk_buffer;
        offsets[ 0 ] = buffer->global_offset;
    }

    vkCmdBindVertexBuffers( vk_command_buffer, binding, 1, &vk_buffer, offsets );
}

void CommandBuffer::bind_vertex_buffers( BufferHandle* handles, u32 first_binding, u32 binding_count, u32* offsets_ ) {
    VkBuffer vk_buffers[ 8 ];
    VkDeviceSize offsets[ 8 ];

    for ( u32 i = 0; i < binding_count; ++i ) {
        Buffer* buffer = gpu_device->access_buffer( handles[i] );

        VkBuffer vk_buffer = buffer->vk_buffer;
        // TODO: add global vertex buffer ?
        if ( buffer->parent_buffer.index != k_invalid_index ) {
            Buffer* parent_buffer = gpu_device->access_buffer( buffer->parent_buffer );
            vk_buffer = parent_buffer->vk_buffer;
            offsets[ i ] = buffer->global_offset;
        }
        else {
            offsets[ i ] = offsets_[ i ];
        }

        vk_buffers[ i ] = vk_buffer;
    }

    vkCmdBindVertexBuffers( vk_command_buffer, first_binding, binding_count, vk_buffers, offsets );
}

void CommandBuffer::bind_index_buffer( BufferHandle handle_, u32 offset_, VkIndexType index_type ) {

    Buffer* buffer = gpu_device->access_buffer( handle_ );

    VkBuffer vk_buffer = buffer->vk_buffer;
    VkDeviceSize offset = offset_;
    if ( buffer->parent_buffer.index != k_invalid_index ) {
        Buffer* parent_buffer = gpu_device->access_buffer( buffer->parent_buffer );
        vk_buffer = parent_buffer->vk_buffer;
        offset = buffer->global_offset;
    }
    vkCmdBindIndexBuffer( vk_command_buffer, vk_buffer, offset, index_type );
}

void CommandBuffer::bind_descriptor_set( DescriptorSetHandle* handles, u32 num_lists, u32* offsets, u32 num_offsets ) {

    // TODO:
    u32 offsets_cache[ 8 ];
    num_offsets = 0;

    for ( u32 l = 0; l < num_lists; ++l ) {
        DescriptorSet* descriptor_set = gpu_device->access_descriptor_set( handles[l] );
        vk_descriptor_sets[l] = descriptor_set->vk_descriptor_set;

        // Search for dynamic buffers
        const DescriptorSetLayout* descriptor_set_layout = descriptor_set->layout;
        for ( u32 i = 0; i < descriptor_set_layout->num_bindings; ++i ) {

            //const u32 binding_point = descriptor_set->bindings[ i ];
            const DescriptorBinding& rb = descriptor_set_layout->bindings[ i ];

            if ( rb.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ) {
                // Search for the actual buffer offset

                ResourceHandle buffer_handle = descriptor_set->resources[ i ];
                Buffer* buffer = gpu_device->access_buffer( { buffer_handle } );

                offsets_cache[ num_offsets++ ] = buffer->global_offset;
            }
        }
    }

    const u32 k_first_set = 1;
    vkCmdBindDescriptorSets( vk_command_buffer, current_pipeline->vk_bind_point, current_pipeline->vk_pipeline_layout, k_first_set,
                             num_lists, vk_descriptor_sets, num_offsets, offsets_cache );

    if ( gpu_device->bindless_supported ) {
        vkCmdBindDescriptorSets( vk_command_buffer, current_pipeline->vk_bind_point, current_pipeline->vk_pipeline_layout, 0,
                                 1, &gpu_device->vulkan_bindless_descriptor_set_cached, 0, nullptr );
    }
}

void CommandBuffer::bind_local_descriptor_set( DescriptorSetHandle* handles, u32 num_lists, u32* offsets, u32 num_offsets ) {

    // TODO:
    u32 offsets_cache[ 8 ];
    num_offsets = 0;

    for ( u32 l = 0; l < num_lists; ++l ) {
        DescriptorSet* descriptor_set = ( DescriptorSet* )descriptor_sets.access_resource( handles[ l ].index );
        vk_descriptor_sets[l] = descriptor_set->vk_descriptor_set;

        // Search for dynamic buffers
        const DescriptorSetLayout* descriptor_set_layout = descriptor_set->layout;
        for ( u32 i = 0; i < descriptor_set_layout->num_bindings; ++i ) {
            const DescriptorBinding& rb = descriptor_set_layout->bindings[ i ];

            if ( rb.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ) {
                // Search for the actual buffer offset
                const u32 resource_index = descriptor_set->bindings[ i ];
                ResourceHandle buffer_handle = descriptor_set->resources[ resource_index ];
                Buffer* buffer = gpu_device->access_buffer( { buffer_handle } );

                offsets_cache[ num_offsets++ ] = buffer->global_offset;
            }
        }
    }

    const u32 k_first_set = 1;
    vkCmdBindDescriptorSets( vk_command_buffer, current_pipeline->vk_bind_point, current_pipeline->vk_pipeline_layout, k_first_set,
                             num_lists, vk_descriptor_sets, num_offsets, offsets_cache );

    if ( gpu_device->bindless_supported ) {
        vkCmdBindDescriptorSets( vk_command_buffer, current_pipeline->vk_bind_point, current_pipeline->vk_pipeline_layout, 0,
                                 1, &gpu_device->vulkan_bindless_descriptor_set_cached, 0, nullptr );
    }
}

void CommandBuffer::set_viewport( const Viewport* viewport ) {

    VkViewport vk_viewport;

    if ( viewport ) {
        vk_viewport.x = viewport->rect.x * 1.f;
        vk_viewport.width = viewport->rect.width * 1.f;
        // Invert Y with negative height and proper offset - Vulkan has unique Clipping Y.
        vk_viewport.y = viewport->rect.height * 1.f - viewport->rect.y;
        vk_viewport.height = -viewport->rect.height * 1.f;
        vk_viewport.minDepth = viewport->min_depth;
        vk_viewport.maxDepth = viewport->max_depth;
    }
    else {
        vk_viewport.x = 0.f;

        if ( current_render_pass ) {
            vk_viewport.width = current_framebuffer->width * 1.f;
            // Invert Y with negative height and proper offset - Vulkan has unique Clipping Y.
            vk_viewport.y = current_framebuffer->height * 1.f;
            vk_viewport.height = -current_framebuffer->height * 1.f;
        }
        else {
            vk_viewport.width = gpu_device->swapchain_width * 1.f;
            // Invert Y with negative height and proper offset - Vulkan has unique Clipping Y.
            vk_viewport.y = gpu_device->swapchain_height * 1.f;
            vk_viewport.height = -gpu_device->swapchain_height * 1.f;
        }
        vk_viewport.minDepth = 0.0f;
        vk_viewport.maxDepth = 1.0f;
    }

    vkCmdSetViewport( vk_command_buffer, 0, 1, &vk_viewport);
}

void CommandBuffer::set_scissor( const Rect2DInt* rect ) {

    VkRect2D vk_scissor;

    if ( rect ) {
        vk_scissor.offset.x = rect->x;
        vk_scissor.offset.y = rect->y;
        vk_scissor.extent.width = rect->width;
        vk_scissor.extent.height = rect->height;
    }
    else {
        vk_scissor.offset.x = 0;
        vk_scissor.offset.y = 0;
        vk_scissor.extent.width = gpu_device->swapchain_width;
        vk_scissor.extent.height = gpu_device->swapchain_height;
    }

    vkCmdSetScissor( vk_command_buffer, 0, 1, &vk_scissor );
}

void CommandBuffer::clear( f32 red, f32 green, f32 blue, f32 alpha, u32 attachment_index ) {
    clear_values[ attachment_index ].color = { red, green, blue, alpha };
}

void CommandBuffer::clear_depth_stencil( f32 depth, u8 value ) {
    clear_values[ k_depth_stencil_clear_index ].depthStencil.depth = depth;
    clear_values[ k_depth_stencil_clear_index ].depthStencil.stencil = value;
}

void CommandBuffer::push_constants( PipelineHandle pipeline, u32 offset, u32 size, void* data ) {
    Pipeline* pipeline_ = gpu_device->access_pipeline( pipeline );
    vkCmdPushConstants( vk_command_buffer, pipeline_->vk_pipeline_layout, VK_SHADER_STAGE_ALL, offset, size, data );
}

void CommandBuffer::draw( TopologyType::Enum topology, u32 first_vertex, u32 vertex_count, u32 first_instance, u32 instance_count ) {
    vkCmdDraw( vk_command_buffer, vertex_count, instance_count, first_vertex, first_instance );
}

void CommandBuffer::draw_indexed( TopologyType::Enum topology, u32 index_count, u32 instance_count, u32 first_index, i32 vertex_offset, u32 first_instance ) {
    vkCmdDrawIndexed( vk_command_buffer, index_count, instance_count, first_index, vertex_offset, first_instance );
}

void CommandBuffer::dispatch( u32 group_x, u32 group_y, u32 group_z ) {
    vkCmdDispatch( vk_command_buffer, group_x, group_y, group_z );
}

void CommandBuffer::draw_indirect( BufferHandle buffer_handle, u32 draw_count, u32 offset, u32 stride ) {

    Buffer* buffer = gpu_device->access_buffer( buffer_handle );

    VkBuffer vk_buffer = buffer->vk_buffer;
    VkDeviceSize vk_offset = offset;

    vkCmdDrawIndirect( vk_command_buffer, vk_buffer, vk_offset, draw_count, stride );
}

void CommandBuffer::draw_indirect_count( BufferHandle argument_buffer, u32 argument_offset, BufferHandle count_buffer, u32 count_offset, u32 max_draws, u32 stride ) {
    Buffer* argument_buffer_ = gpu_device->access_buffer( argument_buffer );
    Buffer* count_buffer_ = gpu_device->access_buffer( count_buffer );

    vkCmdDrawIndirectCount( vk_command_buffer, argument_buffer_->vk_buffer, argument_offset, count_buffer_->vk_buffer, count_offset, max_draws, stride );
}

void CommandBuffer::draw_indexed_indirect( BufferHandle buffer_handle, u32 draw_count, u32 offset, u32 stride ) {
    Buffer* buffer = gpu_device->access_buffer( buffer_handle );

    VkBuffer vk_buffer = buffer->vk_buffer;
    VkDeviceSize vk_offset = offset;

    vkCmdDrawIndexedIndirect( vk_command_buffer, vk_buffer, vk_offset, draw_count, stride );
}

void CommandBuffer::draw_mesh_task( u32 task_count, u32 first_task ) {

    gpu_device->vkCmdDrawMeshTasksNV( vk_command_buffer, task_count, first_task );
}

void CommandBuffer::draw_mesh_task_indirect( BufferHandle argument_buffer, u32 argument_offset, u32 command_count, u32 stride ) {
    Buffer* argument_buffer_ = gpu_device->access_buffer( argument_buffer );

    gpu_device->vkCmdDrawMeshTasksIndirectNV( vk_command_buffer, argument_buffer_->vk_buffer, argument_offset, command_count, stride );
}

void CommandBuffer::draw_mesh_task_indirect_count( BufferHandle argument_buffer, u32 argument_offset, BufferHandle count_buffer, u32 count_offset, u32 max_draws, u32 stride ) {
    Buffer* argument_buffer_ = gpu_device->access_buffer( argument_buffer );
    Buffer* count_buffer_ = gpu_device->access_buffer( count_buffer );

    gpu_device->vkCmdDrawMeshTasksIndirectCountNV( vk_command_buffer, argument_buffer_->vk_buffer, argument_offset, count_buffer_->vk_buffer, count_offset, max_draws, stride );
}

void CommandBuffer::dispatch_indirect( BufferHandle buffer_handle, u32 offset ) {
    Buffer* buffer = gpu_device->access_buffer( buffer_handle );

    VkBuffer vk_buffer = buffer->vk_buffer;
    VkDeviceSize vk_offset = offset;

    vkCmdDispatchIndirect( vk_command_buffer, vk_buffer, vk_offset );
}

// DrawIndirect = 0, VertexInput = 1, VertexShader = 2, FragmentShader = 3, RenderTarget = 4, ComputeShader = 5, Transfer = 6
static ResourceState to_resource_state( PipelineStage::Enum stage ) {
    static ResourceState s_states[] = { RESOURCE_STATE_INDIRECT_ARGUMENT, RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_COPY_DEST };
    return s_states[ stage ];
}

void CommandBuffer::global_debug_barrier() {

    VkMemoryBarrier2KHR barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR | VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR | VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;

    VkDependencyInfoKHR dependency_info{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
    dependency_info.memoryBarrierCount = 1;
    dependency_info.pMemoryBarriers = &barrier;

    gpu_device->vkCmdPipelineBarrier2KHR( vk_command_buffer, &dependency_info );
}

void CommandBuffer::buffer_barrier( BufferHandle buffer_handle, ResourceState old_state, ResourceState new_state, QueueType::Enum source_queue_type, QueueType::Enum destination_queue_type ) {

    Buffer* buffer = gpu_device->access_buffer( buffer_handle );
    util_add_buffer_barrier_ext( gpu_device, vk_command_buffer, buffer->vk_buffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_UNORDERED_ACCESS, buffer->size,
                                 VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, source_queue_type, destination_queue_type );
}

void CommandBuffer::barrier( const ExecutionBarrier& barrier ) {

    if ( current_render_pass ) {
        vkCmdEndRenderPass( vk_command_buffer );

        current_render_pass = nullptr;
        current_framebuffer = nullptr;
    }

    if ( gpu_device->synchronization2_extension_present ) {

        VkImageMemoryBarrier2KHR image_barriers[ 8 ];

        for ( u32 i = 0; i < barrier.num_image_barriers; ++i ) {
            const ImageBarrier& source_barrier = barrier.image_barriers[ i ];
            Texture* texture = gpu_device->access_texture( source_barrier.texture );

            VkImageMemoryBarrier2KHR& barrier = image_barriers[ i ];
            barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR };
            barrier.srcAccessMask = util_to_vk_access_flags2( texture->state );
            barrier.srcStageMask = util_determine_pipeline_stage_flags2( barrier.srcAccessMask, QueueType::Graphics );
            barrier.dstAccessMask = util_to_vk_access_flags2( source_barrier.destination_state );
            barrier.dstStageMask = util_determine_pipeline_stage_flags2( barrier.dstAccessMask, QueueType::Graphics );
            barrier.oldLayout = util_to_vk_image_layout2( texture->state );
            barrier.newLayout = util_to_vk_image_layout2( source_barrier.destination_state );
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture->vk_image;
            barrier.subresourceRange.aspectMask = TextureFormat::has_depth_or_stencil( texture->vk_format ) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseArrayLayer = source_barrier.array_base_layer;
            barrier.subresourceRange.layerCount = source_barrier.array_layer_count;
            barrier.subresourceRange.baseMipLevel = source_barrier.mip_base_level;
            barrier.subresourceRange.levelCount = source_barrier.mip_level_count;
        }

        VkBufferMemoryBarrier2KHR buffer_barriers[ 8 ];

        for ( u32 i = 0; i < barrier.num_buffer_barriers; ++i ) {
            const BufferBarrier& source_barrier = barrier.buffer_barriers[ i ];
            Buffer* buffer = gpu_device->access_buffer( source_barrier.buffer );

            VkBufferMemoryBarrier2KHR& barrier = buffer_barriers[ i ];
            barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR };
            barrier.srcAccessMask = util_to_vk_access_flags2( source_barrier.source_state );
            barrier.srcStageMask = util_determine_pipeline_stage_flags2( barrier.srcAccessMask, QueueType::Graphics );
            barrier.dstAccessMask = util_to_vk_access_flags2( source_barrier.destination_state );
            barrier.dstStageMask = util_determine_pipeline_stage_flags2( barrier.dstAccessMask, QueueType::Graphics );
            barrier.buffer = buffer->vk_buffer;
            barrier.offset = source_barrier.offset;
            barrier.size = source_barrier.offset > 0 ? source_barrier.offset : buffer->size;
        }

        VkDependencyInfoKHR dependency_info{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
        dependency_info.imageMemoryBarrierCount = barrier.num_image_barriers;
        dependency_info.pImageMemoryBarriers = image_barriers;
        dependency_info.pBufferMemoryBarriers = buffer_barriers;
        dependency_info.bufferMemoryBarrierCount = barrier.num_buffer_barriers;

        gpu_device->vkCmdPipelineBarrier2KHR( vk_command_buffer, &dependency_info );
    }
    else {
        // TODO: implement
        RASSERT( false );
    }
}

void CommandBuffer::fill_buffer( BufferHandle buffer, u32 offset, u32 size, u32 data ) {
    Buffer* vk_buffer = gpu_device->access_buffer( buffer );

    vkCmdFillBuffer( vk_command_buffer, vk_buffer->vk_buffer, VkDeviceSize( offset ), size ? VkDeviceSize( size ) : VkDeviceSize( vk_buffer->size ), data);
}

void CommandBuffer::push_marker( const char* name ) {

    GPUTimeQuery* time_query = thread_frame_pool->time_queries->push( name );
    vkCmdWriteTimestamp( vk_command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, thread_frame_pool->vulkan_timestamp_query_pool, time_query->start_query_index );

    if ( !gpu_device->debug_utils_extension_present )
        return;

    gpu_device->push_marker( vk_command_buffer, name );
}

void CommandBuffer::pop_marker() {

    //device->pop_gpu_timestamp( this );
    GPUTimeQuery* time_query = thread_frame_pool->time_queries->pop();
    vkCmdWriteTimestamp( vk_command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, thread_frame_pool->vulkan_timestamp_query_pool, time_query->end_query_index );

    if ( !gpu_device->debug_utils_extension_present )
        return;

    gpu_device->pop_marker( vk_command_buffer );
}

u32 CommandBuffer::get_subgroup_sized( u32 group ) {

    return raptor::ceilu32( group * 1.f / gpu_device->subgroup_size );
}

void CommandBuffer::upload_texture_data( TextureHandle texture_handle, void* texture_data, BufferHandle staging_buffer_handle, sizet staging_buffer_offset ) {

    Texture* texture = gpu_device->access_texture( texture_handle );
    Buffer* staging_buffer = gpu_device->access_buffer( staging_buffer_handle );
    u32 image_size = texture->width * texture->height * 4;

    // Copy buffer_data to staging buffer
    memcpy( staging_buffer->mapped_data + staging_buffer_offset, texture_data, static_cast< size_t >( image_size ) );

    VkBufferImageCopy region = {};
    region.bufferOffset = staging_buffer_offset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { texture->width, texture->height, texture->depth };

    // Pre copy memory barrier to perform layout transition
    util_add_image_barrier( gpu_device, vk_command_buffer, texture, RESOURCE_STATE_COPY_DEST, 0, 1, false );
    // Copy from the staging buffer to the image
    vkCmdCopyBufferToImage( vk_command_buffer, staging_buffer->vk_buffer, texture->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

    // Post copy memory barrier
    util_add_image_barrier_ext( gpu_device,vk_command_buffer, texture, RESOURCE_STATE_COPY_SOURCE,
                                0, 1, 0, 1, false, gpu_device->vulkan_transfer_queue_family, gpu_device->vulkan_main_queue_family,
                                QueueType::CopyTransfer, QueueType::Graphics );
}

void CommandBuffer::copy_texture( TextureHandle src_, TextureHandle dst_, ResourceState dst_state ) {
    Texture* src = gpu_device->access_texture( src_ );
    Texture* dst = gpu_device->access_texture( dst_ );

    VkImageCopy region = {};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;

    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;

    region.dstOffset = { 0, 0, 0 };
    region.extent = { src->width, src->height, src->depth };

    // Copy from the staging buffer to the image
    util_add_image_barrier( gpu_device, vk_command_buffer, src, RESOURCE_STATE_COPY_SOURCE, 0, 1, false );
    // TODO(marco): maybe we need a state per mip?
    ResourceState old_state = dst->state;
    util_add_image_barrier( gpu_device, vk_command_buffer, dst, RESOURCE_STATE_COPY_DEST, 0, 1, false );

    vkCmdCopyImage( vk_command_buffer, src->vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

    // Prepare first mip to create lower mipmaps
    if ( dst->mip_level_count > 1 ) {
        util_add_image_barrier( gpu_device, vk_command_buffer, dst, RESOURCE_STATE_COPY_SOURCE, 0, 1, false );
    }

    i32 w = dst->width;
    i32 h = dst->height;

    for ( int mip_index = 1; mip_index < dst->mip_level_count; ++mip_index ) {
        util_add_image_barrier( gpu_device, vk_command_buffer, dst->vk_image, old_state, RESOURCE_STATE_COPY_DEST, mip_index, 1, false );

        VkImageBlit blit_region{ };
        blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.srcSubresource.mipLevel = mip_index - 1;
        blit_region.srcSubresource.baseArrayLayer = 0;
        blit_region.srcSubresource.layerCount = 1;

        blit_region.srcOffsets[ 0 ] = { 0, 0, 0 };
        blit_region.srcOffsets[ 1 ] = { w, h, 1 };

        w /= 2;
        h /= 2;

        blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.dstSubresource.mipLevel = mip_index;
        blit_region.dstSubresource.baseArrayLayer = 0;
        blit_region.dstSubresource.layerCount = 1;

        blit_region.dstOffsets[ 0 ] = { 0, 0, 0 };
        blit_region.dstOffsets[ 1 ] = { w, h, 1 };

        vkCmdBlitImage( vk_command_buffer, dst->vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_region, VK_FILTER_LINEAR );

        // Prepare current mip for next level
        util_add_image_barrier( gpu_device, vk_command_buffer, dst->vk_image, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_COPY_SOURCE, mip_index, 1, false );
    }

    // Transition
    util_add_image_barrier( gpu_device, vk_command_buffer, dst, dst_state, 0, dst->mip_level_count, false );
}

void CommandBuffer::copy_texture( TextureHandle src_, TextureSubResource src_sub, TextureHandle dst_, TextureSubResource dst_sub, ResourceState dst_state ) {
    Texture* src = gpu_device->access_texture( src_ );
    Texture* dst = gpu_device->access_texture( dst_ );

    const bool src_is_depth = TextureFormat::is_depth_only( src->vk_format );
    const bool dst_is_depth = TextureFormat::is_depth_only( dst->vk_format );

    VkImageCopy region = {};
    region.srcSubresource.aspectMask = src_is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = src_sub.mip_base_level;
    region.srcSubresource.baseArrayLayer = src_sub.array_base_layer;
    region.srcSubresource.layerCount = src_sub.array_layer_count;

    region.dstSubresource.aspectMask = dst_is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;;
    region.dstSubresource.mipLevel = dst_sub.mip_base_level;
    region.dstSubresource.baseArrayLayer = dst_sub.array_base_layer;
    region.dstSubresource.layerCount = dst_sub.array_layer_count;

    region.dstOffset = { 0, 0, 0 };
    region.extent = { src->width, src->height, src->depth };

    // Copy from the staging buffer to the image
    util_add_image_barrier( gpu_device, vk_command_buffer, src, RESOURCE_STATE_COPY_SOURCE, 0, 1, src_is_depth );
    // TODO(marco): maybe we need a state per mip?
    ResourceState old_state = dst->state;
    util_add_image_barrier( gpu_device, vk_command_buffer, dst, RESOURCE_STATE_COPY_DEST, 0, 1, dst_is_depth );

    vkCmdCopyImage( vk_command_buffer, src->vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

    // Prepare first mip to create lower mipmaps
    if ( dst->mip_level_count > 1 ) {
        util_add_image_barrier( gpu_device, vk_command_buffer, dst, RESOURCE_STATE_COPY_SOURCE, 0, 1, src_is_depth );
    }

    i32 w = dst->width;
    i32 h = dst->height;

    for ( int mip_index = 1; mip_index < dst->mip_level_count; ++mip_index ) {
        util_add_image_barrier( gpu_device, vk_command_buffer, dst->vk_image, old_state, RESOURCE_STATE_COPY_DEST, mip_index, 1, dst_is_depth );

        VkImageBlit blit_region{ };
        blit_region.srcSubresource.aspectMask = src_is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.srcSubresource.mipLevel = mip_index - 1;
        blit_region.srcSubresource.baseArrayLayer = 0;
        blit_region.srcSubresource.layerCount = 1;

        blit_region.srcOffsets[ 0 ] = { 0, 0, 0 };
        blit_region.srcOffsets[ 1 ] = { w, h, 1 };

        w /= 2;
        h /= 2;

        blit_region.dstSubresource.aspectMask = dst_is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;;
        blit_region.dstSubresource.mipLevel = mip_index;
        blit_region.dstSubresource.baseArrayLayer = 0;
        blit_region.dstSubresource.layerCount = 1;

        blit_region.dstOffsets[ 0 ] = { 0, 0, 0 };
        blit_region.dstOffsets[ 1 ] = { w, h, 1 };

        vkCmdBlitImage( vk_command_buffer, dst->vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_region, VK_FILTER_LINEAR );

        // Prepare current mip for next level
        util_add_image_barrier( gpu_device, vk_command_buffer, dst->vk_image, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_COPY_SOURCE, mip_index, 1, false );
    }

    // Transition
    util_add_image_barrier( gpu_device, vk_command_buffer, dst, dst_state, 0, dst->mip_level_count, dst_is_depth );
}

void CommandBuffer::copy_buffer( BufferHandle src, sizet src_offset, BufferHandle dst, sizet dst_offset, sizet size ) {
    Buffer* src_buffer = gpu_device->access_buffer( src );
    Buffer* dst_buffer = gpu_device->access_buffer( dst );

    VkBufferCopy copy_region{ };
    copy_region.srcOffset = src_offset;
    copy_region.dstOffset = dst_offset;
    copy_region.size = size;

    vkCmdCopyBuffer( vk_command_buffer, src_buffer->vk_buffer, dst_buffer->vk_buffer, 1, &copy_region );
}

void CommandBuffer::upload_buffer_data( BufferHandle buffer_handle, void* buffer_data, BufferHandle staging_buffer_handle, sizet staging_buffer_offset ) {

    Buffer* buffer = gpu_device->access_buffer( buffer_handle );
    Buffer* staging_buffer = gpu_device->access_buffer( staging_buffer_handle );
    u32 copy_size = buffer->size;

    // Copy buffer_data to staging buffer
    memcpy( staging_buffer->mapped_data + staging_buffer_offset, buffer_data, static_cast< size_t >( copy_size ) );

    VkBufferCopy region{};
    region.srcOffset = staging_buffer_offset;
    region.dstOffset = 0;
    region.size = copy_size;

    vkCmdCopyBuffer( vk_command_buffer, staging_buffer->vk_buffer, buffer->vk_buffer, 1, &region );

    util_add_buffer_barrier_ext( gpu_device, vk_command_buffer, buffer->vk_buffer, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_UNDEFINED,
                                 copy_size, gpu_device->vulkan_transfer_queue_family, gpu_device->vulkan_main_queue_family,
                                 QueueType::CopyTransfer, QueueType::Graphics );
}

void CommandBuffer::upload_buffer_data( BufferHandle src_, BufferHandle dst_ ) {
    Buffer* src = gpu_device->access_buffer( src_ );
    Buffer* dst = gpu_device->access_buffer( dst_ );

    RASSERT( src->size == dst->size );

    u32 copy_size = src->size;

    VkBufferCopy region{};
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = copy_size;

    vkCmdCopyBuffer( vk_command_buffer, src->vk_buffer, dst->vk_buffer, 1, &region );
}

// CommandBufferManager ///////////////////////////////////////////////////
void CommandBufferManager::init( GpuDevice* gpu_, u32 num_threads ) {

    gpu = gpu_;
    num_pools_per_frame = num_threads;

    // Create pools: num frames * num threads;
    const u32 total_pools = num_pools_per_frame * k_max_frames;
    // Init per thread-frame used buffers
    used_buffers.init( gpu->allocator, total_pools, total_pools );
    used_secondary_command_buffers.init( gpu->allocator, total_pools, total_pools );

    for ( u32 i = 0; i < total_pools; i++ ) {
        used_buffers[ i ] = 0;
        used_secondary_command_buffers[ i ] = 0;
    }

    // Create command buffers: pools * buffers per pool
    const u32 total_buffers = total_pools * num_command_buffers_per_thread;
    command_buffers.init( gpu->allocator, total_buffers, total_buffers );

    const u32 total_secondary_buffers = total_pools * k_secondary_command_buffers_count;
    secondary_command_buffers.init( gpu->allocator, total_secondary_buffers );

    for ( u32 i = 0; i < total_buffers; i++ ) {
        VkCommandBufferAllocateInfo cmd = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };

        const u32 frame_index = i / ( num_command_buffers_per_thread * num_pools_per_frame );
        const u32 thread_index = ( i / num_command_buffers_per_thread ) % num_pools_per_frame;
        const u32 pool_index = pool_from_indices( frame_index, thread_index );
        //rprint( "Indices i:%u f:%u t:%u p:%u\n", i, frame_index, thread_index, pool_index );
        cmd.commandPool = gpu->thread_frame_pools[ pool_index ].vulkan_command_pool;
        cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd.commandBufferCount = 1;

        CommandBuffer& current_command_buffer = command_buffers[ i ];
        vkAllocateCommandBuffers( gpu->vulkan_device, &cmd, &current_command_buffer.vk_command_buffer );

        // TODO(marco): move to have a ring per queue per thread
        current_command_buffer.handle = i;
        current_command_buffer.thread_frame_pool = &gpu->thread_frame_pools[ pool_index ];
        current_command_buffer.init( gpu );
    }

    u32 handle = total_buffers;
    for ( u32 pool_index = 0; pool_index < total_pools; ++pool_index ) {
        VkCommandBufferAllocateInfo cmd = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };

        cmd.commandPool = gpu->thread_frame_pools[ pool_index ].vulkan_command_pool;
        cmd.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        cmd.commandBufferCount = k_secondary_command_buffers_count;

        VkCommandBuffer secondary_buffers[ k_secondary_command_buffers_count ];
        vkAllocateCommandBuffers( gpu->vulkan_device, &cmd, secondary_buffers );

        for ( u32 scb_index = 0; scb_index < k_secondary_command_buffers_count; ++scb_index ) {
            CommandBuffer cb{ };
            cb.vk_command_buffer = secondary_buffers[ scb_index ];

            cb.handle = handle++;
            cb.thread_frame_pool = &gpu->thread_frame_pools[ pool_index ];
            cb.init( gpu );

            // NOTE(marco): access to the descriptor pool has to be synchronized
            // across theads. Don't allow for now
            secondary_command_buffers.push( cb );
        }
    }

    //rprint( "Done\n" );
}

void CommandBufferManager::shutdown() {

    for ( u32 i = 0; i < command_buffers.size; i++ ) {
        command_buffers[ i ].shutdown();
    }

    for ( u32 i = 0; i < secondary_command_buffers.size; ++i ) {
        secondary_command_buffers[ i ].shutdown();
    }

    command_buffers.shutdown();
    secondary_command_buffers.shutdown();
    used_buffers.shutdown();
    used_secondary_command_buffers.shutdown();
}

void CommandBufferManager::reset_pools( u32 frame_index ) {

    for ( u32 i = 0; i < num_pools_per_frame; i++ ) {
        const u32 pool_index = pool_from_indices( frame_index, i );
        vkResetCommandPool( gpu->vulkan_device, gpu->thread_frame_pools[ pool_index ].vulkan_command_pool, 0 );

        used_buffers[ pool_index ] = 0;
        used_secondary_command_buffers[ pool_index ] = 0;
    }
}

CommandBuffer* CommandBufferManager::get_command_buffer( u32 frame, u32 thread_index, bool begin ) {
    const u32 pool_index = pool_from_indices( frame, thread_index );
    u32 current_used_buffer = used_buffers[ pool_index ];
    // TODO: how to handle fire-and-forget command buffers ?
    RASSERT( current_used_buffer < num_command_buffers_per_thread );
    if ( begin ) {
        used_buffers[ pool_index ] = current_used_buffer + 1;
    }

    CommandBuffer* cb = &command_buffers[ ( pool_index * num_command_buffers_per_thread ) + current_used_buffer ];
    if ( begin ) {
        cb->reset();
        cb->begin();

        // Timestamp queries
        GpuThreadFramePools* thread_pools = cb->thread_frame_pool;
        thread_pools->time_queries->reset();
        vkCmdResetQueryPool( cb->vk_command_buffer, thread_pools->vulkan_timestamp_query_pool, 0, thread_pools->time_queries->time_queries.size );

        // Pipeline statistics
        vkCmdResetQueryPool( cb->vk_command_buffer, thread_pools->vulkan_pipeline_stats_query_pool, 0, GpuPipelineStatistics::Count );

        vkCmdBeginQuery( cb->vk_command_buffer, thread_pools->vulkan_pipeline_stats_query_pool, 0, 0 );
    }
    return cb;
}

CommandBuffer* CommandBufferManager::get_secondary_command_buffer( u32 frame, u32 thread_index ) {
    const u32 pool_index = pool_from_indices( frame, thread_index );
    u32 current_used_buffer = used_secondary_command_buffers[ pool_index ];
    used_secondary_command_buffers[ pool_index ] = current_used_buffer + 1;

    RASSERT( current_used_buffer < k_secondary_command_buffers_count );

    CommandBuffer* cb = &secondary_command_buffers[ ( pool_index * k_secondary_command_buffers_count ) + current_used_buffer ];
    return cb;
}

u32 CommandBufferManager::pool_from_indices( u32 frame_index, u32 thread_index ) {
    return (frame_index * num_pools_per_frame) + thread_index;
}

} // namespace raptor
