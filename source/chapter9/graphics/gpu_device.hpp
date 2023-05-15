#pragma once

#if defined(_MSC_VER)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "foundation/windows_declarations.h"

#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan_win32.h>

#define VK_USE_PLATFORM_WIN32_KHR
#else
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#endif

VK_DEFINE_HANDLE( VmaAllocator )

#include "graphics/gpu_resources.hpp"

#include "foundation/data_structures.hpp"
#include "foundation/string.hpp"
#include "foundation/service.hpp"
#include "foundation/array.hpp"

namespace raptor {

struct Allocator;

// Forward-declarations //////////////////////////////////////////////////
struct CommandBuffer;
struct CommandBufferManager;
struct DeviceRenderFrame;
struct GPUTimeQueriesManager;
struct GpuDevice;
struct GPUTimeQuery;
struct GpuTimeQueryTree;
struct GpuPipelineStatistics;

//
struct GpuThreadFramePools {

    VkCommandPool                   vulkan_command_pool             = nullptr;
    VkQueryPool                     vulkan_timestamp_query_pool     = nullptr;
    VkQueryPool                     vulkan_pipeline_stats_query_pool = nullptr;

    GpuTimeQueryTree*               time_queries = nullptr;

}; // struct GpuThreadFramePools

//
//
struct GpuDescriptorPoolCreation {

    u16                             samplers        = 256;
    u16                             combined_image_samplers = 256;
    u16                             sampled_image = 256;
    u16                             storage_image = 256;
    u16                             uniform_texel_buffers = 256;
    u16                             storage_texel_buffers = 256;
    u16                             uniform_buffer = 256;
    u16                             storage_buffer = 256;
    u16                             uniform_buffer_dynamic = 256;
    u16                             storage_buffer_dynamic = 256;
    u16                             input_attachments = 256;

}; // struct GpuDescriptorPoolCreation

//
//
struct GpuResourcePoolCreation {

    u16                             buffers         = 256;
    u16                             textures        = 256;
    u16                             pipelines       = 256;
    u16                             samplers        = 256;
    u16                             descriptor_set_layouts = 256;
    u16                             descriptor_sets = 256;
    u16                             render_passes   = 256;
    u16                             framebuffers    = 256;
    u16                             command_buffers = 256;
    u16                             shaders         = 256;
    u16                             page_pools      = 64;
};

//
//
struct GpuDeviceCreation {

    GpuDescriptorPoolCreation       descriptor_pool_creation;
    GpuResourcePoolCreation         resource_pool_creation;

    Allocator*                      allocator       = nullptr;
    StackAllocator*                 temporary_allocator = nullptr;
    void*                           window          = nullptr; // Pointer to API-specific window: SDL_Window, GLFWWindow
    u16                             width           = 1;
    u16                             height          = 1;

    u16                             gpu_time_queries_per_frame  = 32;
    u16                             num_threads                 = 1;
    bool                            enable_gpu_time_queries     = false;
    bool                            enable_pipeline_statistics  = true;
    bool                            debug                       = false;
    bool                            force_disable_dynamic_rendering = false;

    GpuDeviceCreation&              set_window( u32 width, u32 height, void* handle );
    GpuDeviceCreation&              set_allocator( Allocator* allocator );
    GpuDeviceCreation&              set_linear_allocator( StackAllocator* allocator );
    GpuDeviceCreation&              set_num_threads( u32 value );

}; // struct GpuDeviceCreation

//
//
struct GpuDevice : public Service {

    // Helper methods
    static void                     fill_write_descriptor_sets( GpuDevice& gpu, const DescriptorSetLayout* descriptor_set_layout, VkDescriptorSet vk_descriptor_set,
                                                                VkWriteDescriptorSet* descriptor_write, VkDescriptorBufferInfo* buffer_info, VkDescriptorImageInfo* image_info,
                                                                VkSampler vk_default_sampler, u32& num_resources, const ResourceHandle* resources,
                                                                const SamplerHandle* samplers, const u16* bindings );

