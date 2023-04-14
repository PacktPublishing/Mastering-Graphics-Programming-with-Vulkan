#pragma once

#if (_MSC_VER)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#else
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#include <vulkan/vulkan.h>

#include "external/vk_mem_alloc.h"

#include "graphics/gpu_resources.hpp"

#include "foundation/data_structures.hpp"
#include "foundation/string.hpp"
#include "foundation/service.hpp"
#include "foundation/array.hpp"

namespace raptor {

struct Allocator;

// Forward-declarations //////////////////////////////////////////////////
struct CommandBuffer;
struct DeviceRenderFrame;
struct GPUTimestampManager;
struct GpuDevice;


//
//
struct GPUTimestamp {

    u32                             start;
    u32                             end;

    f64                             elapsed_ms;

    u16                             parent_index;
    u16                             depth;

    u32                             color;
    u32                             frame_index;

    const char*                     name;
}; // struct GPUTimestamp


struct GPUTimestampManager {

    void                            init( Allocator* allocator, u16 queries_per_frame, u16 max_frames );
    void                            shutdown();

    bool                            has_valid_queries() const;
    void                            reset();
    u32                             resolve( u32 current_frame, GPUTimestamp* timestamps_to_fill );    // Returns the total queries for this frame.

    u32                             push( u32 current_frame, const char* name );    // Returns the timestamp query index.
    u32                             pop( u32 current_frame );

    Allocator*                      allocator                   = nullptr;
    GPUTimestamp*                   timestamps                  = nullptr;
    u64*                            timestamps_data             = nullptr;

    u32                             queries_per_frame           = 0;
    u32                             current_query               = 0;
    u32                             parent_index                = 0;
    u32                             depth                       = 0;

    bool                            current_frame_resolved      = false;    // Used to query the GPU only once per frame if get_gpu_timestamps is called more than once per frame.

}; // struct GPUTimestampManager


//
//
struct DeviceCreation {

    Allocator*                      allocator       = nullptr;
    StackAllocator*                 temporary_allocator = nullptr;
    void*                           window          = nullptr; // Pointer to API-specific window: SDL_Window, GLFWWindow
    u16                             width           = 1;
    u16                             height          = 1;

    u16                             gpu_time_queries_per_frame = 32;
    bool                            enable_gpu_time_queries = false;
    bool                            debug           = false;

    DeviceCreation&                 set_window( u32 width, u32 height, void* handle );
    DeviceCreation&                 set_allocator( Allocator* allocator );
    DeviceCreation&                 set_linear_allocator( StackAllocator* allocator );

}; // struct DeviceCreation

//
//
struct GpuDevice : public Service {

    static GpuDevice*               instance();

    // Init/Terminate methods
    void                            init( const DeviceCreation& creation );
    void                            shutdown();

    // Creation/Destruction of resources /////////////////////////////////
    BufferHandle                    create_buffer( const BufferCreation& creation );
    TextureHandle                   create_texture( const TextureCreation& creation );
    PipelineHandle                  create_pipeline( const PipelineCreation& creation );
    SamplerHandle                   create_sampler( const SamplerCreation& creation );
    DescriptorSetLayoutHandle       create_descriptor_set_layout( const DescriptorSetLayoutCreation& creation );
    DescriptorSetHandle             create_descriptor_set( const DescriptorSetCreation& creation );
    RenderPassHandle                create_render_pass( const RenderPassCreation& creation );
    ShaderStateHandle               create_shader_state( const ShaderStateCreation& creation );

    void                            destroy_buffer( BufferHandle buffer );
    void                            destroy_texture( TextureHandle texture );
    void                            destroy_pipeline( PipelineHandle pipeline );
    void                            destroy_sampler( SamplerHandle sampler );
    void                            destroy_descriptor_set_layout( DescriptorSetLayoutHandle layout );
    void                            destroy_descriptor_set( DescriptorSetHandle set );
    void                            destroy_render_pass( RenderPassHandle render_pass );
    void                            destroy_shader_state( ShaderStateHandle shader );

    // Query Description /////////////////////////////////////////////////
    void                            query_buffer( BufferHandle buffer, BufferDescription& out_description );
    void                            query_texture( TextureHandle texture, TextureDescription& out_description );
    void                            query_pipeline( PipelineHandle pipeline, PipelineDescription& out_description );
    void                            query_sampler( SamplerHandle sampler, SamplerDescription& out_description );
    void                            query_descriptor_set_layout( DescriptorSetLayoutHandle layout, DescriptorSetLayoutDescription& out_description );
    void                            query_descriptor_set( DescriptorSetHandle set, DesciptorSetDescription& out_description );
    void                            query_shader_state( ShaderStateHandle shader, ShaderStateDescription& out_description );

