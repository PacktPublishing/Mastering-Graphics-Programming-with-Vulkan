#pragma once

#include "foundation/platform.hpp"

#include "graphics/gpu_enum.hpp"

#include <vulkan/vulkan.h>
#include "external/vk_mem_alloc.h"

namespace raptor {

namespace spirv {
    struct ParseResult;
} // namespace spirv

struct Allocator;
struct GpuDevice;

static const u32                    k_invalid_index = 0xffffffff;

static const u32 k_buffers_pool_size                 = 16384;
static const u32 k_textures_pool_size                = 512;
static const u32 k_render_passes_pool_size           = 256;
static const u32 k_descriptor_set_layouts_pool_size  = 128;
static const u32 k_pipelines_pool_size               = 128;
static const u32 k_shaders_pool_size                 = 128;
static const u32 k_descriptor_sets_pool_size         = 4096;
static const u32 k_samplers_pool_size                = 32;

typedef u32                         ResourceHandle;

struct BufferHandle {
    ResourceHandle                  index;
}; // struct BufferHandle

struct TextureHandle {
    ResourceHandle                  index;
}; // struct TextureHandle

struct ShaderStateHandle {
    ResourceHandle                  index;
}; // struct ShaderStateHandle

struct SamplerHandle {
    ResourceHandle                  index;
}; // struct SamplerHandle

struct DescriptorSetLayoutHandle {
    ResourceHandle                  index;
}; // struct DescriptorSetLayoutHandle

struct DescriptorSetHandle {
    ResourceHandle                  index;
}; // struct DescriptorSetHandle

struct PipelineHandle {
    ResourceHandle                  index;
}; // struct PipelineHandle

struct RenderPassHandle {
	ResourceHandle                  index;
}; // struct RenderPassHandle

struct FramebufferHandle {
    ResourceHandle                  index;
}; // struct PipelineHandle

// Invalid handles
static BufferHandle                 k_invalid_buffer        { k_invalid_index };
static TextureHandle                k_invalid_texture       { k_invalid_index };
static ShaderStateHandle            k_invalid_shader        { k_invalid_index };
static SamplerHandle                k_invalid_sampler       { k_invalid_index };
static DescriptorSetLayoutHandle    k_invalid_layout        { k_invalid_index };
static DescriptorSetHandle          k_invalid_set           { k_invalid_index };
static PipelineHandle               k_invalid_pipeline      { k_invalid_index };
static RenderPassHandle             k_invalid_pass          { k_invalid_index };
static FramebufferHandle            k_invalid_framebuffer   { k_invalid_index };



// Consts ///////////////////////////////////////////////////////////////////////

static const u8                     k_max_image_outputs = 8;                // Maximum number of images/render_targets/fbo attachments usable.
static const u8                     k_max_descriptor_set_layouts = 8;       // Maximum number of layouts in the pipeline.
static const u8                     k_max_shader_stages = 5;                // Maximum simultaneous shader stages. Applicable to all different type of pipelines.
static const u8                     k_max_descriptors_per_set = 16;         // Maximum list elements for both descriptor set layout and descriptor sets.
static const u8                     k_max_vertex_streams = 16;
static const u8                     k_max_vertex_attributes = 16;

static const u32                    k_submit_header_sentinel = 0xfefeb7ba;
static const u32                    k_max_resource_deletions = 64;

// Resource creation structs ////////////////////////////////////////////////////

//
//
struct Rect2D {
    f32                             x = 0.0f;
    f32                             y = 0.0f;
    f32                             width = 0.0f;
    f32                             height = 0.0f;
}; // struct Rect2D

//
//
struct Rect2DInt {
    i16                             x = 0;
    i16                             y = 0;
    u16                             width = 0;
    u16                             height = 0;
}; // struct Rect2D

//
//
struct Viewport {
    Rect2DInt                       rect;
    f32                             min_depth = 0.0f;
    f32                             max_depth = 0.0f;
}; // struct Viewport

//
//
struct ViewportState {
    u32                             num_viewports = 0;
    u32                             num_scissors = 0;

    Viewport*                       viewport = nullptr;
    Rect2DInt*                      scissors = nullptr;
}; // struct ViewportState

//
//
struct StencilOperationState {