    // Init/Terminate methods
    void                            init( const GpuDeviceCreation& creation );
    void                            shutdown();

    // Creation/Destruction of resources /////////////////////////////////
    BufferHandle                    create_buffer( const BufferCreation& creation );
    TextureHandle                   create_texture( const TextureCreation& creation );
    TextureHandle                   create_texture_view( const TextureViewCreation& creation );
    PipelineHandle                  create_pipeline( const PipelineCreation& creation, const char* cache_path = nullptr );
    SamplerHandle                   create_sampler( const SamplerCreation& creation );
    DescriptorSetLayoutHandle       create_descriptor_set_layout( const DescriptorSetLayoutCreation& creation );
    DescriptorSetHandle             create_descriptor_set( const DescriptorSetCreation& creation );
    RenderPassHandle                create_render_pass( const RenderPassCreation& creation );
    FramebufferHandle               create_framebuffer( const FramebufferCreation& creation );
    ShaderStateHandle               create_shader_state( const ShaderStateCreation& creation );

    void                            destroy_buffer( BufferHandle buffer );
    void                            destroy_texture( TextureHandle texture );
    void                            destroy_pipeline( PipelineHandle pipeline );
    void                            destroy_sampler( SamplerHandle sampler );
    void                            destroy_descriptor_set_layout( DescriptorSetLayoutHandle layout );
    void                            destroy_descriptor_set( DescriptorSetHandle set );
    void                            destroy_render_pass( RenderPassHandle render_pass );
    void                            destroy_framebuffer( FramebufferHandle framebuffer );
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
    void                            resize_output_textures( FramebufferHandle render_pass, u32 width, u32 height );
    void                            resize_texture( TextureHandle texture, u32 width, u32 height );

    PagePoolHandle                  allocate_texture_pool( TextureHandle texture_handle, u32 pool_size );
    void                            destroy_page_pool( PagePoolHandle pool_handle );

    void                            reset_pool( PagePoolHandle pool_handle );
    void                            bind_texture_pages( PagePoolHandle pool_handle, TextureHandle handle, u32 x, u32 y, u32 width, u32 height, u32 layer );

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
    CommandBuffer*                  get_command_buffer( u32 thread_index, u32 frame_index, bool begin );
    CommandBuffer*                  get_secondary_command_buffer( u32 thread_index, u32 frame_index );

    void                            queue_command_buffer( CommandBuffer* command_buffer );          // Queue command buffer that will not be executed until present is called.

    // Rendering /////////////////////////////////////////////////////////
    void                            new_frame();
    void                            present( CommandBuffer* async_compute_command_buffer );
    void                            resize( u16 width, u16 height );

    void                            fill_barrier( FramebufferHandle render_pass, ExecutionBarrier& out_barrier );

    bool                            buffer_ready( BufferHandle buffer );

    BufferHandle                    get_fullscreen_vertex_buffer() const;           // Returns a vertex buffer usable for fullscreen shaders that uses no vertices.
    RenderPassHandle                get_swapchain_pass() const;                     // Returns what is considered the final pass that writes to the swapchain.
    FramebufferHandle               get_current_framebuffer() const;                // Returns the framebuffer for the active swapchain image

    TextureHandle                   get_dummy_texture() const;
    BufferHandle                    get_dummy_constant_buffer() const;
    const RenderPassOutput&         get_swapchain_output() const                    { return swapchain_output; }

    VkRenderPass                    get_vulkan_render_pass( const RenderPassOutput& output, cstring name );

    // Compute ///////////////////////////////////////////////////////////
    void                            submit_compute_load( CommandBuffer* command_buffer );

    // Names and markers /////////////////////////////////////////////////
    void                            set_resource_name( VkObjectType object_type, u64 handle, const char* name );
    void                            push_marker( VkCommandBuffer command_buffer, cstring name );
    void                            pop_marker( VkCommandBuffer command_buffer );

