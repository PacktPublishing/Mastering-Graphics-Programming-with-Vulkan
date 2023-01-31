#include "gpu_resources.hpp"

#include "gpu_device.hpp"

namespace raptor {


// DepthStencilCreation ////////////////////////////////////

DepthStencilCreation& DepthStencilCreation::set_depth( bool write, VkCompareOp comparison_test ) {
    depth_write_enable = write;
    depth_comparison = comparison_test;
    // Setting depth like this means we want to use the depth test.
    depth_enable = 1;

    return *this;
}

// BlendState  /////////////////////////////////////////////
BlendState& BlendState::set_color( VkBlendFactor source, VkBlendFactor destination, VkBlendOp operation ) {
    source_color = source;
    destination_color = destination;
    color_operation = operation;
    blend_enabled = 1;

    return *this;
}

BlendState& BlendState::set_alpha( VkBlendFactor source, VkBlendFactor destination, VkBlendOp operation ) {
    source_alpha = source;
    destination_alpha = destination;
    alpha_operation = operation;
    separate_blend = 1;

    return *this;
}

BlendState& BlendState::set_color_write_mask( ColorWriteEnabled::Mask value ) {
    color_write_mask = value;

    return *this;
}

// BlendStateCreation //////////////////////////////////////
BlendStateCreation& BlendStateCreation::reset() {
    active_states = 0;

    return *this;
}

BlendState& BlendStateCreation::add_blend_state() {
    return blend_states[active_states++];
}

// BufferCreation //////////////////////////////////////////
BufferCreation& BufferCreation::reset() {
    size = 0;
    initial_data = nullptr;

    return *this;
}

BufferCreation& BufferCreation::set( VkBufferUsageFlags flags, ResourceUsageType::Enum usage_, u32 size_ ) {
    type_flags = flags;
    usage = usage_;
    size = size_;

    return *this;
}

BufferCreation& BufferCreation::set_data( void* data_ ) {
    initial_data = data_;

    return *this;
}

BufferCreation& BufferCreation::set_name( const char* name_ ) {
    name = name_;

    return *this;
}

// TextureCreation /////////////////////////////////////////
TextureCreation& TextureCreation::set_size( u16 width_, u16 height_, u16 depth_ ) {
    width = width_;
    height = height_;
    depth = depth_;

    return *this;
}

TextureCreation& TextureCreation::set_flags( u8 mipmaps_, u8 flags_ ) {
    mipmaps = mipmaps_;
    flags = flags_;

    return *this;
}

TextureCreation& TextureCreation::set_format_type( VkFormat format_, TextureType::Enum type_ ) {
    format = format_;
    type = type_;

    return *this;
}

TextureCreation& TextureCreation::set_name( const char* name_ ) {
    name = name_;

    return *this;
}

TextureCreation& TextureCreation::set_data( void* data_ ) {
    initial_data = data_;

    return *this;
}

// SamplerCreation /////////////////////////////////////////
SamplerCreation& SamplerCreation::set_min_mag_mip( VkFilter min, VkFilter mag, VkSamplerMipmapMode mip ) {
    min_filter = min;
    mag_filter = mag;
    mip_filter = mip;

    return *this;
}

SamplerCreation& SamplerCreation::set_address_mode_u( VkSamplerAddressMode u ) {
    address_mode_u = u;

    return *this;
}

SamplerCreation& SamplerCreation::set_address_mode_uv( VkSamplerAddressMode u, VkSamplerAddressMode v ) {
    address_mode_u = u;
    address_mode_v = v;

    return *this;
}

SamplerCreation& SamplerCreation::set_address_mode_uvw( VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w ) {
    address_mode_u = u;
    address_mode_v = v;
    address_mode_w = w;

    return *this;
}

SamplerCreation& SamplerCreation::set_name( const char* name_ ) {
    name = name_;

    return *this;
}


// ShaderStateCreation /////////////////////////////////////
ShaderStateCreation& ShaderStateCreation::reset() {
    stages_count = 0;

    return *this;
}

ShaderStateCreation& ShaderStateCreation::set_name( const char* name_ ) {
    name = name_;

    return *this;
}

ShaderStateCreation& ShaderStateCreation::add_stage( const char* code, u32 code_size, VkShaderStageFlagBits type ) {
    stages[stages_count].code = code;
    stages[stages_count].code_size = code_size;
    stages[stages_count].type = type;
    ++stages_count;

    return *this;
}

ShaderStateCreation& ShaderStateCreation::set_spv_input( bool value ) {
    spv_input = value;
    return *this;
}

// DescriptorSetLayoutCreation ////////////////////////////////////////////
DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::reset() {
    num_bindings = 0;
    set_index = 0;
    return *this;
}

DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::add_binding( const Binding& binding ) {
    bindings[num_bindings++] = binding;
    return *this;
}

DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::add_binding_at_index( const Binding& binding, int index ) {
    bindings[index] = binding;
    num_bindings = (index + 1) > num_bindings ? (index + 1) : num_bindings;
    return *this;
}

DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::set_name( cstring name_ ) {
    name = name_;
    return *this;
}


DescriptorSetLayoutCreation& DescriptorSetLayoutCreation::set_set_index( u32 index ) {
    set_index = index;
    return *this;
}

// DescriptorSetCreation //////////////////////////////////////////////////
DescriptorSetCreation& DescriptorSetCreation::reset() {
    num_resources = 0;
    return *this;
}

DescriptorSetCreation& DescriptorSetCreation::set_layout( DescriptorSetLayoutHandle layout_ ) {
    layout = layout_;
    return *this;
}

DescriptorSetCreation& DescriptorSetCreation::texture( TextureHandle texture, u16 binding ) {
    // Set a default sampler
    samplers[ num_resources ] = k_invalid_sampler;
    bindings[ num_resources ] = binding;
    resources[ num_resources++ ] = texture.index;
    return *this;
}

DescriptorSetCreation& DescriptorSetCreation::buffer( BufferHandle buffer, u16 binding ) {
    samplers[ num_resources ] = k_invalid_sampler;
    bindings[ num_resources ] = binding;
    resources[ num_resources++ ] = buffer.index;
    return *this;
}

DescriptorSetCreation& DescriptorSetCreation::texture_sampler( TextureHandle texture, SamplerHandle sampler, u16 binding ) {
    bindings[ num_resources ] = binding;
    resources[ num_resources ] = texture.index;
    samplers[ num_resources++ ] = sampler;
    return *this;
}

DescriptorSetCreation& DescriptorSetCreation::set_name( cstring name_ ) {
    name = name_;
    return *this;
}

// VertexInputCreation /////////////////////////////////////
VertexInputCreation& VertexInputCreation::reset() {
    num_vertex_streams = num_vertex_attributes = 0;
    return *this;
}

VertexInputCreation& VertexInputCreation::add_vertex_stream( const VertexStream& stream ) {
    vertex_streams[num_vertex_streams++] = stream;
    return *this;
}

VertexInputCreation& VertexInputCreation::add_vertex_attribute( const VertexAttribute& attribute ) {
    vertex_attributes[num_vertex_attributes++] = attribute;
    return *this;
}

// RenderPassOutput ////////////////////////////////////////
RenderPassOutput& RenderPassOutput::reset() {
    num_color_formats = 0;
    for ( u32 i = 0; i < k_max_image_outputs; ++i) {
        color_formats[i] = VK_FORMAT_UNDEFINED;
    }
    depth_stencil_format = VK_FORMAT_UNDEFINED;
    color_operation = depth_operation = stencil_operation = RenderPassOperation::DontCare;
    return *this;
}

RenderPassOutput& RenderPassOutput::color( VkFormat format ) {
    color_formats[ num_color_formats++ ] = format;
    return *this;
}

RenderPassOutput& RenderPassOutput::depth( VkFormat format ) {
    depth_stencil_format = format;
    return *this;
}

RenderPassOutput& RenderPassOutput::set_operations( RenderPassOperation::Enum color_, RenderPassOperation::Enum depth_, RenderPassOperation::Enum stencil_ ) {
    color_operation = color_;
    depth_operation = depth_;
    stencil_operation = stencil_;

    return *this;
}

// PipelineCreation ////////////////////////////////////////
PipelineCreation& PipelineCreation::add_descriptor_set_layout( DescriptorSetLayoutHandle handle ) {
    descriptor_set_layout[num_active_layouts++] = handle;
    return *this;
}

RenderPassOutput& PipelineCreation::render_pass_output() {
    return render_pass;
}

// RenderPassCreation //////////////////////////////////////
RenderPassCreation& RenderPassCreation::reset() {
    num_render_targets = 0;
    depth_stencil_texture = k_invalid_texture;
    resize = 0;
    scale_x = 1.f;
    scale_y = 1.f;
    color_operation = depth_operation = stencil_operation = RenderPassOperation::DontCare;

    return *this;
}

RenderPassCreation& RenderPassCreation::add_render_texture( TextureHandle texture ) {
    output_textures[num_render_targets++] = texture;

    return *this;
}

RenderPassCreation& RenderPassCreation::set_scaling( f32 scale_x_, f32 scale_y_, u8 resize_ ) {
    scale_x = scale_x_;
    scale_y = scale_y_;
    resize = resize_;

    return *this;
}

RenderPassCreation& RenderPassCreation::set_depth_stencil_texture( TextureHandle texture ) {
    depth_stencil_texture = texture;

    return *this;
}

RenderPassCreation& RenderPassCreation::set_name( const char* name_ ) {
    name = name_;

    return *this;
}

RenderPassCreation& RenderPassCreation::set_type( RenderPassType::Enum type_ ) {
    type = type_;

    return *this;
}

RenderPassCreation& RenderPassCreation::set_operations( RenderPassOperation::Enum color_, RenderPassOperation::Enum depth_, RenderPassOperation::Enum stencil_ ) {
    color_operation = color_;
    depth_operation = depth_;
    stencil_operation = stencil_;

    return *this;
}

// ExecutionBarrier ////////////////////////////////////////
ExecutionBarrier& ExecutionBarrier::reset() {
    num_image_barriers = num_memory_barriers = 0;
    source_pipeline_stage = PipelineStage::DrawIndirect;
    destination_pipeline_stage = PipelineStage::DrawIndirect;
    return *this;
}

ExecutionBarrier& ExecutionBarrier::set( PipelineStage::Enum source, PipelineStage::Enum destination ) {
    source_pipeline_stage = source;
    destination_pipeline_stage = destination;

    return *this;
}

ExecutionBarrier& ExecutionBarrier::add_image_barrier( const ImageBarrier& image_barrier ) {
    image_barriers[num_image_barriers++] = image_barrier;

    return *this;
}

ExecutionBarrier& ExecutionBarrier::add_memory_barrier( const MemoryBarrier& memory_barrier ) {
    memory_barriers[ num_memory_barriers++ ] = memory_barrier;

    return *this;
}

///////////////////////////////////////////////////////////////////////////

cstring to_compiler_extension( VkShaderStageFlagBits value ) {
    switch ( value ) {
        case VK_SHADER_STAGE_VERTEX_BIT:
            return "vert";
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            return "frag";
        case VK_SHADER_STAGE_COMPUTE_BIT:
            return "comp";
        default:
            return "";
    }
}

//
cstring to_stage_defines( VkShaderStageFlagBits value ) {
    switch ( value ) {
        case VK_SHADER_STAGE_VERTEX_BIT:
            return "VERTEX";
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            return "FRAGMENT";
        case VK_SHADER_STAGE_COMPUTE_BIT:
            return "COMPUTE";
        default:
            return "";
    }
}

//
//
VkImageType to_vk_image_type( TextureType::Enum type ) {
    static VkImageType s_vk_target[ TextureType::Count ] = { VK_IMAGE_TYPE_1D, VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_3D, VK_IMAGE_TYPE_1D, VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_3D };
    return s_vk_target[ type ];
}

//
//
VkImageViewType to_vk_image_view_type( TextureType::Enum type ) {
    static VkImageViewType s_vk_data[] = { VK_IMAGE_VIEW_TYPE_1D, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_3D, VK_IMAGE_VIEW_TYPE_1D_ARRAY, VK_IMAGE_VIEW_TYPE_2D_ARRAY, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY };
    return s_vk_data[ type ];
}

//
//
VkFormat to_vk_vertex_format( VertexComponentFormat::Enum value ) {
    // Float, Float2, Float3, Float4, Mat4, Byte, Byte4N, UByte, UByte4N, Short2, Short2N, Short4, Short4N, Uint, Uint2, Uint4, Count
    static VkFormat s_vk_vertex_formats[ VertexComponentFormat::Count ] = { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT, /*MAT4 TODO*/VK_FORMAT_R32G32B32A32_SFLOAT,
                                                                          VK_FORMAT_R8_SINT, VK_FORMAT_R8G8B8A8_SNORM, VK_FORMAT_R8_UINT, VK_FORMAT_R8G8B8A8_UINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16_SNORM,
                                                                          VK_FORMAT_R16G16B16A16_SINT, VK_FORMAT_R16G16B16A16_SNORM, VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32A32_UINT };

    return s_vk_vertex_formats[ value ];
}

//
//
VkPipelineStageFlags to_vk_pipeline_stage( PipelineStage::Enum value ) {
    static VkPipelineStageFlags s_vk_values[] = { VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
    return s_vk_values[ value ];
}

VkAccessFlags util_to_vk_access_flags( ResourceState state ) {
    VkAccessFlags ret = 0;
    if ( state & RESOURCE_STATE_COPY_SOURCE ) {
        ret |= VK_ACCESS_TRANSFER_READ_BIT;
    }
    if ( state & RESOURCE_STATE_COPY_DEST ) {
        ret |= VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    if ( state & RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ) {
        ret |= VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if ( state & RESOURCE_STATE_INDEX_BUFFER ) {
        ret |= VK_ACCESS_INDEX_READ_BIT;
    }
    if ( state & RESOURCE_STATE_UNORDERED_ACCESS ) {
        ret |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    if ( state & RESOURCE_STATE_INDIRECT_ARGUMENT ) {
        ret |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }
    if ( state & RESOURCE_STATE_RENDER_TARGET ) {
        ret |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if ( state & RESOURCE_STATE_DEPTH_WRITE ) {
        ret |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    if ( state & RESOURCE_STATE_SHADER_RESOURCE ) {
        ret |= VK_ACCESS_SHADER_READ_BIT;
    }
    if ( state & RESOURCE_STATE_PRESENT ) {
        ret |= VK_ACCESS_MEMORY_READ_BIT;
    }
    if ( state & RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE ) {
        ret |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    }

    return ret;
}

VkImageLayout util_to_vk_image_layout( ResourceState usage ) {
    if ( usage & RESOURCE_STATE_COPY_SOURCE )
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    if ( usage & RESOURCE_STATE_COPY_DEST )
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    if ( usage & RESOURCE_STATE_RENDER_TARGET )
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    if ( usage & RESOURCE_STATE_DEPTH_WRITE )
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    if ( usage & RESOURCE_STATE_DEPTH_READ )
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    if ( usage & RESOURCE_STATE_UNORDERED_ACCESS )
        return VK_IMAGE_LAYOUT_GENERAL;

    if ( usage & RESOURCE_STATE_SHADER_RESOURCE )
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if ( usage & RESOURCE_STATE_PRESENT )
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    if ( usage == RESOURCE_STATE_COMMON )
        return VK_IMAGE_LAYOUT_GENERAL;

    if ( usage == RESOURCE_STATE_SHADING_RATE_SOURCE )
        return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;

    return VK_IMAGE_LAYOUT_UNDEFINED;
}

VkPipelineStageFlags util_determine_pipeline_stage_flags( VkAccessFlags access_flags, QueueType::Enum queue_type ) {
    VkPipelineStageFlags flags = 0;

    switch ( queue_type ) {
        case QueueType::Graphics:
        {
            if ( ( access_flags & ( VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT ) ) != 0 )
                flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

            if ( ( access_flags & ( VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT ) ) != 0 ) {
                flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
                flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

                // TODO(marco): check RT extension is present/enabled
                flags |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
            }

            if ( ( access_flags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT ) != 0 )
                flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

            if ( ( access_flags & ( VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR ) ) != 0 )
                flags |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;

            if ( ( access_flags & ( VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT ) ) != 0 )
                flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            if ( ( access_flags & VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR ) != 0 )
                flags = VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

            if ( ( access_flags & ( VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT ) ) != 0 )
                flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

            break;
        }
        case QueueType::Compute:
        {
            if ( ( access_flags & ( VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT ) ) != 0 ||
                 ( access_flags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT ) != 0 ||
                 ( access_flags & ( VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT ) ) != 0 ||
                 ( access_flags & ( VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT ) ) != 0 )
                return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

            if ( ( access_flags & ( VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT ) ) != 0 )
                flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

            break;
        }
        case QueueType::CopyTransfer: return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        default: break;
    }

    // Compatible with both compute and graphics queues
    if ( ( access_flags & VK_ACCESS_INDIRECT_COMMAND_READ_BIT ) != 0 )
        flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;

    if ( ( access_flags & ( VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT ) ) != 0 )
        flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;

    if ( ( access_flags & ( VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT ) ) != 0 )
        flags |= VK_PIPELINE_STAGE_HOST_BIT;

    if ( flags == 0 )
        flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    return flags;
}


void util_add_image_barrier( GpuDevice* gpu, VkCommandBuffer command_buffer, VkImage image, ResourceState old_state, ResourceState new_state, u32 base_mip_level, u32 mip_count, bool is_depth ) {
    
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = mip_count;

    barrier.subresourceRange.baseMipLevel = base_mip_level;
    barrier.oldLayout = util_to_vk_image_layout( old_state );
    barrier.newLayout = util_to_vk_image_layout( new_state );
    barrier.srcAccessMask = util_to_vk_access_flags( old_state );
    barrier.dstAccessMask = util_to_vk_access_flags( new_state );

    const VkPipelineStageFlags source_stage_mask = util_determine_pipeline_stage_flags( barrier.srcAccessMask, QueueType::Graphics );
    const VkPipelineStageFlags destination_stage_mask = util_determine_pipeline_stage_flags( barrier.dstAccessMask, QueueType::Graphics );

    vkCmdPipelineBarrier( command_buffer, source_stage_mask, destination_stage_mask, 0,
                          0, nullptr, 0, nullptr, 1, &barrier );
}

void util_add_image_barrier( GpuDevice* gpu, VkCommandBuffer command_buffer, Texture* texture, ResourceState old_state, ResourceState new_state, u32 base_mip_level, u32 mip_count, bool is_depth ) {

    //rprint( "Transitioning Texture %s from %s to %s\n", texture->name, ResourceStateName( texture->state ), ResourceStateName( new_state ) );
    util_add_image_barrier( gpu, command_buffer, texture->vk_image, old_state, new_state, base_mip_level, mip_count, is_depth );
}

void util_add_image_barrier_ext( GpuDevice* gpu, VkCommandBuffer command_buffer, VkImage image, ResourceState old_state, ResourceState new_state,
                                 u32 base_mip_level, u32 mip_count, u32 base_array_layer, u32 array_layer_count, bool is_depth, u32 source_family, u32 destination_family,
                                 QueueType::Enum source_queue_type, QueueType::Enum destination_queue_type ) {

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = image;
    barrier.srcQueueFamilyIndex = source_family;
    barrier.dstQueueFamilyIndex = destination_family;
    barrier.subresourceRange.aspectMask = is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = base_array_layer;
    barrier.subresourceRange.layerCount = array_layer_count;
    barrier.subresourceRange.levelCount = mip_count;

    barrier.subresourceRange.baseMipLevel = base_mip_level;
    barrier.oldLayout = util_to_vk_image_layout( old_state );
    barrier.newLayout = util_to_vk_image_layout( new_state );
    barrier.srcAccessMask = util_to_vk_access_flags( old_state );
    barrier.dstAccessMask = util_to_vk_access_flags( new_state );

    const VkPipelineStageFlags source_stage_mask = util_determine_pipeline_stage_flags( barrier.srcAccessMask, source_queue_type );
    const VkPipelineStageFlags destination_stage_mask = util_determine_pipeline_stage_flags( barrier.dstAccessMask, destination_queue_type );

    vkCmdPipelineBarrier( command_buffer, source_stage_mask, destination_stage_mask, 0,
                          0, nullptr, 0, nullptr, 1, &barrier );
}

void util_add_image_barrier_ext( GpuDevice* gpu, VkCommandBuffer command_buffer, Texture* texture, ResourceState old_state, ResourceState new_state,
                                 u32 base_mip_level, u32 mip_count, u32 base_array_layer, u32 array_layer_count, bool is_depth, u32 source_family, u32 destination_family,
                                 QueueType::Enum source_queue_type, QueueType::Enum destination_queue_type ) {

    util_add_image_barrier_ext( gpu, command_buffer, texture->vk_image, old_state, new_state, base_mip_level, mip_count, base_array_layer, array_layer_count,
                                is_depth, source_family, destination_family, source_queue_type, destination_queue_type );
}

void util_add_buffer_barrier( GpuDevice* gpu, VkCommandBuffer command_buffer, VkBuffer buffer, ResourceState old_state, ResourceState new_state, u32 buffer_size ) {

    util_add_buffer_barrier_ext( gpu, command_buffer, buffer, old_state, new_state, buffer_size,
                                 VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, QueueType::Graphics, QueueType::Graphics );
}

void util_add_buffer_barrier_ext( GpuDevice* gpu, VkCommandBuffer command_buffer, VkBuffer buffer, ResourceState old_state, ResourceState new_state,
                                  u32 buffer_size, u32 source_family, u32 destination_family,
                                  QueueType::Enum source_queue_type, QueueType::Enum destination_queue_type ) {

    VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    barrier.buffer = buffer;
    barrier.srcQueueFamilyIndex = source_family;
    barrier.dstQueueFamilyIndex = destination_family;
    barrier.offset = 0;
    barrier.size = buffer_size;
    barrier.srcAccessMask = util_to_vk_access_flags( old_state );
    barrier.dstAccessMask = util_to_vk_access_flags( new_state );

    const VkPipelineStageFlags source_stage_mask = util_determine_pipeline_stage_flags( barrier.srcAccessMask, source_queue_type );
    const VkPipelineStageFlags destination_stage_mask = util_determine_pipeline_stage_flags( barrier.dstAccessMask, destination_queue_type );

    vkCmdPipelineBarrier( command_buffer, source_stage_mask, destination_stage_mask, 0,
                          0, nullptr, 1, &barrier, 0, nullptr );
}

} // namespace raptor