    VkStencilOp                     fail = VK_STENCIL_OP_KEEP;
    VkStencilOp                     pass = VK_STENCIL_OP_KEEP;
    VkStencilOp                     depth_fail = VK_STENCIL_OP_KEEP;
    VkCompareOp                     compare = VK_COMPARE_OP_ALWAYS;
    u32                             compare_mask = 0xff;
    u32                             write_mask = 0xff;
    u32                             reference = 0xff;

}; // struct StencilOperationState

//
//
struct DepthStencilCreation {

    StencilOperationState           front;
    StencilOperationState           back;
    VkCompareOp                     depth_comparison = VK_COMPARE_OP_ALWAYS;

    u8                              depth_enable        : 1;
    u8                              depth_write_enable  : 1;
    u8                              stencil_enable      : 1;
    u8                              pad                 : 5;

    // Default constructor
    DepthStencilCreation() : depth_enable( 0 ), depth_write_enable( 0 ), stencil_enable( 0 ) {
    }

    DepthStencilCreation&           set_depth( bool write, VkCompareOp comparison_test );

}; // struct DepthStencilCreation

struct BlendState {

    VkBlendFactor                   source_color        = VK_BLEND_FACTOR_ONE;
    VkBlendFactor                   destination_color   = VK_BLEND_FACTOR_ONE;
    VkBlendOp                       color_operation     = VK_BLEND_OP_ADD;

    VkBlendFactor                   source_alpha        = VK_BLEND_FACTOR_ONE;
    VkBlendFactor                   destination_alpha   = VK_BLEND_FACTOR_ONE;
    VkBlendOp                       alpha_operation     = VK_BLEND_OP_ADD;

    ColorWriteEnabled::Mask         color_write_mask    = ColorWriteEnabled::All_mask;

    u8                              blend_enabled   : 1;
    u8                              separate_blend  : 1;
    u8                              pad             : 6;


    BlendState() : blend_enabled( 0 ), separate_blend( 0 ) {
    }

    BlendState&                     set_color( VkBlendFactor source_color, VkBlendFactor destination_color, VkBlendOp color_operation );
    BlendState&                     set_alpha( VkBlendFactor source_color, VkBlendFactor destination_color, VkBlendOp color_operation );
    BlendState&                     set_color_write_mask( ColorWriteEnabled::Mask value );

}; // struct BlendState

struct BlendStateCreation {

    BlendState                      blend_states[ k_max_image_outputs ];
    u32                             active_states = 0;

    BlendStateCreation&             reset();
    BlendState&                     add_blend_state();

}; // BlendStateCreation

//
//
struct RasterizationCreation {

    VkCullModeFlagBits              cull_mode   = VK_CULL_MODE_NONE;
    VkFrontFace                     front       = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    FillMode::Enum                  fill        = FillMode::Solid;
}; // struct RasterizationCreation

//
//
struct BufferCreation {

    VkBufferUsageFlags              type_flags      = 0;
    ResourceUsageType::Enum         usage           = ResourceUsageType::Immutable;
    u32                             size            = 0;
    u32                             persistent      = 0;
    u32                             device_only     = 0;
    void*                           initial_data    = nullptr;

    const char*                     name            = nullptr;

    BufferCreation&                 reset();
    BufferCreation&                 set( VkBufferUsageFlags flags, ResourceUsageType::Enum usage, u32 size );
    BufferCreation&                 set_data( void* data );
    BufferCreation&                 set_name( const char* name );
    BufferCreation&                 set_persistent( bool value );
    BufferCreation&                 set_device_only( bool value );

}; // struct BufferCreation

//
//
struct TextureCreation {

    void*                           initial_data    = nullptr;
    u16                             width           = 1;
    u16                             height          = 1;
    u16                             depth           = 1;
    u8                              mipmaps         = 1;
    u8                              flags           = 0;    // TextureFlags bitmasks

    VkFormat                        format          = VK_FORMAT_UNDEFINED;
    TextureType::Enum               type            = TextureType::Texture2D;

    TextureHandle                   alias           = k_invalid_texture;

    const char*                     name            = nullptr;

    TextureCreation&                set_size( u16 width, u16 height, u16 depth );
    TextureCreation&                set_flags( u8 mipmaps, u8 flags );
    TextureCreation&                set_format_type( VkFormat format, TextureType::Enum type );
    TextureCreation&                set_name( const char* name );
    TextureCreation&                set_data( void* data );
    TextureCreation&                set_alias( TextureHandle alias );

}; // struct TextureCreation

//
//
struct SamplerCreation {