    const RenderPassOutput&         get_render_pass_output( RenderPassHandle render_pass ) const;

    // Update/Reload resources ///////////////////////////////////////////
    void                            resize_output_textures( RenderPassHandle render_pass, u32 width, u32 height );

    void                            update_descriptor_set( DescriptorSetHandle set );

    // Misc //////////////////////////////////////////////////////////////
    void                            link_texture_sampler( TextureHandle texture, SamplerHandle sampler );   // TODO: for now specify a sampler for a texture or use the default one.

    void                            set_present_mode( PresentMode::Enum mode );

    void                            frame_counters_advance();

    bool                            get_family_queue( VkPhysicalDevice physical_device );

    VkShaderModuleCreateInfo        compile_shader( cstring code, u32 code_size, VkShaderStageFlagBits stage, cstring name );

    // Swapchain //////////////////////////////////////////////////////////
    void                            create_swapchain();
    void                            destroy_swapchain();
    void                            resize_swapchain();

    // Map/Unmap /////////////////////////////////////////////////////////
    void*                           map_buffer( const MapBufferParameters& parameters );
    void                            unmap_buffer( const MapBufferParameters& parameters );

    void*                           dynamic_allocate( u32 size );

    void                            set_buffer_global_offset( BufferHandle buffer, u32 offset );

    // Command Buffers ///////////////////////////////////////////////////
    CommandBuffer*                  get_command_buffer( QueueType::Enum type, bool begin );
    CommandBuffer*                  get_instant_command_buffer();

    void                            queue_command_buffer( CommandBuffer* command_buffer );          // Queue command buffer that will not be executed until present is called.

    // Rendering /////////////////////////////////////////////////////////
    void                            new_frame();
    void                            present();
    void                            resize( u16 width, u16 height );
    void                            set_presentation_mode( PresentMode::Enum mode );

    void                            fill_barrier( RenderPassHandle render_pass, ExecutionBarrier& out_barrier );

    BufferHandle                    get_fullscreen_vertex_buffer() const;           // Returns a vertex buffer usable for fullscreen shaders that uses no vertices.
    RenderPassHandle                get_swapchain_pass() const;                     // Returns what is considered the final pass that writes to the swapchain.

    TextureHandle                   get_dummy_texture() const;
    BufferHandle                    get_dummy_constant_buffer() const;
    const RenderPassOutput&         get_swapchain_output() const                    { return swapchain_output; }

    VkRenderPass                    get_vulkan_render_pass( const RenderPassOutput& output, cstring name );

    // Names and markers /////////////////////////////////////////////////
    void                            set_resource_name( VkObjectType object_type, uint64_t handle, const char* name );
    void                            push_marker( VkCommandBuffer command_buffer, cstring name );
    void                            pop_marker( VkCommandBuffer command_buffer );

    // GPU Timings ///////////////////////////////////////////////////////
    void                            set_gpu_timestamps_enable( bool value )         { timestamps_enabled = value; }

    u32                             get_gpu_timestamps( GPUTimestamp* out_timestamps );
    void                            push_gpu_timestamp( CommandBuffer* command_buffer, const char* name );
    void                            pop_gpu_timestamp( CommandBuffer* command_buffer );


    // Instant methods ///////////////////////////////////////////////////
    void                            destroy_buffer_instant( ResourceHandle buffer );
    void                            destroy_texture_instant( ResourceHandle texture );
    void                            destroy_pipeline_instant( ResourceHandle pipeline );
    void                            destroy_sampler_instant( ResourceHandle sampler );
    void                            destroy_descriptor_set_layout_instant( ResourceHandle layout );
    void                            destroy_descriptor_set_instant( ResourceHandle set );
    void                            destroy_render_pass_instant( ResourceHandle render_pass );
    void                            destroy_shader_state_instant( ResourceHandle shader );

    void                            update_descriptor_set_instant( const DescriptorSetUpdate& update );

    ResourcePool                    buffers;
    ResourcePool                    textures;
    ResourcePool                    pipelines;
    ResourcePool                    samplers;
    ResourcePool                    descriptor_set_layouts;
    ResourcePool                    descriptor_sets;
	ResourcePool                    render_passes;
    ResourcePool                    command_buffers;
    ResourcePool                    shaders;

    // Primitive resources
    BufferHandle                    fullscreen_vertex_buffer;
    RenderPassHandle                swapchain_pass;
    SamplerHandle                   default_sampler;
    // Dummy resources
    TextureHandle                   dummy_texture;
    BufferHandle                    dummy_constant_buffer;