    // GPU Timings ///////////////////////////////////////////////////////
    void                            set_gpu_timestamps_enable( bool value )         { timestamps_enabled = value; }

    u32                             copy_gpu_timestamps( GPUTimeQuery* out_timestamps );


    // Instant methods ///////////////////////////////////////////////////
    void                            destroy_buffer_instant( ResourceHandle buffer );
    void                            destroy_texture_instant( ResourceHandle texture );
    void                            destroy_pipeline_instant( ResourceHandle pipeline );
    void                            destroy_sampler_instant( ResourceHandle sampler );
    void                            destroy_descriptor_set_layout_instant( ResourceHandle layout );
    void                            destroy_descriptor_set_instant( ResourceHandle set );
    void                            destroy_render_pass_instant( ResourceHandle render_pass );
    void                            destroy_framebuffer_instant( ResourceHandle framebuffer );
    void                            destroy_shader_state_instant( ResourceHandle shader );
    void                            destroy_page_pool_instant( ResourceHandle handle );

    void                            update_descriptor_set_instant( const DescriptorSetUpdate& update );

    // Memory Statistics //////////////////////////////////////////////////
    cstring                         get_gpu_name() const                { return vulkan_physical_properties.deviceName; }
    u32                             get_memory_heap_count();

    ResourcePool                    buffers;
    ResourcePool                    textures;
    ResourcePool                    pipelines;
    ResourcePool                    samplers;
    ResourcePool                    descriptor_set_layouts;
    ResourcePool                    descriptor_sets;
	ResourcePool                    render_passes;
    ResourcePool                    framebuffers;
    ResourcePool                    shaders;
    ResourcePool                    page_pools;

    // Primitive resources
    BufferHandle                    fullscreen_vertex_buffer;
    RenderPassHandle                swapchain_render_pass{ k_invalid_index };
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

    u64                             absolute_frame;

    u16                             swapchain_width                     = 1;
    u16                             swapchain_height                    = 1;

    GPUTimeQueriesManager*          gpu_time_queries_manager            = nullptr;

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
    VkQueue                         vulkan_main_queue;
    VkQueue                         vulkan_compute_queue;
    VkQueue                         vulkan_transfer_queue;
    u32                             vulkan_main_queue_family;
    u32                             vulkan_compute_queue_family;
    u32                             vulkan_transfer_queue_family;
    VkDescriptorPool                vulkan_descriptor_pool;

    // [TAG: BINDLESS]
    VkDescriptorPool                vulkan_bindless_descriptor_pool;
    VkDescriptorSet                 vulkan_bindless_descriptor_set_cached;  // Cached but will be removed with its associated DescriptorSet.
    DescriptorSetLayoutHandle       bindless_descriptor_set_layout;
    DescriptorSetHandle             bindless_descriptor_set;

    // Swapchain
    FramebufferHandle               vulkan_swapchain_framebuffers[ k_max_swapchain_images ]{ k_invalid_index, k_invalid_index, k_invalid_index };

    Array<GpuThreadFramePools>      thread_frame_pools;

    // Per frame synchronization
    VkSemaphore                     vulkan_render_complete_semaphore[ k_max_frames ];
    VkSemaphore                     vulkan_image_acquired_semaphore;
    VkSemaphore                     vulkan_graphics_semaphore;
    VkFence                         vulkan_command_buffer_executed_fence[ k_max_frames ];

    VkSemaphore                     vulkan_bind_semaphore;

    VkSemaphore                     vulkan_compute_semaphore;
    VkFence                         vulkan_compute_fence;
    u64                             last_compute_semaphore_value = 0;
    bool                            has_async_work = false;

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

    // Extension functions
    PFN_vkCmdBeginRenderingKHR      vkCmdBeginRenderingKHR;
    PFN_vkCmdEndRenderingKHR        vkCmdEndRenderingKHR;
    PFN_vkQueueSubmit2KHR           vkQueueSubmit2KHR;
    PFN_vkCmdPipelineBarrier2KHR    vkCmdPipelineBarrier2KHR;