    VkFilter                        min_filter = VK_FILTER_NEAREST;
    VkFilter                        mag_filter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode             mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSamplerAddressMode            address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode            address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode            address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    const char*                     name = nullptr;

    SamplerCreation&                set_min_mag_mip( VkFilter min, VkFilter mag, VkSamplerMipmapMode mip );
    SamplerCreation&                set_address_mode_u( VkSamplerAddressMode u );
    SamplerCreation&                set_address_mode_uv( VkSamplerAddressMode u, VkSamplerAddressMode v );
    SamplerCreation&                set_address_mode_uvw( VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w );
    SamplerCreation&                set_name( const char* name );

}; // struct SamplerCreation

//
//
struct ShaderStage {

    const char*                     code        = nullptr;
    u32                             code_size   = 0;
    VkShaderStageFlagBits           type        = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;

}; // struct ShaderStage

//
//
struct ShaderStateCreation {

    ShaderStage                     stages[ k_max_shader_stages ];

    const char*                     name            = nullptr;

    u32                             stages_count    = 0;
    u32                             spv_input       = 0;

    // Building helpers
    ShaderStateCreation&            reset();
    ShaderStateCreation&            set_name( const char* name );
    ShaderStateCreation&            add_stage( const char* code, sizet code_size, VkShaderStageFlagBits type );
    ShaderStateCreation&            set_spv_input( bool value );

}; // struct ShaderStateCreation

//
//
struct DescriptorSetLayoutCreation {

    //
    // A single descriptor binding. It can be relative to one or more resources of the same type.
    //
    struct Binding {

        VkDescriptorType            type    = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        u16                         index   = 0;
        u16                         count   = 0;
        cstring                     name    = nullptr;  // Comes from external memory.
    }; // struct Binding

    Binding                         bindings[ k_max_descriptors_per_set ];
    u32                             num_bindings    = 0;
    u32                             set_index       = 0;
    bool                            bindless        = false;
    bool                            dynamic         = false;

    cstring                         name            = nullptr;

    // Building helpers
    DescriptorSetLayoutCreation&    reset();
    DescriptorSetLayoutCreation&    add_binding( const Binding& binding );
    DescriptorSetLayoutCreation&    add_binding( VkDescriptorType type, u32 index, u32 count, cstring name );
    DescriptorSetLayoutCreation&    add_binding_at_index( const Binding& binding, int index );
    DescriptorSetLayoutCreation&    set_name( cstring name );
    DescriptorSetLayoutCreation&    set_set_index( u32 index );

}; // struct DescriptorSetLayoutCreation

//
//
struct DescriptorSetCreation {

    ResourceHandle                  resources[ k_max_descriptors_per_set ];
    SamplerHandle                   samplers[ k_max_descriptors_per_set ];
    u16                             bindings[ k_max_descriptors_per_set ];

    DescriptorSetLayoutHandle       layout;
    u32                             num_resources   = 0;

    cstring                         name            = nullptr;

    // Building helpers
    DescriptorSetCreation&          reset();
    DescriptorSetCreation&          set_layout( DescriptorSetLayoutHandle layout );
    DescriptorSetCreation&          texture( TextureHandle texture, u16 binding );
    DescriptorSetCreation&          buffer( BufferHandle buffer, u16 binding );
    DescriptorSetCreation&          texture_sampler( TextureHandle texture, SamplerHandle sampler, u16 binding );   // TODO: separate samplers from textures
    DescriptorSetCreation&          set_name( cstring name );

}; // struct DescriptorSetCreation

//
//
struct DescriptorSetUpdate {
    DescriptorSetHandle             descriptor_set;

    u32                             frame_issued = 0;
}; // DescriptorSetUpdate

//
//
struct VertexAttribute {

    u16                             location = 0;
    u16                             binding = 0;
    u32                             offset = 0;
    VertexComponentFormat::Enum     format = VertexComponentFormat::Count;

}; // struct VertexAttribute

//
//
struct VertexStream {