    RenderPassOutput                swapchain_output;

    StringBuffer                    string_buffer;

    Allocator*                      allocator;
    StackAllocator*                 temporary_allocator;

    u32                             dynamic_max_per_frame_size;
    BufferHandle                    dynamic_buffer;
    u8*                             dynamic_mapped_memory;
    u32                             dynamic_allocated_size;
    u32                             dynamic_per_frame_size;

    CommandBuffer**                 queued_command_buffers              = nullptr;
    u32                             num_allocated_command_buffers       = 0;
    u32                             num_queued_command_buffers          = 0;

    PresentMode::Enum               present_mode                        = PresentMode::VSync;
    u32                             current_frame;
    u32                             previous_frame;

    u32                             absolute_frame;

    u16                             swapchain_width                     = 1;
    u16                             swapchain_height                    = 1;

    GPUTimestampManager*            gpu_timestamp_manager               = nullptr;

    bool                            bindless_supported                  = false;
    bool                            timestamps_enabled                  = false;
    bool                            resized                             = false;
    bool                            vertical_sync                       = false;

    static constexpr cstring        k_name                              = "raptor_gpu_service";


    VkAllocationCallbacks*          vulkan_allocation_callbacks;
    VkInstance                      vulkan_instance;
    VkPhysicalDevice                vulkan_physical_device;
    VkPhysicalDeviceProperties      vulkan_physical_properties;
    VkDevice                        vulkan_device;
    VkQueue                         vulkan_queue;
    uint32_t                        vulkan_queue_family;
    VkDescriptorPool                vulkan_descriptor_pool;

    // Swapchain
    VkImage                         vulkan_swapchain_images[ k_max_swapchain_images ];
    VkImageView                     vulkan_swapchain_image_views[ k_max_swapchain_images ];
    VkFramebuffer                   vulkan_swapchain_framebuffers[ k_max_swapchain_images ];

    VkQueryPool                     vulkan_timestamp_query_pool;
    // Per frame synchronization
    VkSemaphore                     vulkan_render_complete_semaphore[ k_max_swapchain_images ];
    VkSemaphore                     vulkan_image_acquired_semaphore;
    VkFence                         vulkan_command_buffer_executed_fence[ k_max_swapchain_images ];

    TextureHandle                   depth_texture;

    static const uint32_t           k_max_frames                    = 3;

    // Windows specific
    VkSurfaceKHR                    vulkan_window_surface;
    VkSurfaceFormatKHR              vulkan_surface_format;
    VkPresentModeKHR                vulkan_present_mode;
    VkSwapchainKHR                  vulkan_swapchain;
    u32                             vulkan_swapchain_image_count;

    VkDebugReportCallbackEXT        vulkan_debug_callback;
    VkDebugUtilsMessengerEXT        vulkan_debug_utils_messenger;

    u32                             vulkan_image_index;

    VmaAllocator                    vma_allocator;

    // These are dynamic - so that workload can be handled correctly.
    Array<ResourceUpdate>           resource_deletion_queue;
    Array<DescriptorSetUpdate>      descriptor_set_updates;

    f32                             gpu_timestamp_frequency;
    bool                            gpu_timestamp_reset             = true;
    bool                            debug_utils_extension_present   = false;

    char                            vulkan_binaries_path[ 512 ];


    ShaderState*              access_shader_state( ShaderStateHandle shader );
    const ShaderState*        access_shader_state( ShaderStateHandle shader ) const;

    Texture*                  access_texture( TextureHandle texture );
    const Texture*            access_texture( TextureHandle texture ) const;

    Buffer*                   access_buffer( BufferHandle buffer );
    const Buffer*             access_buffer( BufferHandle buffer ) const;

    Pipeline*                 access_pipeline( PipelineHandle pipeline );
    const Pipeline*           access_pipeline( PipelineHandle pipeline ) const;

    Sampler*                  access_sampler( SamplerHandle sampler );
    const Sampler*            access_sampler( SamplerHandle sampler ) const;

    DesciptorSetLayout*       access_descriptor_set_layout( DescriptorSetLayoutHandle layout );
    const DesciptorSetLayout* access_descriptor_set_layout( DescriptorSetLayoutHandle layout ) const;

    DesciptorSet*             access_descriptor_set( DescriptorSetHandle set );
    const DesciptorSet*       access_descriptor_set( DescriptorSetHandle set ) const;

    RenderPass*               access_render_pass( RenderPassHandle render_pass );
    const RenderPass*         access_render_pass( RenderPassHandle render_pass ) const;


}; // struct Device


} // namespace raptor