    // Mesh shaders functions
    PFN_vkCmdDrawMeshTasksNV        vkCmdDrawMeshTasksNV;
    PFN_vkCmdDrawMeshTasksIndirectCountNV vkCmdDrawMeshTasksIndirectCountNV;
    PFN_vkCmdDrawMeshTasksIndirectNV vkCmdDrawMeshTasksIndirectNV;

    // Variable rate shading functions
    PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR vkGetPhysicalDeviceFragmentShadingRatesKHR;
    PFN_vkCmdSetFragmentShadingRateKHR vkCmdSetFragmentShadingRateKHR;

    Array<VkPhysicalDeviceFragmentShadingRateKHR> fragment_shading_rates;

    // These are dynamic - so that workload can be handled correctly.
    Array<ResourceUpdate>           resource_deletion_queue;
    Array<DescriptorSetUpdate>      descriptor_set_updates;
    // [TAG: BINDLESS]
    Array<ResourceUpdate>           texture_to_update_bindless;

    Array<SparseMemoryBindInfo>     pending_sparse_memory_info;
    Array<VkSparseImageMemoryBind>  pending_sparse_queue_binds;

    u32                             num_threads = 1;
    f32                             gpu_timestamp_frequency;
    bool                            debug_utils_extension_present   = false;
    bool                            dynamic_rendering_extension_present     = false;
    bool                            timeline_semaphore_extension_present    = false;
    bool                            synchronization2_extension_present      = false;
    bool                            mesh_shaders_extension_present  = false;
    bool                            multiview_extension_present     = false;
    bool                            fragment_shading_rate_present   = false;

    sizet                           ubo_alignment                   = 256;
    sizet                           ssbo_alignemnt                  = 256;
    u32                             subgroup_size                   = 32;
    u32                             max_framebuffer_layers          = 1;
    VkExtent2D                      min_fragment_shading_rate_texel_size;

    char                            vulkan_binaries_path[ 512 ];


    ShaderState*                    access_shader_state( ShaderStateHandle shader );
    const ShaderState*              access_shader_state( ShaderStateHandle shader ) const;

    Texture*                        access_texture( TextureHandle texture );
    const Texture*                  access_texture( TextureHandle texture ) const;

    Buffer*                         access_buffer( BufferHandle buffer );
    const Buffer*                   access_buffer( BufferHandle buffer ) const;

    Pipeline*                       access_pipeline( PipelineHandle pipeline );
    const Pipeline*                 access_pipeline( PipelineHandle pipeline ) const;

    Sampler*                        access_sampler( SamplerHandle sampler );
    const Sampler*                  access_sampler( SamplerHandle sampler ) const;

    DescriptorSetLayout*            access_descriptor_set_layout( DescriptorSetLayoutHandle layout );
    const DescriptorSetLayout*      access_descriptor_set_layout( DescriptorSetLayoutHandle layout ) const;

    DescriptorSetLayoutHandle       get_descriptor_set_layout( PipelineHandle pipeline_handle, int layout_index );
    DescriptorSetLayoutHandle       get_descriptor_set_layout( PipelineHandle pipeline_handle, int layout_index ) const;

    DescriptorSet*                  access_descriptor_set( DescriptorSetHandle set );
    const DescriptorSet*            access_descriptor_set( DescriptorSetHandle set ) const;

    RenderPass*                     access_render_pass( RenderPassHandle render_pass );
    const RenderPass*               access_render_pass( RenderPassHandle render_pass ) const;

    Framebuffer*                    access_framebuffer( FramebufferHandle framebuffer );
    const Framebuffer*              access_framebuffer( FramebufferHandle framebuffer ) const;

    PagePool*                       access_page_pool( PagePoolHandle page_pool );
    const PagePool*                 access_page_pool( PagePoolHandle page_pool ) const;

}; // struct GpuDevice


} // namespace raptor