    u16                             binding = 0;
    u16                             stride = 0;
    VertexInputRate::Enum           input_rate = VertexInputRate::Count;

}; // struct VertexStream

//
//
struct VertexInputCreation {

    u32                             num_vertex_streams = 0;
    u32                             num_vertex_attributes = 0;

    VertexStream                    vertex_streams[ k_max_vertex_streams ];
    VertexAttribute                 vertex_attributes[ k_max_vertex_attributes ];

    VertexInputCreation&            reset();
    VertexInputCreation&            add_vertex_stream( const VertexStream& stream );
    VertexInputCreation&            add_vertex_attribute( const VertexAttribute& attribute );
}; // struct VertexInputCreation

//
//
struct RenderPassOutput {

    VkFormat                        color_formats[ k_max_image_outputs ];
    VkImageLayout                   color_final_layouts[ k_max_image_outputs ];
    RenderPassOperation::Enum       color_operations[ k_max_image_outputs ];

    VkFormat                        depth_stencil_format;
    VkImageLayout                   depth_stencil_final_layout;

    u32                             num_color_formats;

    RenderPassOperation::Enum       depth_operation         = RenderPassOperation::DontCare;
    RenderPassOperation::Enum       stencil_operation       = RenderPassOperation::DontCare;

    RenderPassOutput&               reset();
    RenderPassOutput&               color( VkFormat format, VkImageLayout layout, RenderPassOperation::Enum load_op );
    RenderPassOutput&               depth( VkFormat format, VkImageLayout layout );
    RenderPassOutput&               set_depth_stencil_operations( RenderPassOperation::Enum depth, RenderPassOperation::Enum stencil );

}; // struct RenderPassOutput

//
//
struct RenderPassCreation {

    u16                             num_render_targets  = 0;

    VkFormat                        color_formats[ k_max_image_outputs ];
    VkImageLayout                   color_final_layouts[ k_max_image_outputs ];
    RenderPassOperation::Enum       color_operations[ k_max_image_outputs ];

    VkFormat                        depth_stencil_format = VK_FORMAT_UNDEFINED;
    VkImageLayout                   depth_stencil_final_layout;

    RenderPassOperation::Enum       depth_operation         = RenderPassOperation::DontCare;
    RenderPassOperation::Enum       stencil_operation       = RenderPassOperation::DontCare;

    const char*                     name                = nullptr;

    RenderPassCreation&             reset();
    RenderPassCreation&             add_attachment( VkFormat format, VkImageLayout layout, RenderPassOperation::Enum load_op );
    RenderPassCreation&             set_depth_stencil_texture( VkFormat format, VkImageLayout layout );
    RenderPassCreation&             set_name( const char* name );
    RenderPassCreation&             set_depth_stencil_operations( RenderPassOperation::Enum depth, RenderPassOperation::Enum stencil );

}; // struct RenderPassCreation

//
//
struct FramebufferCreation {

    RenderPassHandle                render_pass;

    u16                             num_render_targets  = 0;

    TextureHandle                   output_textures[ k_max_image_outputs ];
    TextureHandle                   depth_stencil_texture = { k_invalid_index };

    u16                             width       = 0;
    u16                             height      = 0;

    f32                             scale_x             = 1.f;
    f32                             scale_y             = 1.f;
    u8                              resize              = 1;

    const char*                     name                = nullptr;

    FramebufferCreation&            reset();
    FramebufferCreation&            add_render_texture( TextureHandle texture );
    FramebufferCreation&            set_depth_stencil_texture( TextureHandle texture );
    FramebufferCreation&            set_scaling( f32 scale_x, f32 scale_y, u8 resize );
    FramebufferCreation&            set_name( const char* name );

}; // struct RenderPassCreation

//
//
struct PipelineCreation {

    RasterizationCreation           rasterization;
    DepthStencilCreation            depth_stencil;
    BlendStateCreation              blend_state;
    VertexInputCreation             vertex_input;
    ShaderStateCreation             shaders;

    VkPrimitiveTopology             topology;

    RenderPassOutput                render_pass;
    DescriptorSetLayoutHandle       descriptor_set_layout[ k_max_descriptor_set_layouts ];
    const ViewportState*            viewport            = nullptr;

    u32                             num_active_layouts  = 0;

    const char*                     name                = nullptr;

    PipelineCreation&               add_descriptor_set_layout( DescriptorSetLayoutHandle handle );
    RenderPassOutput&               render_pass_output();

}; // struct PipelineCreation

// API-agnostic structs /////////////////////////////////////////////////////////

//
// Helper methods for texture formats
//
namespace TextureFormat {

    inline bool                     is_depth_stencil( VkFormat value ) {
        return value == VK_FORMAT_D16_UNORM_S8_UINT || value == VK_FORMAT_D24_UNORM_S8_UINT || value == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }
    inline bool                     is_depth_only( VkFormat value ) {
        return value >= VK_FORMAT_D16_UNORM && value < VK_FORMAT_D32_SFLOAT;
    }
    inline bool                     is_stencil_only( VkFormat value ) {
        return value == VK_FORMAT_S8_UINT;
    }

    inline bool                     has_depth( VkFormat value ) {
        return (value >= VK_FORMAT_D16_UNORM && value < VK_FORMAT_S8_UINT ) || (value >= VK_FORMAT_D16_UNORM_S8_UINT && value <= VK_FORMAT_D32_SFLOAT_S8_UINT);
    }
    inline bool                     has_stencil( VkFormat value ) {
        return value >= VK_FORMAT_S8_UINT && value <= VK_FORMAT_D32_SFLOAT_S8_UINT;
    }
    inline bool                     has_depth_or_stencil( VkFormat value ) {
        return value >= VK_FORMAT_D16_UNORM && value <= VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

} // namespace TextureFormat


//
//
struct DescriptorData {

    void*                           data    = nullptr;

}; // struct DescriptorData

//
//
struct DescriptorBinding {

    VkDescriptorType                type;
    u16                             index   = 0;
    u16                             count   = 0;
    u16                             set     = 0;

    const char*                     name    = nullptr;
}; // struct DescriptorBinding



// Resources descriptions /////////////////////////////////////////////////

//
//
struct ShaderStateDescription {

    void*                           native_handle = nullptr;
    cstring                         name        = nullptr;

}; // struct ShaderStateDescription

//
//
struct BufferDescription {

    void*                           native_handle = nullptr;
    cstring                         name        = nullptr;

    VkBufferUsageFlags              type_flags  = 0;
    ResourceUsageType::Enum         usage       = ResourceUsageType::Immutable;
    u32                             size        = 0;
    BufferHandle                    parent_handle;

}; // struct BufferDescription

//
//
struct TextureDescription {

    void*                           native_handle = nullptr;
    cstring                         name        = nullptr;

    u16                             width       = 1;
    u16                             height      = 1;
    u16                             depth       = 1;
    u8                              mipmaps     = 1;
    u8                              render_target = 0;
    u8                              compute_access = 0;

    VkFormat                        format = VK_FORMAT_UNDEFINED;
    TextureType::Enum               type = TextureType::Texture2D;

}; // struct Texture

//
//
struct SamplerDescription {

    cstring                         name        = nullptr;

    VkFilter                        min_filter = VK_FILTER_NEAREST;
    VkFilter                        mag_filter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode             mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSamplerAddressMode            address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode            address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode            address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;

}; // struct SamplerDescription

//
//
struct DescriptorSetLayoutDescription {

    DescriptorBinding*              bindings    = nullptr;
    u32                             num_active_bindings = 0;

}; // struct DescriptorSetLayoutDescription

//
//
struct DesciptorSetDescription {

    DescriptorData*                 resources   = nullptr;
    u32                             num_active_resources = 0;

}; // struct DesciptorSetDescription

//
//
struct PipelineDescription {

    ShaderStateHandle               shader;

}; // struct PipelineDescription


// API-agnostic resource modifications //////////////////////////////////////////

struct MapBufferParameters {
    BufferHandle                    buffer;
    u32                             offset = 0;
    u32                             size = 0;

}; // struct MapBufferParameters

// Synchronization //////////////////////////////////////////////////////////////

//
//
struct ImageBarrier {

    TextureHandle                   texture;

}; // struct ImageBarrier

//
//
struct MemoryBarrier {

    BufferHandle                    buffer;

}; // struct MemoryBarrier

//
//
struct ExecutionBarrier {

    PipelineStage::Enum             source_pipeline_stage;
    PipelineStage::Enum             destination_pipeline_stage;

    u32                             new_barrier_experimental  = u32_max;
    u32                             load_operation = 0;

    u32                             num_image_barriers;
    u32                             num_memory_barriers;

    ImageBarrier                    image_barriers[ 8 ];
    MemoryBarrier                   memory_barriers[ 8 ];

    ExecutionBarrier&               reset();
    ExecutionBarrier&               set( PipelineStage::Enum source, PipelineStage::Enum destination );
    ExecutionBarrier&               add_image_barrier( const ImageBarrier& image_barrier );
    ExecutionBarrier&               add_memory_barrier( const MemoryBarrier& memory_barrier );

}; // struct Barrier

//
//
struct ResourceUpdate {

    ResourceUpdateType::Enum        type;
    ResourceHandle                  handle;
    u32                             current_frame;
    u32                             deleting;
}; // struct ResourceUpdate

// Resources /////////////////////////////////////////////////////////////

static const u32                    k_max_swapchain_images = 3;
static const u32                    k_max_frames           = 1;

//
//
struct Buffer {

    VkBuffer                        vk_buffer;
    VmaAllocation                   vma_allocation;
    VkDeviceMemory                  vk_device_memory;
    VkDeviceSize                    vk_device_size;

    VkBufferUsageFlags              type_flags      = 0;
    ResourceUsageType::Enum         usage           = ResourceUsageType::Immutable;
    u32                             size            = 0;
    u32                             global_offset   = 0;    // Offset into global constant, if dynamic

    BufferHandle                    handle;
    BufferHandle                    parent_buffer;

    bool                            ready           = true;

    u8*                             mapped_data     = nullptr;
    const char*                     name            = nullptr;

}; // struct Buffer


//
//
struct Sampler {

    VkSampler                       vk_sampler;

    VkFilter                        min_filter = VK_FILTER_NEAREST;
    VkFilter                        mag_filter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode             mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSamplerAddressMode            address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode            address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode            address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    const char*                     name    = nullptr;

}; // struct Sampler

//
//
struct Texture {

    VkImage                         vk_image;
    VkImageView                     vk_image_view;
    VkFormat                        vk_format;
    VmaAllocation                   vma_allocation;
    ResourceState                   state = RESOURCE_STATE_UNDEFINED;

    u16                             width = 1;
    u16                             height = 1;
    u16                             depth = 1;
    u8                              mipmaps = 1;
    u8                              flags = 0;

    TextureHandle                   handle;
    TextureType::Enum               type    = TextureType::Texture2D;

    Sampler*                        sampler = nullptr;

    const char*                     name    = nullptr;
}; // struct Texture

//
//
struct ShaderState {

    VkPipelineShaderStageCreateInfo shader_stage_info[ k_max_shader_stages ];

    const char*                     name = nullptr;

    u32                             active_shaders = 0;
    bool                            graphics_pipeline = false;

    spirv::ParseResult*             parse_result;
}; // struct ShaderState

//
//
struct DescriptorSetLayout {

    VkDescriptorSetLayout           vk_descriptor_set_layout;

    VkDescriptorSetLayoutBinding*   vk_binding      = nullptr;
    DescriptorBinding*              bindings        = nullptr;
    u8*                             index_to_binding = nullptr; // Mapping between binding point and binding data.
    u16                             num_bindings    = 0;
    u16                             set_index       = 0;
    u8                              bindless        = 0;
    u8                              dynamic         = 0;

    DescriptorSetLayoutHandle       handle;

}; // struct DesciptorSetLayout

//
//
struct DescriptorSet {

    VkDescriptorSet                 vk_descriptor_set;

    ResourceHandle*                 resources       = nullptr;
    SamplerHandle*                  samplers        = nullptr;
    u16*                            bindings        = nullptr;

    const DescriptorSetLayout*      layout          = nullptr;
    u32                             num_resources   = 0;
}; // struct DesciptorSet


//
//
struct Pipeline {

    VkPipeline                      vk_pipeline;
    VkPipelineLayout                vk_pipeline_layout;

    VkPipelineBindPoint             vk_bind_point;

    ShaderStateHandle               shader_state;

    const DescriptorSetLayout*      descriptor_set_layout[ k_max_descriptor_set_layouts ];
    DescriptorSetLayoutHandle       descriptor_set_layout_handles[ k_max_descriptor_set_layouts ];
    u32                             num_active_layouts = 0;

    DepthStencilCreation            depth_stencil;
    BlendStateCreation              blend_state;
    RasterizationCreation           rasterization;

    PipelineHandle                  handle;
    bool                            graphics_pipeline = true;

}; // struct Pipeline


//
//
struct RenderPass {

    // NOTE(marco): this will be a null handle if dynamic rendering is available
    VkRenderPass                    vk_render_pass;

    RenderPassOutput                output;

    u16                             dispatch_x  = 0;
    u16                             dispatch_y  = 0;
    u16                             dispatch_z  = 0;

    u8                              num_render_targets = 0;

    const char*                     name        = nullptr;
}; // struct RenderPass

//
//
struct Framebuffer {

    // NOTE(marco): this will be a null handle if dynamic rendering is available
    VkFramebuffer                   vk_framebuffer;

    // NOTE(marco): cache render pass handle
    RenderPassHandle                render_pass;

    u16                             width       = 0;
    u16                             height      = 0;

    f32                             scale_x     = 1.f;
    f32                             scale_y     = 1.f;

    TextureHandle                   color_attachments[ k_max_image_outputs ];
    TextureHandle                   depth_stencil_attachment;
    u32                             num_color_attachments;

    u8                              resize      = 0;

    const char*                     name        = nullptr;
}; // struct Framebuffer


//
//
struct ComputeLocalSize {

    u32                             x : 10;
    u32                             y : 10;
    u32                             z : 10;
    u32                             pad : 2;
}; // struct ComputeLocalSize

// Enum translations. Use tables or switches depending on the case. ///////
cstring                     to_compiler_extension( VkShaderStageFlagBits value );
cstring                     to_stage_defines( VkShaderStageFlagBits value );

VkImageType                 to_vk_image_type( TextureType::Enum type );
VkImageViewType             to_vk_image_view_type( TextureType::Enum type );

VkFormat                    to_vk_vertex_format( VertexComponentFormat::Enum value );

VkPipelineStageFlags        to_vk_pipeline_stage( PipelineStage::Enum value );

//
//
VkAccessFlags               util_to_vk_access_flags( ResourceState state );
VkAccessFlags               util_to_vk_access_flags2( ResourceState state );

VkImageLayout               util_to_vk_image_layout( ResourceState usage );
VkImageLayout               util_to_vk_image_layout2( ResourceState usage );

// Determines pipeline stages involved for given accesses
VkPipelineStageFlags        util_determine_pipeline_stage_flags( VkAccessFlags access_flags, QueueType::Enum queue_type );
VkPipelineStageFlags2KHR    util_determine_pipeline_stage_flags2( VkAccessFlags2KHR access_flags, QueueType::Enum queue_type );

void util_add_image_barrier( GpuDevice* gpu, VkCommandBuffer command_buffer, Texture* texture, ResourceState new_state,
                             u32 base_mip_level, u32 mip_count, bool is_depth );

void util_add_image_barrier( GpuDevice* gpu, VkCommandBuffer command_buffer, VkImage image, ResourceState old_state, ResourceState new_state,
                             u32 base_mip_level, u32 mip_count, bool is_depth );

void util_add_image_barrier_ext( GpuDevice* gpu, VkCommandBuffer command_buffer, VkImage image, ResourceState old_state, ResourceState new_state,
                                 u32 base_mip_level, u32 mip_count, bool is_depth, u32 source_family, u32 destination_family,
                                 QueueType::Enum source_queue_type, QueueType::Enum destination_queue_type );

void util_add_image_barrier_ext( GpuDevice* gpu, VkCommandBuffer command_buffer, Texture* texture, ResourceState new_state,
                                 u32 base_mip_level, u32 mip_count, bool is_depth, u32 source_family, u32 destination_family,
                                 QueueType::Enum source_queue_type, QueueType::Enum destination_queue_type );

void util_add_buffer_barrier_ext( GpuDevice* gpu, VkCommandBuffer command_buffer, VkBuffer buffer, ResourceState old_state, ResourceState new_state,
                                  u32 buffer_size, u32 source_family, u32 destination_family,
                                  QueueType::Enum source_queue_type, QueueType::Enum destination_queue_type );

VkFormat util_string_to_vk_format( cstring format );

} // namespace raptor
