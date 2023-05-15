#include "graphics/gpu_device.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/gpu_profiler.hpp"
#include "graphics/spirv_parser.hpp"

#include "foundation/memory.hpp"
#include "foundation/hash_map.hpp"
#include "foundation/process.hpp"
#include "foundation/file.hpp"

#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "external/vk_mem_alloc.h"

template<class T>
constexpr const T& raptor_min( const T& a, const T& b ) {
    return ( a < b ) ? a : b;
}

template<class T>
constexpr const T& raptor_max( const T& a, const T& b ) {
    return ( a < b ) ? b : a;
}

#define VMA_MAX raptor_max
#define VMA_MIN raptor_min
#define VMA_USE_STL_CONTAINERS 0
#define VMA_USE_STL_VECTOR 0
#define VMA_USE_STL_UNORDERED_MAP 0
#define VMA_USE_STL_LIST 0

#if defined (_MSC_VER)
#pragma warning (disable: 4127)
#pragma warning (disable: 4189)
#pragma warning (disable: 4191)
#pragma warning (disable: 4296)
#pragma warning (disable: 4324)
#pragma warning (disable: 4355)
#pragma warning (disable: 4365)
#pragma warning (disable: 4625)
#pragma warning (disable: 4626)
#pragma warning (disable: 4668)
#pragma warning (disable: 5026)
#pragma warning (disable: 5027)
#endif // _MSC_VER

//#define VMA_DEBUG_LOG rprintret

#define VMA_IMPLEMENTATION
#include "external/vk_mem_alloc.h"

// SDL and Vulkan headers
#include <SDL.h>
#include <SDL_vulkan.h>

namespace raptor {


static void                 check_result( VkResult result );
#define                     check( result ) RASSERTM( result == VK_SUCCESS, "Vulkan assert code %u", result )

// Device implementation //////////////////////////////////////////////////

// Methods //////////////////////////////////////////////////////////////////////

// Enable this to add debugging capabilities.
// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_debug_utils.html
#define VULKAN_DEBUG_REPORT

//#define VULKAN_SYNCHRONIZATION_VALIDATION

static const char* s_requested_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    // Platform specific extension
#ifdef VK_USE_PLATFORM_WIN32_KHR
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
        VK_MVK_MACOS_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XCB_KHR)
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XCB_KHR)
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_MIR_KHR || VK_USE_PLATFORM_DISPLAY_KHR)
        VK_KHR_DISPLAY_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_IOS_MVK)
        VK_MVK_IOS_SURFACE_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_WIN32_KHR

#if defined (VULKAN_DEBUG_REPORT)
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif // VULKAN_DEBUG_REPORT
};

static const char* s_requested_layers[] = {
#if defined (VULKAN_DEBUG_REPORT)
    "VK_LAYER_KHRONOS_validation",
    //"VK_LAYER_AMD_switchable_graphics",
    //"VK_LAYER_NV_optimus",
    //"VK_LAYER_LUNARG_core_validation",
    //"VK_LAYER_LUNARG_image",
    //"VK_LAYER_LUNARG_parameter_validation",
    //"VK_LAYER_LUNARG_object_tracker"
#else
    "",
#endif // VULKAN_DEBUG_REPORT
};

#ifdef VULKAN_DEBUG_REPORT

static VkBool32 debug_utils_callback( VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                            VkDebugUtilsMessageTypeFlagsEXT types,
                                                            const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                                            void* user_data ) {
    bool triggerBreak = severity & ( VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT );
    rprint( " MessageID: %s %i\nMessage: %s\n\n", callback_data->pMessageIdName, callback_data->messageIdNumber, callback_data->pMessage );


    if ( triggerBreak ) {
       // __debugbreak();
    }

    return VK_FALSE;
}

// GPU Timestamps ///////////////////////////////////////////////////

VkDebugUtilsMessengerCreateInfoEXT create_debug_utils_messenger_info() {
    VkDebugUtilsMessengerCreateInfoEXT creation_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    creation_info.pfnUserCallback = debug_utils_callback;
    creation_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    creation_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;

    return creation_info;
}

#endif // VULKAN_DEBUG_REPORT

static SDL_Window*                  sdl_window;

PFN_vkSetDebugUtilsObjectNameEXT    pfnSetDebugUtilsObjectNameEXT;
PFN_vkCmdBeginDebugUtilsLabelEXT    pfnCmdBeginDebugUtilsLabelEXT;
PFN_vkCmdEndDebugUtilsLabelEXT      pfnCmdEndDebugUtilsLabelEXT;

static raptor::FlatHashMap<u64, VkRenderPass> render_pass_cache;
static CommandBufferManager command_buffer_ring;

static const u32        k_bindless_texture_binding = 10;
static const u32        k_bindless_image_binding = 11;
static const u32        k_max_bindless_resources = 1024;

bool GpuDevice::get_family_queue( VkPhysicalDevice physical_device ) {
    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr );

    VkQueueFamilyProperties* queue_families = ( VkQueueFamilyProperties* )ralloca( sizeof( VkQueueFamilyProperties ) * queue_family_count, allocator );
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families );

    u32 family_index = 0;
    VkBool32 surface_supported;
    for ( ; family_index < queue_family_count; ++family_index ) {
        VkQueueFamilyProperties queue_family = queue_families[ family_index ];
        if ( queue_family.queueCount > 0 && queue_family.queueFlags & ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ) ) {
            vkGetPhysicalDeviceSurfaceSupportKHR( physical_device, family_index, vulkan_window_surface, &surface_supported);

            if ( surface_supported ) {
                vulkan_main_queue_family = family_index;
                break;
            }
        }
    }

    rfree( queue_families, allocator );

    return surface_supported;
}

void GpuDevice::init( const GpuDeviceCreation& creation ) {

    rprint( "Gpu Device init\n" );
    // 1. Perform common code
    allocator = creation.allocator;
    temporary_allocator = creation.temporary_allocator;

    string_buffer.init( 1024 * 1024, creation.allocator );

    //////// Init Vulkan instance.
    VkResult result;
    vulkan_allocation_callbacks = nullptr;

    VkApplicationInfo application_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Raptor Graphics Device", 1, "Raptor", 1, VK_MAKE_VERSION( 1, 2, 0 ) };

    VkInstanceCreateInfo create_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &application_info,
#if defined(VULKAN_DEBUG_REPORT)
                                         ArraySize( s_requested_layers ), s_requested_layers,
#else
                                         0, nullptr,
#endif
                                         ArraySize( s_requested_extensions ), s_requested_extensions };
#if defined(VULKAN_DEBUG_REPORT)
    const VkDebugUtilsMessengerCreateInfoEXT debug_create_info = create_debug_utils_messenger_info();

#if defined(VULKAN_SYNCHRONIZATION_VALIDATION)
    const VkValidationFeatureEnableEXT featuresRequested[] = { VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT, VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT/*, VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT*/ };
    VkValidationFeaturesEXT features = {};
    features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    features.pNext = &debug_create_info;
    features.enabledValidationFeatureCount = _countof( featuresRequested );
    features.pEnabledValidationFeatures = featuresRequested;
    create_info.pNext = &features;
#else
    create_info.pNext = &debug_create_info;
#endif // VULKAN_SYNCHRONIZATION_VALIDATION
#endif // VULKAN_DEBUG_REPORT

    //// Create Vulkan Instance
    result = vkCreateInstance( &create_info, vulkan_allocation_callbacks, &vulkan_instance );
    check( result );

    swapchain_width = creation.width;
    swapchain_height = creation.height;

    StackAllocator* temp_allocator = creation.temporary_allocator;
    sizet initial_temp_allocator_marker = temp_allocator->get_marker();

    //// Choose extensions
#ifdef VULKAN_DEBUG_REPORT
    {
        u32 num_instance_extensions;
        vkEnumerateInstanceExtensionProperties( nullptr, &num_instance_extensions, nullptr );
        VkExtensionProperties* extensions = ( VkExtensionProperties* )ralloca( sizeof( VkExtensionProperties ) * num_instance_extensions, temp_allocator );
        vkEnumerateInstanceExtensionProperties( nullptr, &num_instance_extensions, extensions );
        for ( size_t i = 0; i < num_instance_extensions; i++ ) {

            if ( !strcmp( extensions[ i ].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) ) {
                debug_utils_extension_present = true;
                continue;
            }
        }

        if ( !debug_utils_extension_present ) {
            rprint( "Extension %s for debugging non present.", VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
        } else {
            // Create new debug utils callback
            PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = ( PFN_vkCreateDebugUtilsMessengerEXT )vkGetInstanceProcAddr( vulkan_instance, "vkCreateDebugUtilsMessengerEXT" );
            VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = create_debug_utils_messenger_info();

            vkCreateDebugUtilsMessengerEXT( vulkan_instance, &debug_messenger_create_info, vulkan_allocation_callbacks, &vulkan_debug_utils_messenger );
        }
    }
#endif

    //////// Choose physical device
    u32 num_physical_device;
    result = vkEnumeratePhysicalDevices( vulkan_instance, &num_physical_device, NULL );
    check( result );

    VkPhysicalDevice* gpus = ( VkPhysicalDevice* )ralloca( sizeof( VkPhysicalDevice ) * num_physical_device, temp_allocator );
    result = vkEnumeratePhysicalDevices( vulkan_instance, &num_physical_device, gpus );
    check( result );

    //////// Create drawable surface
    // Create surface
    SDL_Window* window = ( SDL_Window* )creation.window;
    if ( SDL_Vulkan_CreateSurface( window, vulkan_instance, &vulkan_window_surface ) == SDL_FALSE ) {
        rprint( "Failed to create Vulkan surface.\n" );
    }

    sdl_window = window;

    VkPhysicalDevice discrete_gpu = VK_NULL_HANDLE;
    VkPhysicalDevice integrated_gpu = VK_NULL_HANDLE;
    for ( u32 i = 0; i < num_physical_device; ++i ) {
        VkPhysicalDevice physical_device = gpus[ i ];
        vkGetPhysicalDeviceProperties( physical_device, &vulkan_physical_properties );

        if ( vulkan_physical_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
            if ( get_family_queue( physical_device ) ) {
                // NOTE(marco): prefer discrete GPU over integrated one, stop at first discrete GPU that has
                // present capabilities
                discrete_gpu = physical_device;
                break;
            }

            continue;
        }

        if ( vulkan_physical_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ) {
            if ( get_family_queue( physical_device ) ) {
                integrated_gpu = physical_device;
            }

            continue;
        }
    }

    if ( discrete_gpu != VK_NULL_HANDLE ) {
        vulkan_physical_device = discrete_gpu;
    } else if ( integrated_gpu != VK_NULL_HANDLE ) {
        vulkan_physical_device = integrated_gpu;
    } else {
        RASSERTM( false, "Suitable GPU device not found!" );
        return;
    }

    temp_allocator->free_marker( initial_temp_allocator_marker );

    {
        initial_temp_allocator_marker = temp_allocator->get_marker();

        u32 device_extension_count = 0;
        vkEnumerateDeviceExtensionProperties( vulkan_physical_device, nullptr, &device_extension_count, nullptr );
        VkExtensionProperties* extensions = ( VkExtensionProperties* )ralloca( sizeof( VkExtensionProperties ) * device_extension_count, temp_allocator );
        vkEnumerateDeviceExtensionProperties( vulkan_physical_device, nullptr, &device_extension_count, extensions );
        for ( size_t i = 0; i < device_extension_count; i++ ) {

            if ( !creation.force_disable_dynamic_rendering && !strcmp( extensions[ i ].extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME ) ) {
                dynamic_rendering_extension_present = true;
                continue;
            }

            if ( !strcmp( extensions[ i ].extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME ) ) {
                timeline_semaphore_extension_present = true;
                continue;
            }

            if ( !strcmp( extensions[ i ].extensionName, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME ) ) {
                synchronization2_extension_present = true;
                continue;
            }

            if ( !strcmp( extensions[ i ].extensionName, VK_NV_MESH_SHADER_EXTENSION_NAME ) ) {
                mesh_shaders_extension_present = true;
                continue;
            }
        }

        temp_allocator->free_marker( initial_temp_allocator_marker );
    }

    vkGetPhysicalDeviceProperties( vulkan_physical_device, &vulkan_physical_properties );
    gpu_timestamp_frequency = vulkan_physical_properties.limits.timestampPeriod / ( 1000 * 1000 );

    rprint( "GPU Used: %s\n", vulkan_physical_properties.deviceName );

    ubo_alignment = vulkan_physical_properties.limits.minUniformBufferOffsetAlignment;
    ssbo_alignemnt = vulkan_physical_properties.limits.minStorageBufferOffsetAlignment;

    // [TAG: BINDLESS]
    // Query bindless extension, called Descriptor Indexing (https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VK_EXT_descriptor_indexing.html)
    VkPhysicalDeviceDescriptorIndexingFeatures indexing_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES, nullptr };
    VkPhysicalDeviceFeatures2 device_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &indexing_features };

    vkGetPhysicalDeviceFeatures2( vulkan_physical_device, &device_features );
    // For the feature to be correctly working, we need both the possibility to partially bind a descriptor,
    // as some entries in the bindless array will be empty, and SpirV runtime descriptors.
    bindless_supported = indexing_features.descriptorBindingPartiallyBound && indexing_features.runtimeDescriptorArray;
    // TODO: remove when finished with bindless
    //bindless_supported = false;

    //////// Create logical device
    u32 queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties( vulkan_physical_device, &queue_family_count, nullptr );

    VkQueueFamilyProperties* queue_families = ( VkQueueFamilyProperties* )ralloca( sizeof( VkQueueFamilyProperties ) * queue_family_count, temp_allocator );
    vkGetPhysicalDeviceQueueFamilyProperties( vulkan_physical_device, &queue_family_count, queue_families );

    u32 main_queue_family_index = u32_max, transfer_queue_family_index = u32_max, compute_queue_family_index = u32_max, present_queue_family_index = u32_max;
    u32 compute_queue_index = u32_max;
    for ( u32 fi = 0; fi < queue_family_count; ++fi) {
        VkQueueFamilyProperties queue_family = queue_families[ fi ];

        if ( queue_family.queueCount == 0 ) {
            continue;
        }
#if defined(_DEBUG)
        rprint( "Family %u, flags %u queue count %u\n", fi, queue_family.queueFlags, queue_family.queueCount );
#endif // DEBUG

        // Search for main queue that should be able to do all work (graphics, compute and transfer)
        if ( (queue_family.queueFlags & ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT  )) == ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT  ) ) {
            main_queue_family_index = fi;

            if ( queue_family.queueCount > 1 ) {
                compute_queue_family_index = fi;
                compute_queue_index = 1;
            }

            continue;
        }

        // Search for another compute queue if graphics queue exposes only one queue
        if ( ( queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT ) && compute_queue_index == u32_max ) {
            compute_queue_family_index = fi;
            compute_queue_index = 0;
        }

        // Search for transfer queue
        if ( ( queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT ) == 0 && (queue_family.queueFlags & VK_QUEUE_TRANSFER_BIT) ) {
            transfer_queue_family_index = fi;
            continue;
        }
    }

    // Cache family indices
    vulkan_main_queue_family = main_queue_family_index;
    vulkan_compute_queue_family = compute_queue_family_index;
    vulkan_transfer_queue_family = transfer_queue_family_index;

    Array<const char*> device_extensions;
    device_extensions.init( temporary_allocator, 2 );
    device_extensions.push( VK_KHR_SWAPCHAIN_EXTENSION_NAME );
    device_extensions.push( VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME );

    if ( dynamic_rendering_extension_present ) {
        device_extensions.push( VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME );
    }

    if ( timeline_semaphore_extension_present ) {
        device_extensions.push( VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME );
    }

    if ( synchronization2_extension_present ) {
        device_extensions.push( VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME );
    }

    if ( mesh_shaders_extension_present ) {
        device_extensions.push( VK_NV_MESH_SHADER_EXTENSION_NAME );
    }

    const float queue_priority[] = { 1.0f, 1.0f };
    VkDeviceQueueCreateInfo queue_info[ 3 ] = {};

    u32 queue_count = 0;

    VkDeviceQueueCreateInfo& main_queue = queue_info[ queue_count++ ];
    main_queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    main_queue.queueFamilyIndex = main_queue_family_index;
    main_queue.queueCount = compute_queue_family_index == main_queue_family_index ? 2 : 1;
    main_queue.pQueuePriorities = queue_priority;

    if ( compute_queue_family_index != main_queue_family_index ) {
        VkDeviceQueueCreateInfo& compute_queue = queue_info[ queue_count++ ];
        compute_queue.queueFamilyIndex = compute_queue_family_index;
        compute_queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        compute_queue.queueCount = 1;
        compute_queue.pQueuePriorities = queue_priority;
    }

    if ( vulkan_transfer_queue_family < queue_family_count ) {
        VkDeviceQueueCreateInfo& transfer_queue_info = queue_info[ queue_count++ ];
        transfer_queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        transfer_queue_info.queueFamilyIndex = transfer_queue_family_index;
        transfer_queue_info.queueCount = 1;
        transfer_queue_info.pQueuePriorities = queue_priority;
    }

    // Enable all features: just pass the physical features 2 struct.
    VkPhysicalDeviceFeatures2 physical_features2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan11Features vulkan_11_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    void* current_pnext = &vulkan_11_features;

    VkPhysicalDeviceVulkan12Features vulkan_12_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    vulkan_12_features.pNext = current_pnext;
    current_pnext = &vulkan_12_features;

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR };
    if ( dynamic_rendering_extension_present ) {
        dynamic_rendering_features.pNext = current_pnext;
        current_pnext = &dynamic_rendering_features;
    }

    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2_features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
    if ( synchronization2_extension_present ) {
        synchronization2_features.pNext = current_pnext;
        current_pnext = &synchronization2_features;
    }

    VkPhysicalDeviceMeshShaderFeaturesNV mesh_shaders_feature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV };
    if ( mesh_shaders_extension_present ) {
        mesh_shaders_feature.taskShader = true;
        mesh_shaders_feature.meshShader = true;

        mesh_shaders_feature.pNext = current_pnext;
        current_pnext = &mesh_shaders_feature;
    }

    physical_features2.pNext = current_pnext;
    vkGetPhysicalDeviceFeatures2( vulkan_physical_device, &physical_features2 );

    RASSERT( vulkan_11_features.shaderDrawParameters == VK_TRUE );

    VkDeviceCreateInfo device_create_info { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    device_create_info.queueCreateInfoCount = queue_count;
    device_create_info.pQueueCreateInfos = queue_info;
    device_create_info.enabledExtensionCount = device_extensions.size;
    device_create_info.ppEnabledExtensionNames = device_extensions.data;
    device_create_info.pNext = &physical_features2;

    result = vkCreateDevice( vulkan_physical_device, &device_create_info, vulkan_allocation_callbacks, &vulkan_device );
    check( result );

    //  Get the function pointers to Debug Utils functions.
    if ( debug_utils_extension_present ) {
        pfnSetDebugUtilsObjectNameEXT = ( PFN_vkSetDebugUtilsObjectNameEXT )vkGetDeviceProcAddr( vulkan_device, "vkSetDebugUtilsObjectNameEXT" );
        pfnCmdBeginDebugUtilsLabelEXT = ( PFN_vkCmdBeginDebugUtilsLabelEXT )vkGetDeviceProcAddr( vulkan_device, "vkCmdBeginDebugUtilsLabelEXT" );
        pfnCmdEndDebugUtilsLabelEXT = ( PFN_vkCmdEndDebugUtilsLabelEXT )vkGetDeviceProcAddr( vulkan_device, "vkCmdEndDebugUtilsLabelEXT" );
    }

    if ( dynamic_rendering_extension_present ) {
        cmd_begin_rendering = ( PFN_vkCmdBeginRenderingKHR )vkGetDeviceProcAddr( vulkan_device, "vkCmdBeginRenderingKHR" );
        cmd_end_rendering   = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr( vulkan_device, "vkCmdEndRenderingKHR" );
    }

    if ( synchronization2_extension_present ) {
        queue_submit2 = ( PFN_vkQueueSubmit2KHR )vkGetDeviceProcAddr( vulkan_device, "vkQueueSubmit2KHR" );
        cmd_pipeline_barrier2 = ( PFN_vkCmdPipelineBarrier2KHR )vkGetDeviceProcAddr( vulkan_device, "vkCmdPipelineBarrier2KHR" );
    }

    if ( mesh_shaders_extension_present ) {
        cmd_draw_mesh_tasks = (PFN_vkCmdDrawMeshTasksNV)vkGetDeviceProcAddr( vulkan_device, "vkCmdDrawMeshTasksNV" );
        cmd_draw_mesh_tasks_indirect = (PFN_vkCmdDrawMeshTasksIndirectNV)vkGetDeviceProcAddr( vulkan_device, "vkCmdDrawMeshTasksIndirectNV" );
        cmd_draw_mesh_tasks_indirect_count = ( PFN_vkCmdDrawMeshTasksIndirectCountNV )vkGetDeviceProcAddr( vulkan_device, "vkCmdDrawMeshTasksIndirectCountNV" );
    }

    // Get main queue
    vkGetDeviceQueue( vulkan_device, main_queue_family_index, 0, &vulkan_main_queue );

    // TODO(marco): handle case where we can't create a separate compute queue
    vkGetDeviceQueue( vulkan_device, compute_queue_family_index, compute_queue_index, &vulkan_compute_queue );

    // Get transfer queue if present
    if ( vulkan_transfer_queue_family < queue_family_count ) {
        vkGetDeviceQueue( vulkan_device, transfer_queue_family_index, 0, &vulkan_transfer_queue );
    }

    // Create Framebuffers
    int window_width, window_height;
    SDL_GetWindowSize( window, &window_width, &window_height );

    //// Select Surface Format
    //const TextureFormat::Enum swapchain_formats[] = { TextureFormat::B8G8R8A8_UNORM, TextureFormat::R8G8B8A8_UNORM, TextureFormat::B8G8R8X8_UNORM, TextureFormat::B8G8R8X8_UNORM };
    const VkFormat surface_image_formats[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    const VkColorSpaceKHR surface_color_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

    u32 supported_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR( vulkan_physical_device, vulkan_window_surface, &supported_count, NULL );
    VkSurfaceFormatKHR* supported_formats = ( VkSurfaceFormatKHR* )ralloca( sizeof( VkSurfaceFormatKHR ) * supported_count, temp_allocator );
    vkGetPhysicalDeviceSurfaceFormatsKHR( vulkan_physical_device, vulkan_window_surface, &supported_count, supported_formats );

    // Cache render pass output
    swapchain_output.reset();

    //// Check for supported formats
    bool format_found = false;
    const u32 surface_format_count = ArraySize( surface_image_formats );

    for ( int i = 0; i < surface_format_count; i++ ) {
        for ( u32 j = 0; j < supported_count; j++ ) {
            if ( supported_formats[ j ].format == surface_image_formats[ i ] && supported_formats[ j ].colorSpace == surface_color_space ) {
                vulkan_surface_format = supported_formats[ j ];
                swapchain_output.color( surface_image_formats[ j ], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, RenderPassOperation::Clear );
                format_found = true;
                break;
            }
        }

        if ( format_found )
            break;
    }

    swapchain_output.depth( VK_FORMAT_D32_SFLOAT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
    swapchain_output.set_depth_stencil_operations( RenderPassOperation::Clear, RenderPassOperation::Clear );

    // Default to the first format supported.
    if ( !format_found ) {
        vulkan_surface_format = supported_formats[ 0 ];
        RASSERT( false );
    }

    // Final use of temp allocator, free all temporary memory created here.
    temp_allocator->free_marker( initial_temp_allocator_marker );

    set_present_mode( present_mode );

    //////// Create VMA Allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = vulkan_physical_device;
    allocatorInfo.device = vulkan_device;
    allocatorInfo.instance = vulkan_instance;

    result = vmaCreateAllocator( &allocatorInfo, &vma_allocator );
    check( result );

    ////////  Create Descriptor Pools
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
    result = vkCreateDescriptorPool( vulkan_device, &pool_info, vulkan_allocation_callbacks, &vulkan_descriptor_pool );
    check( result );

    // [TAG: BINDLESS]
    // Create the Descriptor Pool used by bindless, that needs update after bind flag.
    if ( bindless_supported ) {
        VkDescriptorPoolSize pool_sizes_bindless[] =
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, k_max_bindless_resources },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, k_max_bindless_resources },
        };

        // Update after bind is needed here, for each binding and in the descriptor set layout creation.
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
        pool_info.maxSets = k_max_bindless_resources * ArraySize( pool_sizes_bindless );
        pool_info.poolSizeCount = ( u32 )ArraySize( pool_sizes_bindless );
        pool_info.pPoolSizes = pool_sizes_bindless;
        result = vkCreateDescriptorPool( vulkan_device, &pool_info, vulkan_allocation_callbacks, &vulkan_bindless_descriptor_pool);
        check( result );
    }

    // Init render frame informations. This includes fences, semaphores, command buffers, ...
    // TODO: memory - allocate memory of all Device render frame stuff
    u8* memory = rallocam( sizeof( GPUTimeQueriesManager ) + sizeof( CommandBuffer* ) * 128, allocator );


    // Create vulkan pools
    const u32 num_pools = creation.num_threads * k_max_frames;
    num_threads = creation.num_threads;
    thread_frame_pools.init( allocator, num_pools, num_pools );

    gpu_time_queries_manager = ( GPUTimeQueriesManager* )( memory );
    gpu_time_queries_manager->init( thread_frame_pools.data, allocator, creation.gpu_time_queries_per_frame, creation.num_threads, k_max_frames );

    for ( u32 i = 0; i < thread_frame_pools.size; ++i ) {
        GpuThreadFramePools& pool = thread_frame_pools[ i ];
        pool.time_queries = &gpu_time_queries_manager->query_trees[ i ];

        // Create command buffer pool.
        VkCommandPoolCreateInfo cmd_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
        cmd_pool_info.queueFamilyIndex = vulkan_main_queue_family;
        cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        vkCreateCommandPool( vulkan_device, &cmd_pool_info, vulkan_allocation_callbacks, &pool.vulkan_command_pool );

        // Create timestamp query pool used for GPU timings.
        VkQueryPoolCreateInfo timestamp_pool_info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_TIMESTAMP, creation.gpu_time_queries_per_frame * 2u, 0 };
        vkCreateQueryPool( vulkan_device, &timestamp_pool_info, vulkan_allocation_callbacks, &pool.vulkan_timestamp_query_pool );

        // Create pipeline statistics query pool
        VkQueryPoolCreateInfo statistics_pool_info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_PIPELINE_STATISTICS, 7, 0 };
        statistics_pool_info.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
        vkCreateQueryPool( vulkan_device, &statistics_pool_info, vulkan_allocation_callbacks, &pool.vulkan_pipeline_stats_query_pool);
    }

    // Create resource pools
    //// Init pools
    buffers.init( allocator, k_buffers_pool_size, sizeof( Buffer ) );
    textures.init( allocator, k_textures_pool_size, sizeof( Texture ) );
    render_passes.init( allocator, k_render_passes_pool_size, sizeof( RenderPass ) );
    framebuffers.init( allocator, 256, sizeof( RenderPass ) );
    descriptor_set_layouts.init( allocator, k_descriptor_set_layouts_pool_size, sizeof( DescriptorSetLayout ) );
    pipelines.init( allocator, k_pipelines_pool_size, sizeof( Pipeline ) );
    shaders.init( allocator, k_shaders_pool_size, sizeof( ShaderState ) );
    descriptor_sets.init( allocator, k_descriptor_sets_pool_size, sizeof( DescriptorSet ) );
    samplers.init( allocator, k_samplers_pool_size, sizeof( Sampler ) );

    VkSemaphoreCreateInfo semaphore_info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    vkCreateSemaphore( vulkan_device, &semaphore_info, vulkan_allocation_callbacks, &vulkan_image_acquired_semaphore );

    for ( size_t i = 0; i < k_max_frames; i++ ) {

        vkCreateSemaphore( vulkan_device, &semaphore_info, vulkan_allocation_callbacks, &vulkan_render_complete_semaphore[ i ] );

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if ( !timeline_semaphore_extension_present ) {
            vkCreateFence( vulkan_device, &fenceInfo, vulkan_allocation_callbacks, &vulkan_command_buffer_executed_fence[ i ] );
        }
    }

    if ( timeline_semaphore_extension_present ) {
        VkSemaphoreTypeCreateInfo semaphore_type_info{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        semaphore_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        semaphore_info.pNext = &semaphore_type_info;

        vkCreateSemaphore( vulkan_device, &semaphore_info, vulkan_allocation_callbacks, &vulkan_graphics_semaphore );

        vkCreateSemaphore( vulkan_device, &semaphore_info, vulkan_allocation_callbacks, &vulkan_compute_semaphore );
    } else {
        vkCreateSemaphore( vulkan_device, &semaphore_info, vulkan_allocation_callbacks, &vulkan_compute_semaphore );

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        vkCreateFence( vulkan_device, &fenceInfo, vulkan_allocation_callbacks, &vulkan_compute_fence );
    }

    command_buffer_ring.init( this, creation.num_threads );

    // Allocate queued command buffers array
    queued_command_buffers = ( CommandBuffer** )( gpu_time_queries_manager + 1 );
    CommandBuffer** correctly_allocated_buffer = ( CommandBuffer** )( memory + sizeof( GPUTimeQueriesManager ) );
    RASSERTM( queued_command_buffers == correctly_allocated_buffer, "Wrong calculations for queued command buffers arrays. Should be %p, but it is %p.", correctly_allocated_buffer, queued_command_buffers );

    vulkan_image_index = 0;
    current_frame = 0;
    previous_frame = 0;
    absolute_frame = 0;
    timestamps_enabled = false;

    resource_deletion_queue.init( allocator, 16 );
    descriptor_set_updates.init( allocator, 16 );
    texture_to_update_bindless.init( allocator, 16 );

    // Init render pass cache
    render_pass_cache.init( allocator, 16 );

    //////// Create swapchain
    create_swapchain();

    //
    // Init primitive resources
    //
    SamplerCreation sc{};
    sc.set_address_mode_uvw( VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE )
        .set_min_mag_mip( VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR ).set_name( "Sampler Default" );
    default_sampler = create_sampler( sc );

    u32 fullscreen_size = 3 * 3 * sizeof( f32 );
    BufferCreation fullscreen_vb_creation = { VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ResourceUsageType::Immutable, fullscreen_size, 1, 0, nullptr, "Fullscreen_vb" };
    fullscreen_vertex_buffer = create_buffer( fullscreen_vb_creation );

    // Init Dummy resources
    TextureCreation dummy_texture_creation;
    dummy_texture_creation.set_size( 1, 1, 1 ).set_flags( TextureFlags::Mask::RenderTarget_mask | TextureFlags::Mask::Compute_mask ).set_format_type( VK_FORMAT_R8_UINT, TextureType::Texture2D ).set_name( "Dummy_texture" );
    dummy_texture = create_texture( dummy_texture_creation );

    BufferCreation dummy_constant_buffer_creation = { VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Immutable, 16, 0, 0, nullptr, "Dummy_cb" };
    dummy_constant_buffer = create_buffer( dummy_constant_buffer_creation );

    // Get binaries path
#if defined(_MSC_VER)
    char* vulkan_env = string_buffer.reserve( 512 );
    ExpandEnvironmentStringsA( "%VULKAN_SDK%", vulkan_env, 512 );
    char* compiler_path = string_buffer.append_use_f( "%s\\Bin\\", vulkan_env );
#else
    char* vulkan_env = getenv ("VULKAN_SDK");
    char* compiler_path = string_buffer.append_use_f( "%s/bin/", vulkan_env );
#endif

    strcpy( vulkan_binaries_path, compiler_path );
    string_buffer.clear();

    // [TAG: BINDLESS]
    // Bindless resources creation
    if ( bindless_supported ) {
        DescriptorSetLayoutCreation bindless_layout_creation;
        bindless_layout_creation.reset().add_binding( VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, k_bindless_texture_binding, k_max_bindless_resources, "BindlessTextures" )
            .add_binding( VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, k_bindless_texture_binding + 1, k_max_bindless_resources, "BindlessImages" ).set_set_index( 0 )
            .set_name( "BindlessLayout" );
        bindless_layout_creation.bindless = true;

        bindless_descriptor_set_layout = create_descriptor_set_layout( bindless_layout_creation );

        DescriptorSetCreation bindless_set_creation;
        bindless_set_creation.reset().set_layout( bindless_descriptor_set_layout );// .texture( dummy_texture, 0 ).texture( dummy_texture, 1 );
        bindless_descriptor_set = create_descriptor_set( bindless_set_creation );

        DescriptorSet* bindless_set = access_descriptor_set( bindless_descriptor_set );
        vulkan_bindless_descriptor_set_cached = bindless_set->vk_descriptor_set;
    }

    // Dynamic buffer handling
    // TODO:
    dynamic_per_frame_size = 1024 * 1024 * 10;
    BufferCreation bc;
    bc.set( VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ResourceUsageType::Immutable, dynamic_per_frame_size * k_max_frames ).set_name( "Dynamic_Persistent_Buffer" );
    dynamic_buffer = create_buffer( bc );

    MapBufferParameters cb_map = { dynamic_buffer, 0, 0 };
    dynamic_mapped_memory = ( u8* )map_buffer( cb_map );
}

void GpuDevice::shutdown() {

    vkDeviceWaitIdle( vulkan_device );

    command_buffer_ring.shutdown();

    for ( size_t i = 0; i < k_max_frames; i++ ) {
        vkDestroySemaphore( vulkan_device, vulkan_render_complete_semaphore[ i ], vulkan_allocation_callbacks );

        if ( !timeline_semaphore_extension_present ) {
            vkDestroyFence( vulkan_device, vulkan_command_buffer_executed_fence[ i ], vulkan_allocation_callbacks );
        }
    }

    if ( timeline_semaphore_extension_present ) {
        vkDestroySemaphore( vulkan_device, vulkan_graphics_semaphore, vulkan_allocation_callbacks );
        vkDestroySemaphore( vulkan_device, vulkan_compute_semaphore, vulkan_allocation_callbacks );
    }

    vkDestroySemaphore( vulkan_device, vulkan_image_acquired_semaphore, vulkan_allocation_callbacks );

    gpu_time_queries_manager->shutdown();

    MapBufferParameters cb_map = { dynamic_buffer, 0, 0 };
    unmap_buffer( cb_map );

    destroy_descriptor_set_layout( bindless_descriptor_set_layout );
    destroy_descriptor_set( bindless_descriptor_set );
    destroy_buffer( fullscreen_vertex_buffer );
    destroy_buffer( dynamic_buffer );
    destroy_render_pass( swapchain_render_pass );
    destroy_texture( dummy_texture );
    destroy_buffer( dummy_constant_buffer );
    destroy_sampler( default_sampler );

    // Add pending bindless textures to delete.
    for ( u32 i = 0; i < texture_to_update_bindless.size; ++i ) {
        ResourceUpdate& update = texture_to_update_bindless[ i ];
        if ( update.deleting ) {
            destroy_texture_instant( update.handle );
        }
    }

    // Destroy all pending resources.
    for ( u32 i = 0; i < resource_deletion_queue.size; i++ ) {
        ResourceUpdate& resource_deletion = resource_deletion_queue[ i ];

        // Skip just freed resources.
        if ( resource_deletion.current_frame == -1 )
            continue;

        switch ( resource_deletion.type ) {

            case ResourceUpdateType::Buffer:
            {
                destroy_buffer_instant( resource_deletion.handle );
                break;
            }

            case ResourceUpdateType::Pipeline:
            {
                destroy_pipeline_instant( resource_deletion.handle );
                break;
            }

            case ResourceUpdateType::RenderPass:
            {
                destroy_render_pass_instant( resource_deletion.handle );
                break;
            }

            case ResourceUpdateType::DescriptorSet:
            {
                destroy_descriptor_set_instant( resource_deletion.handle );
                break;
            }

            case ResourceUpdateType::DescriptorSetLayout:
            {
                destroy_descriptor_set_layout_instant( resource_deletion.handle );
                break;
            }

            case ResourceUpdateType::Sampler:
            {
                destroy_sampler_instant( resource_deletion.handle );
                break;
            }

            case ResourceUpdateType::ShaderState:
            {
                destroy_shader_state_instant( resource_deletion.handle );
                break;
            }

            case ResourceUpdateType::Texture:
            {
                destroy_texture_instant( resource_deletion.handle );
                break;
            }

            case ResourceUpdateType::Framebuffer:
            {
                destroy_framebuffer_instant( resource_deletion.handle );
                break;
            }

            default:
            {
                RASSERTM( false, "Cannot process resource type %u\n", resource_deletion.type );
                break;
            }
        }
    }


    // Destroy render passes from the cache.
    // Swapchain vkRenderPass is also present.
    if ( !dynamic_rendering_extension_present ) {
        FlatHashMapIterator it = render_pass_cache.iterator_begin();
        while ( it.is_valid() ) {
            VkRenderPass vk_render_pass = render_pass_cache.get( it );
            vkDestroyRenderPass( vulkan_device, vk_render_pass, vulkan_allocation_callbacks );
            render_pass_cache.iterator_advance( it );
        }
    }
    render_pass_cache.shutdown();

    // Destroy swapchain
    destroy_swapchain();
    vkDestroySurfaceKHR( vulkan_instance, vulkan_window_surface, vulkan_allocation_callbacks );

    texture_to_update_bindless.shutdown();
    resource_deletion_queue.shutdown();
    descriptor_set_updates.shutdown();

    pipelines.shutdown();
    buffers.shutdown();
    shaders.shutdown();
    textures.shutdown();
    samplers.shutdown();
    descriptor_set_layouts.shutdown();
    descriptor_sets.shutdown();
    render_passes.shutdown();
    framebuffers.shutdown();

#ifdef VULKAN_DEBUG_REPORT
    // Remove the debug report callback
    auto vkDestroyDebugUtilsMessengerEXT = ( PFN_vkDestroyDebugUtilsMessengerEXT )vkGetInstanceProcAddr( vulkan_instance, "vkDestroyDebugUtilsMessengerEXT" );
    vkDestroyDebugUtilsMessengerEXT( vulkan_instance, vulkan_debug_utils_messenger, vulkan_allocation_callbacks );
#endif // IMGUI_VULKAN_DEBUG_REPORT

    // [TAG: BINDLESS]
    if ( bindless_supported ) {
        vkDestroyDescriptorPool( vulkan_device, vulkan_bindless_descriptor_pool, vulkan_allocation_callbacks );
    }

    vkDestroyDescriptorPool( vulkan_device, vulkan_descriptor_pool, vulkan_allocation_callbacks );

    // Destroy all query and command pools
    for ( u32 i = 0; i < thread_frame_pools.size; ++i ) {
        GpuThreadFramePools& pool = thread_frame_pools[ i ];
        vkDestroyQueryPool( vulkan_device, pool.vulkan_timestamp_query_pool, vulkan_allocation_callbacks );
        vkDestroyQueryPool( vulkan_device, pool.vulkan_pipeline_stats_query_pool, vulkan_allocation_callbacks );
        vkDestroyCommandPool( vulkan_device, pool.vulkan_command_pool, vulkan_allocation_callbacks );
    }

    // Memory: this contains allocations for gpu timestamp memory, queued command buffers and render frames.
    rfree( gpu_time_queries_manager, allocator );
    thread_frame_pools.shutdown();

    // Put this here so that pools catch which kind of resource has leaked.
    vmaDestroyAllocator( vma_allocator );

    vkDestroyDevice( vulkan_device, vulkan_allocation_callbacks );

    vkDestroyInstance( vulkan_instance, vulkan_allocation_callbacks );

    string_buffer.shutdown();

    rprint( "Gpu Device shutdown\n" );
}

// Resource Creation ////////////////////////////////////////////////////////////
static void vulkan_create_texture_view( GpuDevice& gpu, const TextureViewCreation& creation, Texture* texture ) {

    //// Create the image view
    VkImageViewCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    info.image = texture->vk_image;
    info.viewType = to_vk_image_view_type( texture->type );
    info.format = texture->vk_format;

    if ( TextureFormat::has_depth_or_stencil( texture->vk_format ) ) {

        info.subresourceRange.aspectMask = TextureFormat::has_depth( texture->vk_format ) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
        // TODO:gs
        //info.subresourceRange.aspectMask |= TextureFormat::has_stencil( creation.format ) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    } else {
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    info.subresourceRange.baseMipLevel = creation.mip_base_level;
    info.subresourceRange.levelCount = creation.mip_level_count;
    info.subresourceRange.baseArrayLayer = creation.array_base_layer;
    info.subresourceRange.layerCount = creation.array_layer_count;
    check( vkCreateImageView( gpu.vulkan_device, &info, gpu.vulkan_allocation_callbacks, &texture->vk_image_view ) );

    gpu.set_resource_name( VK_OBJECT_TYPE_IMAGE_VIEW, ( u64 )texture->vk_image_view, creation.name );
}

static void vulkan_create_texture( GpuDevice& gpu, const TextureCreation& creation, TextureHandle handle, Texture* texture ) {

    texture->width = creation.width;
    texture->height = creation.height;
    texture->depth = creation.depth;
    texture->mip_base_level = 0;        // For new textures, we have a view that is for all mips and layers.
    texture->array_base_layer = 0;      // For new textures, we have a view that is for all mips and layers.
    texture->array_layer_count = creation.array_layer_count;
    texture->mip_level_count = creation.mip_level_count;
    texture->type = creation.type;
    texture->name = creation.name;
    texture->vk_format = creation.format;
    texture->sampler = nullptr;
    texture->flags = creation.flags;
    texture->parent_texture = k_invalid_texture;
    texture->handle = handle;

    //// Create the image
    VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_info.format = texture->vk_format;
    image_info.flags = 0;
    image_info.imageType = to_vk_image_type( creation.type );
    image_info.extent.width = creation.width;
    image_info.extent.height = creation.height;
    image_info.extent.depth = creation.depth;
    image_info.mipLevels = creation.mip_level_count;
    image_info.arrayLayers = creation.array_layer_count;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;

    const bool is_render_target = ( creation.flags & TextureFlags::RenderTarget_mask ) == TextureFlags::RenderTarget_mask;
    const bool is_compute_used = ( creation.flags & TextureFlags::Compute_mask ) == TextureFlags::Compute_mask;

    // Default to always readable from shader.
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

    image_info.usage |= is_compute_used ? VK_IMAGE_USAGE_STORAGE_BIT : 0;

    if ( TextureFormat::has_depth_or_stencil( creation.format ) ) {
        // Depth/Stencil textures are normally textures you render into.
        image_info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    } else {
        image_info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // TODO
        image_info.usage |= is_render_target ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0;
    }

    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo memory_info{};
    memory_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if ( creation.alias.index == k_invalid_texture.index ) {
        check( vmaCreateImage( gpu.vma_allocator, &image_info, &memory_info,
                            &texture->vk_image, &texture->vma_allocation, nullptr ) );

#if defined (_DEBUG)
        vmaSetAllocationName( gpu.vma_allocator, texture->vma_allocation, creation.name );
#endif // _DEBUG
    } else {
        Texture* alias_texture = gpu.access_texture( creation.alias );
        RASSERT( alias_texture != nullptr );

        texture->vma_allocation = 0;
        check( vmaCreateAliasingImage( gpu.vma_allocator, alias_texture->vma_allocation, &image_info, &texture->vk_image ) );
    }

    gpu.set_resource_name( VK_OBJECT_TYPE_IMAGE, ( u64 )texture->vk_image, creation.name );

    // Create default texture view.
    TextureViewCreation tvc;
    tvc.set_mips( 0, creation.mip_level_count ).set_array( 0, creation.array_layer_count ).set_name( creation.name );

    vulkan_create_texture_view( gpu, tvc, texture );
    texture->state = RESOURCE_STATE_UNDEFINED;

    // Add deferred bindless update.
    if ( gpu.bindless_supported ) {
        ResourceUpdate resource_update{ ResourceUpdateType::Texture, texture->handle.index, gpu.current_frame, 0 };
        gpu.texture_to_update_bindless.push( resource_update );
    }
}

static void upload_texture_data( Texture* texture, void* upload_data, GpuDevice& gpu ) {

    // Create stating buffer
    VkBufferCreateInfo buffer_info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    u32 image_size = texture->width * texture->height * 4;
    buffer_info.size = image_size;

    VmaAllocationCreateInfo memory_info{};
    memory_info.flags = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
    memory_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VmaAllocationInfo allocation_info{};
    VkBuffer staging_buffer;
    VmaAllocation staging_allocation;
    ( vmaCreateBuffer( gpu.vma_allocator, &buffer_info, &memory_info,
                            &staging_buffer, &staging_allocation, &allocation_info ) );

    // Copy buffer_data
    void* destination_data;
    vmaMapMemory( gpu.vma_allocator, staging_allocation, &destination_data );
    memcpy( destination_data, upload_data, static_cast< size_t >( image_size ) );
    vmaUnmapMemory( gpu.vma_allocator, staging_allocation );

    // Execute command buffer
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    // TODO: threading
    CommandBuffer* command_buffer = gpu.get_command_buffer( 0, gpu.current_frame, false );
    vkBeginCommandBuffer( command_buffer->vk_command_buffer, &beginInfo );

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { texture->width, texture->height, texture->depth };

    // Copy from the staging buffer to the image
    util_add_image_barrier( &gpu, command_buffer->vk_command_buffer, texture->vk_image, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_COPY_DEST, 0, 1, false );

    vkCmdCopyBufferToImage( command_buffer->vk_command_buffer, staging_buffer, texture->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
    // Prepare first mip to create lower mipmaps
    if ( texture->mip_level_count > 1 ) {
        util_add_image_barrier( &gpu, command_buffer->vk_command_buffer, texture->vk_image, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_COPY_SOURCE, 0, 1, false );
    }

    i32 w = texture->width;
    i32 h = texture->height;

    for ( int mip_index = 1; mip_index < texture->mip_level_count; ++mip_index ) {
        util_add_image_barrier( &gpu, command_buffer->vk_command_buffer, texture->vk_image, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_COPY_DEST, mip_index, 1, false );

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

        vkCmdBlitImage( command_buffer->vk_command_buffer, texture->vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_region, VK_FILTER_LINEAR );

        // Prepare current mip for next level
        util_add_image_barrier( &gpu, command_buffer->vk_command_buffer, texture->vk_image, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_COPY_SOURCE, mip_index, 1, false );
    }

    // Transition
    util_add_image_barrier( &gpu, command_buffer->vk_command_buffer, texture->vk_image, ( texture->mip_level_count > 1 ) ? RESOURCE_STATE_COPY_SOURCE : RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_SHADER_RESOURCE, 0, texture->mip_level_count, false );
    texture->state = RESOURCE_STATE_SHADER_RESOURCE;

    vkEndCommandBuffer( command_buffer->vk_command_buffer );

    // Submit command buffer
    if ( gpu.synchronization2_extension_present ) {
        VkCommandBufferSubmitInfoKHR command_buffer_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR };
        command_buffer_info.commandBuffer = command_buffer->vk_command_buffer;

        VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &command_buffer_info;

        gpu.queue_submit2( gpu.vulkan_main_queue, 1, &submit_info, VK_NULL_HANDLE );
    } else {
        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &command_buffer->vk_command_buffer;

        vkQueueSubmit( gpu.vulkan_main_queue, 1, &submitInfo, VK_NULL_HANDLE );
    }
    vkQueueWaitIdle( gpu.vulkan_main_queue );

    vmaDestroyBuffer( gpu.vma_allocator, staging_buffer, staging_allocation );

    // TODO: free command buffer
    vkResetCommandBuffer( command_buffer->vk_command_buffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT );
}

TextureHandle GpuDevice::create_texture( const TextureCreation& creation ) {

    u32 resource_index = textures.obtain_resource();
    TextureHandle handle = { resource_index };
    if ( resource_index == k_invalid_index ) {
        return handle;
    }

    Texture* texture = access_texture( handle );

    vulkan_create_texture( *this, creation, handle, texture );

    //// Copy buffer_data if present
    if ( creation.initial_data ) {
        upload_texture_data( texture, creation.initial_data, *this );
    }

    return handle;
}

TextureHandle GpuDevice::create_texture_view( const TextureViewCreation& creation ) {
    u32 resource_index = textures.obtain_resource();
    TextureHandle handle = { resource_index };
    if ( resource_index == k_invalid_index ) {
        return handle;
    }

    Texture* parent_texture = access_texture( creation.parent_texture );
    Texture* texture_view = access_texture( handle );

    // Copy parent texture data to texture view
    memory_copy( texture_view, parent_texture, sizeof( Texture ) );
    // Add texture view data
    texture_view->parent_texture = creation.parent_texture;
    texture_view->handle = handle;
    texture_view->array_base_layer = creation.array_base_layer;
    texture_view->mip_base_level = creation.mip_base_level;

    vulkan_create_texture_view( *this, creation, texture_view );

    return handle;
}

// helper method
bool is_end_of_line( char c ) {
    bool result = ( ( c == '\n' ) || ( c == '\r' ) );
    return( result );
}

void dump_shader_code( StringBuffer& temp_string_buffer, cstring code, VkShaderStageFlagBits stage, cstring name ) {
    rprint( "Error in creation of shader %s, stage %s. Writing shader:\n", name, to_stage_defines( stage ) );

    cstring current_code = code;
    u32 line_index = 1;
    while ( current_code ) {

        cstring end_of_line = current_code;
        if ( !end_of_line || *end_of_line == 0 ) {
            break;
        }
        while ( !is_end_of_line( *end_of_line ) ) {
            ++end_of_line;
        }
        if ( *end_of_line == '\r' ) {
            ++end_of_line;
        }
        if ( *end_of_line == '\n' ) {
            ++end_of_line;
        }

        temp_string_buffer.clear();
        char* line = temp_string_buffer.append_use_substring( current_code, 0, ( end_of_line - current_code ) );
        rprint( "%u: %s", line_index++, line );

        current_code = end_of_line;
    }
}

VkShaderModuleCreateInfo GpuDevice::compile_shader( cstring code, u32 code_size, VkShaderStageFlagBits stage, cstring name ) {

    VkShaderModuleCreateInfo shader_create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };

    // Compile from glsl to SpirV.
    // TODO: detect if input is HLSL.
    const char* temp_filename = "temp.shader";

    // Write current shader to file.
    FILE* temp_shader_file = fopen( temp_filename, "w" );
    fwrite( code, code_size, 1, temp_shader_file );
    fclose( temp_shader_file );

    sizet current_marker = temporary_allocator->get_marker();
    StringBuffer temp_string_buffer;
    temp_string_buffer.init( rkilo( 1 ), temporary_allocator );

    // Add uppercase define as STAGE_NAME
    char* stage_define = temp_string_buffer.append_use_f( "%s_%s", to_stage_defines( stage ), name );
    sizet stage_define_length = strlen( stage_define );
    for ( u32 i = 0; i < stage_define_length; ++i ) {
        stage_define[ i ] = toupper( stage_define[ i ] );
    }
    // Compile to SPV
#if defined(_MSC_VER)
    char* glsl_compiler_path = temp_string_buffer.append_use_f( "%sglslangValidator.exe", vulkan_binaries_path );
    char* final_spirv_filename = temp_string_buffer.append_use( "shader_final.spv" );
    // TODO: add optional debug information in shaders (option -g).
    char* arguments = temp_string_buffer.append_use_f( "glslangValidator.exe %s -V --target-env vulkan1.2 -o %s -S %s --D %s --D %s", temp_filename, final_spirv_filename, to_compiler_extension( stage ), stage_define, to_stage_defines( stage ) );
#else
    char* glsl_compiler_path = temp_string_buffer.append_use_f( "%sglslangValidator", vulkan_binaries_path );
    char* final_spirv_filename = temp_string_buffer.append_use( "shader_final.spv" );
    char* arguments = temp_string_buffer.append_use_f( "%s -V --target-env vulkan1.2 -o %s -S %s --D %s --D %s", temp_filename, final_spirv_filename, to_compiler_extension( stage ), stage_define, to_stage_defines( stage ) );
#endif
    process_execute( ".", glsl_compiler_path, arguments, "" );

    bool optimize_shaders = false;

    if ( optimize_shaders ) {
        // TODO: add optional optimization stage
        //"spirv-opt -O input -o output
        char* spirv_optimizer_path = temp_string_buffer.append_use_f( "%sspirv-opt.exe", vulkan_binaries_path );
        char* optimized_spirv_filename = temp_string_buffer.append_use_f( "shader_opt.spv" );
        char* spirv_opt_arguments = temp_string_buffer.append_use_f( "spirv-opt.exe -O --preserve-bindings %s -o %s", final_spirv_filename, optimized_spirv_filename );

        process_execute( ".", spirv_optimizer_path, spirv_opt_arguments, "" );

        // Read back SPV file.
        shader_create_info.pCode = reinterpret_cast< const u32* >( file_read_binary( optimized_spirv_filename, temporary_allocator, &shader_create_info.codeSize ) );

        file_delete( optimized_spirv_filename );
    } else {
        // Read back SPV file.
        shader_create_info.pCode = reinterpret_cast< const u32* >( file_read_binary( final_spirv_filename, temporary_allocator, &shader_create_info.codeSize ) );
    }

    // Handling compilation error
    if ( shader_create_info.pCode == nullptr ) {
        dump_shader_code( temp_string_buffer, code, stage, name );
    }

    // Temporary files cleanup
    file_delete( temp_filename );
    file_delete( final_spirv_filename );

    return shader_create_info;
}

ShaderStateHandle GpuDevice::create_shader_state( const ShaderStateCreation& creation ) {

    ShaderStateHandle handle = { k_invalid_index };

    if ( creation.stages_count == 0 || creation.stages == nullptr ) {
        rprint( "Shader %s does not contain shader stages.\n", creation.name );
        return handle;
    }

    handle.index = shaders.obtain_resource();
    if ( handle.index == k_invalid_index ) {
        return handle;
    }

    // For each shader stage, compile them individually.
    u32 compiled_shaders = 0;

    ShaderState* shader_state = access_shader_state( handle );
    shader_state->graphics_pipeline = true;
    shader_state->active_shaders = 0;

    // TODO(marco): should we keep this around?
    sizet current_temporary_marker = temporary_allocator->get_marker();

    StringBuffer name_buffer;
    name_buffer.init( 8000, temporary_allocator );

    // Parse result needs to be always in memory as its used to free descriptor sets.
    shader_state->parse_result = ( spirv::ParseResult* )allocator->allocate( sizeof( spirv::ParseResult ), 64 );
    memset( shader_state->parse_result, 0, sizeof( spirv::ParseResult ) );

    u32 broken_stage = u32_max;

    for ( compiled_shaders = 0; compiled_shaders < creation.stages_count; ++compiled_shaders ) {
        const ShaderStage& stage = creation.stages[ compiled_shaders ];

        // Gives priority to compute: if any is present (and it should not be) then it is not a graphics pipeline.
        if ( stage.type == VK_SHADER_STAGE_COMPUTE_BIT ) {
            shader_state->graphics_pipeline = false;
        }

        VkShaderModuleCreateInfo shader_create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };

        if ( creation.spv_input ) {
            shader_create_info.codeSize = stage.code_size;
            shader_create_info.pCode = reinterpret_cast< const u32* >( stage.code );
        } else {
            shader_create_info = compile_shader( stage.code, stage.code_size, stage.type, creation.name );
        }

        // Compile shader module
        VkPipelineShaderStageCreateInfo& shader_stage_info = shader_state->shader_stage_info[ compiled_shaders ];
        memset( &shader_stage_info, 0, sizeof( VkPipelineShaderStageCreateInfo ) );
        shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_stage_info.pName = "main";
        shader_stage_info.stage = stage.type;

        if ( vkCreateShaderModule( vulkan_device, &shader_create_info, nullptr, &shader_state->shader_stage_info[ compiled_shaders ].module ) != VK_SUCCESS ) {
            broken_stage = compiled_shaders;
            break;
        }

        spirv::parse_binary( shader_create_info.pCode, shader_create_info.codeSize, name_buffer, shader_state->parse_result );

        set_resource_name( VK_OBJECT_TYPE_SHADER_MODULE, ( u64 )shader_state->shader_stage_info[ compiled_shaders ].module, creation.name );
    }
    // Not needed anymore - temp allocator freed at the end.
    //name_buffer.shutdown();
    temporary_allocator->free_marker( current_temporary_marker );

    bool creation_failed = compiled_shaders != creation.stages_count;
    if ( !creation_failed ) {
        shader_state->active_shaders = compiled_shaders;
        shader_state->name = creation.name;
    }

    if ( creation_failed ) {
        destroy_shader_state( handle );
        handle.index = k_invalid_index;

        if ( !creation.spv_input ) {
            const ShaderStage& stage = creation.stages[ broken_stage ];
            dump_shader_code( name_buffer, stage.code, stage.type, creation.name );
        }
    }

    return handle;
}

PipelineHandle GpuDevice::create_pipeline( const PipelineCreation& creation, const char* cache_path ) {
    PipelineHandle handle = { pipelines.obtain_resource() };
    if ( handle.index == k_invalid_index ) {
        return handle;
    }

    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
    VkPipelineCacheCreateInfo pipeline_cache_create_info { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };

    bool cache_exists = file_exists( cache_path );
    if ( cache_path != nullptr && cache_exists ) {
        FileReadResult read_result = file_read_binary( cache_path, allocator );

        VkPipelineCacheHeaderVersionOne* cache_header = (VkPipelineCacheHeaderVersionOne*)read_result.data;

        if ( cache_header->deviceID == vulkan_physical_properties.deviceID &&
             cache_header->vendorID == vulkan_physical_properties.vendorID &&
             memcmp( cache_header->pipelineCacheUUID, vulkan_physical_properties.pipelineCacheUUID, VK_UUID_SIZE ) == 0 )
        {
            pipeline_cache_create_info.initialDataSize = read_result.size;
            pipeline_cache_create_info.pInitialData = read_result.data;
        }
        else
        {
            cache_exists = false;
        }

        check( vkCreatePipelineCache( vulkan_device, &pipeline_cache_create_info, vulkan_allocation_callbacks, &pipeline_cache ) );

        allocator->deallocate( read_result.data );
    }
    else {
        check( vkCreatePipelineCache( vulkan_device, &pipeline_cache_create_info, vulkan_allocation_callbacks, &pipeline_cache ) );
    }

    ShaderStateHandle shader_state = create_shader_state( creation.shaders );
    if ( shader_state.index == k_invalid_index ) {
        // Shader did not compile.
        pipelines.release_resource( handle.index );
        handle.index = k_invalid_index;

        return handle;
    }

    // Now that shaders have compiled we can create the pipeline.
    Pipeline* pipeline = access_pipeline( handle );
    ShaderState* shader_state_data = access_shader_state( shader_state );

    pipeline->shader_state = shader_state;

    VkDescriptorSetLayout vk_layouts[ k_max_descriptor_set_layouts ];

    u32 num_active_layouts = shader_state_data->parse_result->set_count;

    // Create VkPipelineLayout
    for ( u32 l = 0; l < num_active_layouts; ++l ) {

        // [TAG: BINDLESS]
        // At index 0 there is the bindless layout.
        // TODO: improve API.
        if ( l == 0 ) {
            DescriptorSetLayout* s = access_descriptor_set_layout( bindless_descriptor_set_layout );
            // Avoid deletion of this set as it is global and will be freed after.
            pipeline->descriptor_set_layout_handles[ l ] = k_invalid_layout;
            vk_layouts[ l ] = s->vk_descriptor_set_layout;
            continue;
        }
        else {
            pipeline->descriptor_set_layout_handles[ l ] = create_descriptor_set_layout( shader_state_data->parse_result->sets[ l ] );
        }

        pipeline->descriptor_set_layout[ l ] = access_descriptor_set_layout( pipeline->descriptor_set_layout_handles[ l ] );

        vk_layouts[ l ] = pipeline->descriptor_set_layout[ l ]->vk_descriptor_set_layout;
    }

    VkPipelineLayoutCreateInfo pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipeline_layout_info.pSetLayouts = vk_layouts;
    pipeline_layout_info.setLayoutCount = num_active_layouts;

    VkPipelineLayout pipeline_layout;
    check( vkCreatePipelineLayout( vulkan_device, &pipeline_layout_info, vulkan_allocation_callbacks, &pipeline_layout ) );
    // Cache pipeline layout
    pipeline->vk_pipeline_layout = pipeline_layout;
    pipeline->num_active_layouts = num_active_layouts;

    // Create full pipeline
    if ( shader_state_data->graphics_pipeline ) {
        VkGraphicsPipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

        //// Shader stage
        pipeline_info.pStages = shader_state_data->shader_stage_info;
        pipeline_info.stageCount = shader_state_data->active_shaders;
        //// PipelineLayout
        pipeline_info.layout = pipeline_layout;

        //// Vertex input
        VkPipelineVertexInputStateCreateInfo vertex_input_info = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

        // Vertex attributes.
        VkVertexInputAttributeDescription vertex_attributes[ 8 ];
        if ( creation.vertex_input.num_vertex_attributes ) {

            for ( u32 i = 0; i < creation.vertex_input.num_vertex_attributes; ++i ) {
                const VertexAttribute& vertex_attribute = creation.vertex_input.vertex_attributes[ i ];
                vertex_attributes[ i ] = { vertex_attribute.location, vertex_attribute.binding, to_vk_vertex_format( vertex_attribute.format ), vertex_attribute.offset };
            }

            vertex_input_info.vertexAttributeDescriptionCount = creation.vertex_input.num_vertex_attributes;
            vertex_input_info.pVertexAttributeDescriptions = vertex_attributes;
        } else {
            vertex_input_info.vertexAttributeDescriptionCount = 0;
            vertex_input_info.pVertexAttributeDescriptions = nullptr;
        }
        // Vertex bindings
        VkVertexInputBindingDescription vertex_bindings[ 8 ];
        if ( creation.vertex_input.num_vertex_streams ) {
            vertex_input_info.vertexBindingDescriptionCount = creation.vertex_input.num_vertex_streams;

            for ( u32 i = 0; i < creation.vertex_input.num_vertex_streams; ++i ) {
                const VertexStream& vertex_stream = creation.vertex_input.vertex_streams[ i ];
                VkVertexInputRate vertex_rate = vertex_stream.input_rate == VertexInputRate::PerVertex ? VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX : VkVertexInputRate::VK_VERTEX_INPUT_RATE_INSTANCE;
                vertex_bindings[ i ] = { vertex_stream.binding, vertex_stream.stride, vertex_rate };
            }
            vertex_input_info.pVertexBindingDescriptions = vertex_bindings;
        } else {
            vertex_input_info.vertexBindingDescriptionCount = 0;
            vertex_input_info.pVertexBindingDescriptions = nullptr;
        }

        pipeline_info.pVertexInputState = &vertex_input_info;

        //// Input Assembly
        VkPipelineInputAssemblyStateCreateInfo input_assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        input_assembly.topology = creation.topology;
        input_assembly.primitiveRestartEnable = VK_FALSE;

        pipeline_info.pInputAssemblyState = &input_assembly;

        //// Color Blending
        VkPipelineColorBlendAttachmentState color_blend_attachment[ 8 ];

        if ( creation.blend_state.active_states ) {
            RASSERTM( creation.blend_state.active_states == creation.render_pass.num_color_formats, "Blend states (count: %u) mismatch with output targets (count %u)!If blend states are active, they must be defined for all outputs", creation.blend_state.active_states, creation.render_pass.num_color_formats );
            for ( size_t i = 0; i < creation.blend_state.active_states; i++ ) {
                const BlendState& blend_state = creation.blend_state.blend_states[ i ];

                color_blend_attachment[ i ].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                color_blend_attachment[ i ].blendEnable = blend_state.blend_enabled ? VK_TRUE : VK_FALSE;
                color_blend_attachment[ i ].srcColorBlendFactor = blend_state.source_color;
                color_blend_attachment[ i ].dstColorBlendFactor = blend_state.destination_color;
                color_blend_attachment[ i ].colorBlendOp = blend_state.color_operation;

                if ( blend_state.separate_blend ) {
                    color_blend_attachment[ i ].srcAlphaBlendFactor = blend_state.source_alpha;
                    color_blend_attachment[ i ].dstAlphaBlendFactor = blend_state.destination_alpha;
                    color_blend_attachment[ i ].alphaBlendOp = blend_state.alpha_operation;
                } else {
                    color_blend_attachment[ i ].srcAlphaBlendFactor = blend_state.source_color;
                    color_blend_attachment[ i ].dstAlphaBlendFactor = blend_state.destination_color;
                    color_blend_attachment[ i ].alphaBlendOp = blend_state.color_operation;
                }
            }
        } else {
            // Default non blended state
            for ( u32 i = 0; i < creation.render_pass.num_color_formats; ++i ) {
                color_blend_attachment[ i ] = {};
                color_blend_attachment[ i ].blendEnable = VK_FALSE;
                color_blend_attachment[ i ].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            }
        }

        VkPipelineColorBlendStateCreateInfo color_blending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        color_blending.logicOpEnable = VK_FALSE;
        color_blending.logicOp = VK_LOGIC_OP_COPY; // Optional
        color_blending.attachmentCount = creation.blend_state.active_states ? creation.blend_state.active_states : creation.render_pass.num_color_formats;
        color_blending.pAttachments = color_blend_attachment;
        color_blending.blendConstants[ 0 ] = 0.0f; // Optional
        color_blending.blendConstants[ 1 ] = 0.0f; // Optional
        color_blending.blendConstants[ 2 ] = 0.0f; // Optional
        color_blending.blendConstants[ 3 ] = 0.0f; // Optional

        pipeline_info.pColorBlendState = &color_blending;

        //// Depth Stencil
        VkPipelineDepthStencilStateCreateInfo depth_stencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

        depth_stencil.depthWriteEnable = creation.depth_stencil.depth_write_enable ? VK_TRUE : VK_FALSE;
        depth_stencil.stencilTestEnable = creation.depth_stencil.stencil_enable ? VK_TRUE : VK_FALSE;
        depth_stencil.depthTestEnable = creation.depth_stencil.depth_enable ? VK_TRUE : VK_FALSE;
        depth_stencil.depthCompareOp = creation.depth_stencil.depth_comparison;
        if ( creation.depth_stencil.stencil_enable ) {
            // TODO: add stencil
            RASSERT( false );
        }

        pipeline_info.pDepthStencilState = &depth_stencil;

        //// Multisample
        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; // Optional
        multisampling.pSampleMask = nullptr; // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE; // Optional

        pipeline_info.pMultisampleState = &multisampling;

        //// Rasterizer
        VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = creation.rasterization.cull_mode;
        rasterizer.frontFace = creation.rasterization.front;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

        pipeline_info.pRasterizationState = &rasterizer;

        //// Tessellation
        pipeline_info.pTessellationState;


        //// Viewport state
        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = ( float )swapchain_width;
        viewport.height = ( float )swapchain_height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = { swapchain_width, swapchain_height };

        VkPipelineViewportStateCreateInfo viewport_state{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        pipeline_info.pViewportState = &viewport_state;

        //// Render Pass
        VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
        if ( dynamic_rendering_extension_present ) {
            pipeline_rendering_create_info.viewMask = 0;
            pipeline_rendering_create_info.colorAttachmentCount = creation.render_pass.num_color_formats;
            pipeline_rendering_create_info.pColorAttachmentFormats = creation.render_pass.num_color_formats > 0 ? creation.render_pass.color_formats : nullptr;
            pipeline_rendering_create_info.depthAttachmentFormat = creation.render_pass.depth_stencil_format;
            pipeline_rendering_create_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

            pipeline_info.pNext = &pipeline_rendering_create_info;
        } else {
            pipeline_info.renderPass = get_vulkan_render_pass( creation.render_pass, creation.name );
        }

        //// Dynamic states
        VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic_state{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic_state.dynamicStateCount = ArraySize( dynamic_states );
        dynamic_state.pDynamicStates = dynamic_states;

        pipeline_info.pDynamicState = &dynamic_state;

        check( vkCreateGraphicsPipelines( vulkan_device, pipeline_cache, 1, &pipeline_info, vulkan_allocation_callbacks, &pipeline->vk_pipeline ) );

        pipeline->vk_bind_point = VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS;
    } else {
        VkComputePipelineCreateInfo pipeline_info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };

        pipeline_info.stage = shader_state_data->shader_stage_info[ 0 ];
        pipeline_info.layout = pipeline_layout;

        check( vkCreateComputePipelines( vulkan_device, pipeline_cache, 1, &pipeline_info, vulkan_allocation_callbacks, &pipeline->vk_pipeline ) );

        pipeline->vk_bind_point = VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE;
    }

    if ( cache_path != nullptr && !cache_exists ) {
        sizet cache_data_size = 0;
        check( vkGetPipelineCacheData( vulkan_device, pipeline_cache, &cache_data_size, nullptr ) );

        void* cache_data = allocator->allocate( cache_data_size, 64 );
        check( vkGetPipelineCacheData( vulkan_device, pipeline_cache, &cache_data_size, cache_data ) );

        file_write_binary( cache_path, cache_data, cache_data_size );

        allocator->deallocate( cache_data );
    }

    vkDestroyPipelineCache( vulkan_device, pipeline_cache, vulkan_allocation_callbacks );

    return handle;
}

BufferHandle GpuDevice::create_buffer( const BufferCreation& creation ) {
    BufferHandle handle = { buffers.obtain_resource() };
    if ( handle.index == k_invalid_index ) {
        return handle;
    }

    Buffer* buffer = access_buffer( handle );

    buffer->name = creation.name;
    buffer->size = creation.size;
    buffer->type_flags = creation.type_flags;
    buffer->usage = creation.usage;
    buffer->handle = handle;
    buffer->global_offset = 0;
    buffer->parent_buffer = k_invalid_buffer;

    // Cache and calculate if dynamic buffer can be used.
    static const VkBufferUsageFlags k_dynamic_buffer_mask = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    const bool use_global_buffer = ( creation.type_flags & k_dynamic_buffer_mask ) != 0;
    if ( creation.usage == ResourceUsageType::Dynamic && use_global_buffer ) {
        buffer->parent_buffer = dynamic_buffer;
        return handle;
    }

    VkBufferCreateInfo buffer_info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | creation.type_flags;
    buffer_info.size = creation.size > 0 ? creation.size : 1;       // 0 sized creations are not permitted.

    // NOTE(marco): technically we could map a buffer if the device exposes a heap
    // with MEMORY_PROPERTY_DEVICE_LOCAL_BIT and MEMORY_PROPERTY_HOST_VISIBLE_BIT
    // but that's usually very small (256MB) unless resizable bar is enabled.
    // We simply don't allow it for now.
    RASSERT( !( creation.persistent && creation.device_only ) );

    VmaAllocationCreateInfo allocation_create_info{};
    allocation_create_info.flags = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
    if ( creation.persistent ) {
        allocation_create_info.flags = allocation_create_info.flags | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    if ( creation.device_only ) {
        allocation_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    } else {
        allocation_create_info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    }

    VmaAllocationInfo allocation_info{};
    check( vmaCreateBuffer( vma_allocator, &buffer_info, &allocation_create_info,
                            &buffer->vk_buffer, &buffer->vma_allocation, &allocation_info ) );
#if defined (_DEBUG)
    vmaSetAllocationName( vma_allocator, buffer->vma_allocation, creation.name );
#endif // _DEBUG

    set_resource_name( VK_OBJECT_TYPE_BUFFER, ( u64 )buffer->vk_buffer, creation.name );

    buffer->vk_device_memory = allocation_info.deviceMemory;

    if ( creation.initial_data ) {
        void* data;
        vmaMapMemory( vma_allocator, buffer->vma_allocation, &data );
        memcpy( data, creation.initial_data, ( size_t )creation.size );
        vmaUnmapMemory( vma_allocator, buffer->vma_allocation );
    }

    if ( creation.persistent )
    {
        buffer->mapped_data = static_cast<u8*>(allocation_info.pMappedData);
    }

    return handle;
}

SamplerHandle GpuDevice::create_sampler( const SamplerCreation& creation ) {
    SamplerHandle handle = { samplers.obtain_resource() };
    if ( handle.index == k_invalid_index ) {
        return handle;
    }

    Sampler* sampler = access_sampler( handle );

    sampler->address_mode_u = creation.address_mode_u;
    sampler->address_mode_v = creation.address_mode_v;
    sampler->address_mode_w = creation.address_mode_w;
    sampler->min_filter = creation.min_filter;
    sampler->mag_filter = creation.mag_filter;
    sampler->mip_filter = creation.mip_filter;
    sampler->name = creation.name;
    sampler->reduction_mode = creation.reduction_mode;

    VkSamplerCreateInfo create_info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    create_info.addressModeU = creation.address_mode_u;
    create_info.addressModeV = creation.address_mode_v;
    create_info.addressModeW = creation.address_mode_w;
    create_info.minFilter = creation.min_filter;
    create_info.magFilter = creation.mag_filter;
    create_info.mipmapMode = creation.mip_filter;
    create_info.anisotropyEnable = 0;
    create_info.compareEnable = 0;
    create_info.unnormalizedCoordinates = 0;
    create_info.borderColor = VkBorderColor::VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    create_info.minLod = 0;
    create_info.maxLod = 16;
    // TODO:
    /*float                   mipLodBias;
    float                   maxAnisotropy;
    VkCompareOp             compareOp;
    VkBorderColor           borderColor;
    VkBool32                unnormalizedCoordinates;*/

    VkSamplerReductionModeCreateInfoEXT createInfoReduction = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT };
    // Add optional reduction mode.
    if ( creation.reduction_mode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT ) {
        createInfoReduction.reductionMode = creation.reduction_mode;

        create_info.pNext = &createInfoReduction;
    }

    vkCreateSampler( vulkan_device, &create_info, vulkan_allocation_callbacks, &sampler->vk_sampler );

    set_resource_name( VK_OBJECT_TYPE_SAMPLER, ( u64 )sampler->vk_sampler, creation.name );

    return handle;
}


DescriptorSetLayoutHandle GpuDevice::create_descriptor_set_layout( const DescriptorSetLayoutCreation& creation ) {
    DescriptorSetLayoutHandle handle = { descriptor_set_layouts.obtain_resource() };
    if ( handle.index == k_invalid_index ) {
        return handle;
    }

    DescriptorSetLayout* descriptor_set_layout = access_descriptor_set_layout( handle );

    u16 max_binding = 0;
    for (u32 r = 0; r < creation.num_bindings; ++r) {
        const DescriptorSetLayoutCreation::Binding& input_binding = creation.bindings[r];
        max_binding = raptor_max( max_binding, input_binding.index );
    }
    max_binding += 1;

    // TODO: add support for multiple sets.
    // Create flattened binding list
    descriptor_set_layout->num_bindings = ( u16 )creation.num_bindings;
    u8* memory = rallocam( ( ( sizeof( VkDescriptorSetLayoutBinding ) + sizeof( DescriptorBinding ) ) * creation.num_bindings ) + ( sizeof(u8) * max_binding ), allocator );
    descriptor_set_layout->bindings = ( DescriptorBinding* )memory;
    descriptor_set_layout->vk_binding = ( VkDescriptorSetLayoutBinding* )( memory + sizeof( DescriptorBinding ) * creation.num_bindings );
    descriptor_set_layout->index_to_binding = ( u8* )( descriptor_set_layout->vk_binding + creation.num_bindings );
    descriptor_set_layout->handle = handle;
    descriptor_set_layout->set_index = u16( creation.set_index );
    descriptor_set_layout->bindless = creation.bindless ? 1 : 0;
    descriptor_set_layout->dynamic = creation.dynamic ? 1 : 0;

    const bool skip_bindless_bindings = bindless_supported && !creation.bindless;
    u32 used_bindings = 0;

    for ( u32 r = 0; r < creation.num_bindings; ++r ) {
        DescriptorBinding& binding = descriptor_set_layout->bindings[ r ];
        const DescriptorSetLayoutCreation::Binding& input_binding = creation.bindings[ r ];
        binding.index = input_binding.index == u16_max ? ( u16 )r : input_binding.index;
        binding.count = input_binding.count;
        binding.type = input_binding.type;
        binding.name = input_binding.name;

        // Add binding index to binding data
        descriptor_set_layout->index_to_binding[ binding.index ] = r;

        // [TAG: BINDLESS]
        // Skip bindings for images and textures as they are bindless, thus bound in the global bindless arrays (one for images, one for textures).
        // TODO(marco): better solution to allow individual image views to be bound
        if ( creation.set_index == 0 && skip_bindless_bindings && (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || binding.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) ) {
            continue;
        }

        VkDescriptorSetLayoutBinding& vk_binding = descriptor_set_layout->vk_binding[ used_bindings ];
        ++used_bindings;

        vk_binding.binding = binding.index;
        vk_binding.descriptorType = input_binding.type;
        vk_binding.descriptorType = vk_binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : vk_binding.descriptorType;
        vk_binding.descriptorCount = input_binding.count;

        // TODO:
        vk_binding.stageFlags = VK_SHADER_STAGE_ALL;
        vk_binding.pImmutableSamplers = nullptr;
    }

    // Create the descriptor set layout
    VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layout_info.bindingCount = used_bindings;
    layout_info.pBindings = descriptor_set_layout->vk_binding;

    if ( creation.bindless ) {
        // Needs update after bind flag.
        layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;

        // TODO: reenable variable descriptor count
        // Binding flags
        VkDescriptorBindingFlags bindless_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;//VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT
        VkDescriptorBindingFlags binding_flags[ 16 ];

        for ( u32 r = 0; r < creation.num_bindings; ++r ) {
            binding_flags[ r ] = bindless_flags;
        }

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr };
        extended_info.bindingCount = used_bindings;
        extended_info.pBindingFlags = binding_flags;

        layout_info.pNext = &extended_info;
        vkCreateDescriptorSetLayout( vulkan_device, &layout_info, vulkan_allocation_callbacks, &descriptor_set_layout->vk_descriptor_set_layout );
    }
    else {
        vkCreateDescriptorSetLayout( vulkan_device, &layout_info, vulkan_allocation_callbacks, &descriptor_set_layout->vk_descriptor_set_layout );
    }

    return handle;
}

//
//
void GpuDevice::fill_write_descriptor_sets( GpuDevice& gpu, const DescriptorSetLayout* descriptor_set_layout, VkDescriptorSet vk_descriptor_set,
                                            VkWriteDescriptorSet* descriptor_write, VkDescriptorBufferInfo* buffer_info, VkDescriptorImageInfo* image_info,
                                            VkSampler vk_default_sampler, u32& num_resources, const ResourceHandle* resources,
                                            const SamplerHandle* samplers, const u16* bindings ) {

    u32 used_resources = 0;
    const bool skip_bindless_bindings = gpu.bindless_supported && !descriptor_set_layout->bindless;

    for ( u32 r = 0; r < num_resources; r++ ) {

        // Binding array contains the binding point as written in the shader.
        u32 layout_binding_index = bindings[ r ];
        // index_to_binding array contains the mapping between a binding point and its
        // correct binding informations.
        u32 binding_data_index = descriptor_set_layout->index_to_binding[ layout_binding_index ];
        const DescriptorBinding& binding = descriptor_set_layout->bindings[ binding_data_index ];

        // [TAG: BINDLESS]
        // Skip bindless descriptors as they are bound in the global bindless arrays.
        if ( descriptor_set_layout->set_index == 0 && skip_bindless_bindings && (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || binding.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) ) {
            continue;
        }

        u32 i = used_resources;
        ++used_resources;

        descriptor_write[ i ] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptor_write[ i ].dstSet = vk_descriptor_set;
        // Use binding array to get final binding point.
        const u32 binding_point = binding.index;
        descriptor_write[ i ].dstBinding = binding_point;
        descriptor_write[ i ].dstArrayElement = 0;
        descriptor_write[ i ].descriptorCount = 1;

        switch ( binding.type ) {
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            {
                descriptor_write[ i ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

                // Find proper sampler.
                // TODO: improve. Remove the single texture interface ?
                image_info[ i ].sampler = vk_default_sampler;

                TextureHandle texture_handle = { resources[ r ] };
                Texture* texture_data = gpu.access_texture( texture_handle );

                image_info[ i ].imageView = texture_data->vk_image_view;

                if ( texture_data->sampler ) {
                    image_info[ i ].sampler = texture_data->sampler->vk_sampler;
                }

                if ( gpu.synchronization2_extension_present ) {
                    image_info[ i ].imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR;
                } else {
                    image_info[ i ].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }

                // TODO: else ?
                if ( samplers[ r ].index != k_invalid_index ) {
                    Sampler* sampler = gpu.access_sampler( { samplers[ r ] } );
                    image_info[ i ].sampler = sampler->vk_sampler;
                }

                descriptor_write[ i ].pImageInfo = &image_info[ i ];

                break;
            }

            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            {
                descriptor_write[ i ].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

                image_info[ i ].sampler = nullptr;
                image_info[ i ].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                TextureHandle texture_handle = { resources[ r ] };
                Texture* texture_data = gpu.access_texture( texture_handle );

                image_info[ i ].imageView = texture_data->vk_image_view;

                descriptor_write[ i ].pImageInfo = &image_info[ i ];

                break;
            }

            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            {
                BufferHandle buffer_handle = { resources[ r ] };
                Buffer* buffer = gpu.access_buffer( buffer_handle );

                descriptor_write[ i ].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptor_write[ i ].descriptorType = buffer->usage == ResourceUsageType::Dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

                // Bind parent buffer if present, used for dynamic resources.
                if ( buffer->parent_buffer.index != k_invalid_index ) {
                    Buffer* parent_buffer = gpu.access_buffer( buffer->parent_buffer );

                    buffer_info[ i ].buffer = parent_buffer->vk_buffer;
                } else {
                    buffer_info[ i ].buffer = buffer->vk_buffer;
                }

                buffer_info[ i ].offset = 0;
                buffer_info[ i ].range = buffer->size;

                descriptor_write[ i ].pBufferInfo = &buffer_info[ i ];

                break;
            }

            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            {
                BufferHandle buffer_handle = { resources[ r ] };
                Buffer* buffer = gpu.access_buffer( buffer_handle );

                descriptor_write[ i ].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                // Bind parent buffer if present, used for dynamic resources.
                if ( buffer->parent_buffer.index != k_invalid_index ) {
                    Buffer* parent_buffer = gpu.access_buffer( buffer->parent_buffer );

                    buffer_info[ i ].buffer = parent_buffer->vk_buffer;
                } else {
                    buffer_info[ i ].buffer = buffer->vk_buffer;
                }

                buffer_info[ i ].offset = 0;
                buffer_info[ i ].range = buffer->size;

                descriptor_write[ i ].pBufferInfo = &buffer_info[ i ];

                break;
            }

            default:
            {
                RASSERTM( false, "Resource type %d not supported in descriptor set creation!\n", binding.type );
                break;
            }
        }
    }

    num_resources = used_resources;
}

DescriptorSetHandle GpuDevice::create_descriptor_set( const DescriptorSetCreation& creation ) {
    DescriptorSetHandle handle = { descriptor_sets.obtain_resource() };
    if ( handle.index == k_invalid_index ) {
        return handle;
    }

    DescriptorSet* descriptor_set = access_descriptor_set( handle );
    const DescriptorSetLayout* descriptor_set_layout = access_descriptor_set_layout( creation.layout );

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    alloc_info.descriptorPool = descriptor_set_layout->bindless ? vulkan_bindless_descriptor_pool : vulkan_descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &descriptor_set_layout->vk_descriptor_set_layout;

    if ( descriptor_set_layout->bindless ) {
        VkDescriptorSetVariableDescriptorCountAllocateInfoEXT count_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT };
        u32 max_binding = k_max_bindless_resources - 1;
        count_info.descriptorSetCount = 1;
        // This number is the max allocatable count
        count_info.pDescriptorCounts = &max_binding;
        alloc_info.pNext = &count_info;
        check( vkAllocateDescriptorSets( vulkan_device, &alloc_info, &descriptor_set->vk_descriptor_set ) );
    }
    else {
        check( vkAllocateDescriptorSets( vulkan_device, &alloc_info, &descriptor_set->vk_descriptor_set ) );
    }

    // Cache data
    u8* memory = rallocam( ( sizeof( ResourceHandle ) + sizeof( SamplerHandle ) + sizeof( u16 ) ) * creation.num_resources, allocator );
    descriptor_set->resources = ( ResourceHandle* )memory;
    descriptor_set->samplers = ( SamplerHandle* )( memory + sizeof( ResourceHandle ) * creation.num_resources );
    descriptor_set->bindings = ( u16* )( memory + ( sizeof( ResourceHandle ) + sizeof( SamplerHandle ) ) * creation.num_resources );
    descriptor_set->num_resources = creation.num_resources;
    descriptor_set->layout = descriptor_set_layout;

    RASSERTM( creation.num_resources < k_max_descriptors_per_set, "Overflow in resources, please bump k_max_descriptors_per_set." );

    // Update descriptor set
    VkWriteDescriptorSet descriptor_write[ k_max_descriptors_per_set ];
    VkDescriptorBufferInfo buffer_info[ k_max_descriptors_per_set ];
    VkDescriptorImageInfo image_info[ k_max_descriptors_per_set ];

    Sampler* vk_default_sampler = access_sampler( default_sampler );

    u32 num_resources = creation.num_resources;
    fill_write_descriptor_sets( *this, descriptor_set_layout, descriptor_set->vk_descriptor_set, descriptor_write, buffer_info, image_info, vk_default_sampler->vk_sampler,
                                num_resources, creation.resources, creation.samplers, creation.bindings );

    // Cache resources
    for ( u32 r = 0; r < creation.num_resources; r++ ) {
        // TODO(marco): should resources simply be a union of VkBufferView and VkImageView?
        descriptor_set->resources[ r ] = creation.resources[ r ];
        descriptor_set->samplers[ r ] = creation.samplers[ r ];
        descriptor_set->bindings[ r ] = creation.bindings[ r ];
    }

    vkUpdateDescriptorSets( vulkan_device, num_resources, descriptor_write, 0, nullptr );

    return handle;
}

static void vulkan_create_framebuffer( GpuDevice& gpu, Framebuffer* framebuffer ) {
    RenderPass* vk_render_pass = gpu.access_render_pass( framebuffer->render_pass );

    // Create framebuffer
    VkFramebufferCreateInfo framebuffer_info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebuffer_info.renderPass = vk_render_pass->vk_render_pass;
    framebuffer_info.width = framebuffer->width;
    framebuffer_info.height = framebuffer->height;
    framebuffer_info.layers = 1;

    VkImageView framebuffer_attachments[ k_max_image_outputs + 1 ]{};
    u32 active_attachments = 0;
    for ( ; active_attachments < framebuffer->num_color_attachments; ++active_attachments ) {
        Texture* texture_vk = gpu.access_texture( framebuffer->color_attachments[ active_attachments ] );
        framebuffer_attachments[ active_attachments ] = texture_vk->vk_image_view;
    }

    if ( framebuffer->depth_stencil_attachment.index != k_invalid_index ) {
        Texture* depth_texture_vk = gpu.access_texture( framebuffer->depth_stencil_attachment );
        framebuffer_attachments[ active_attachments++ ] = depth_texture_vk->vk_image_view;
    }
    framebuffer_info.pAttachments = framebuffer_attachments;
    framebuffer_info.attachmentCount = active_attachments;

    check( vkCreateFramebuffer( gpu.vulkan_device, &framebuffer_info, nullptr, &framebuffer->vk_framebuffer ) );
    gpu.set_resource_name( VK_OBJECT_TYPE_FRAMEBUFFER, ( u64 )framebuffer->vk_framebuffer, framebuffer->name );
}

//
//
static VkRenderPass vulkan_create_render_pass( GpuDevice& gpu, const RenderPassOutput& output, cstring name ) {
    VkAttachmentDescription color_attachments[ 8 ] = {};
    VkAttachmentReference color_attachments_ref[ 8 ] = {};

    VkAttachmentLoadOp depth_op, stencil_op;
    VkImageLayout depth_initial;

    switch ( output.depth_operation ) {
        case RenderPassOperation::Load:
            depth_op = VK_ATTACHMENT_LOAD_OP_LOAD;
            depth_initial = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            break;
        case RenderPassOperation::Clear:
            depth_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth_initial = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
        default:
            depth_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depth_initial = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
    }

    switch ( output.stencil_operation ) {
        case RenderPassOperation::Load:
            stencil_op = VK_ATTACHMENT_LOAD_OP_LOAD;
            break;
        case RenderPassOperation::Clear:
            stencil_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            break;
        default:
            stencil_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            break;
    }

    // Color attachments
    u32 c = 0;
    for ( ; c < output.num_color_formats; ++c ) {
        VkAttachmentLoadOp color_op;
        VkImageLayout color_initial;
        switch ( output.color_operations[ c ] ) {
            case RenderPassOperation::Load:
                color_op = VK_ATTACHMENT_LOAD_OP_LOAD;
                color_initial = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                break;
            case RenderPassOperation::Clear:
                color_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
                color_initial = VK_IMAGE_LAYOUT_UNDEFINED;
                break;
            default:
                color_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                color_initial = VK_IMAGE_LAYOUT_UNDEFINED;
                break;
        }

        VkAttachmentDescription& color_attachment = color_attachments[ c ];
        color_attachment.format = output.color_formats[ c ];
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = color_op;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = stencil_op;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = color_initial;
        color_attachment.finalLayout = output.color_final_layouts[ c ];

        VkAttachmentReference& color_attachment_ref = color_attachments_ref[ c ];
        color_attachment_ref.attachment = c;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Depth attachment
    VkAttachmentDescription depth_attachment{};
    VkAttachmentReference depth_attachment_ref{};

    if ( output.depth_stencil_format != VK_FORMAT_UNDEFINED ) {

        depth_attachment.format = output.depth_stencil_format;
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = depth_op;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.stencilLoadOp = stencil_op;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = depth_initial;
        depth_attachment.finalLayout = output.depth_stencil_final_layout;

        depth_attachment_ref.attachment = c;
        depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    // Create subpass.
    // TODO: for now is just a simple subpass, evolve API.
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    // Calculate active attachments for the subpass
    VkAttachmentDescription attachments[ k_max_image_outputs + 1 ]{};
    for ( u32 active_attachments = 0; active_attachments < output.num_color_formats; ++active_attachments ) {
        attachments[ active_attachments ] = color_attachments[ active_attachments ];
    }
    subpass.colorAttachmentCount = output.num_color_formats;
    subpass.pColorAttachments = color_attachments_ref;

    subpass.pDepthStencilAttachment = nullptr;

    u32 depth_stencil_count = 0;
    if ( output.depth_stencil_format != VK_FORMAT_UNDEFINED ) {
        attachments[ subpass.colorAttachmentCount ] = depth_attachment;

        subpass.pDepthStencilAttachment = &depth_attachment_ref;

        depth_stencil_count = 1;
    }

    VkRenderPassCreateInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };

    render_pass_info.attachmentCount = ( output.num_color_formats ) + depth_stencil_count;
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;

    // Create external subpass dependencies
    //VkSubpassDependency external_dependencies[ 16 ];
    //u32 num_external_dependencies = 0;

    VkRenderPass vk_render_pass;
    check( vkCreateRenderPass( gpu.vulkan_device, &render_pass_info, nullptr, &vk_render_pass ) );

    gpu.set_resource_name( VK_OBJECT_TYPE_RENDER_PASS, ( u64 )vk_render_pass, name );

    return vk_render_pass;
}

//
//
static RenderPassOutput fill_render_pass_output( GpuDevice& gpu, const RenderPassCreation& creation ) {
    RenderPassOutput output;
    output.reset();

    for ( u32 i = 0; i < creation.num_render_targets; ++i ) {
        output.color( creation.color_formats[ i ], creation.color_final_layouts[ i ], creation.color_operations[ i ] );
    }
    if ( creation.depth_stencil_format != VK_FORMAT_UNDEFINED ) {
        output.depth( creation.depth_stencil_format, creation.depth_stencil_final_layout );
    }

    output.depth_operation = creation.depth_operation;
    output.stencil_operation = creation.stencil_operation;

    return output;
}

RenderPassHandle GpuDevice::create_render_pass( const RenderPassCreation& creation ) {
    RenderPassHandle handle = { render_passes.obtain_resource() };
    if ( handle.index == k_invalid_index ) {
        return handle;
    }

    RenderPass* render_pass = access_render_pass( handle );
    // Init the rest of the struct.
    render_pass->num_render_targets = ( u8 )creation.num_render_targets;
    render_pass->dispatch_x = 0;
    render_pass->dispatch_y = 0;
    render_pass->dispatch_z = 0;
    render_pass->name = creation.name;
    render_pass->vk_render_pass = VK_NULL_HANDLE;

    render_pass->output = fill_render_pass_output( *this, creation );

    // Always use render pass cache with method get_vulkan_render_pass instead of creating one.
    // Render pass cache will create a pass if needed.
    //render_pass->vk_render_pass = vulkan_create_render_pass( *this, render_pass->output, creation.name );

    if ( !dynamic_rendering_extension_present ) {
        render_pass->vk_render_pass = get_vulkan_render_pass( render_pass->output, creation.name );
    }

    return handle;
}

//
//
FramebufferHandle GpuDevice::create_framebuffer( const FramebufferCreation& creation ) {
    FramebufferHandle handle = { framebuffers.obtain_resource() };
    if ( handle.index == k_invalid_index ) {
        return handle;
    }

    Framebuffer* framebuffer = access_framebuffer( handle );
    // Init the rest of the struct.
    framebuffer->num_color_attachments = creation.num_render_targets;
    for ( u32 a = 0; a < creation.num_render_targets; ++a) {
        framebuffer->color_attachments[ a ] = creation.output_textures[ a ];
    }
    framebuffer->depth_stencil_attachment = creation.depth_stencil_texture;
    framebuffer->width   = creation.width;
    framebuffer->height   = creation.height;
    framebuffer->scale_x = creation.scale_x;
    framebuffer->scale_y = creation.scale_y;
    framebuffer->resize  = creation.resize;
    framebuffer->name    = creation.name;
    framebuffer->render_pass = creation.render_pass;

    if ( !dynamic_rendering_extension_present ) {
        vulkan_create_framebuffer( *this, framebuffer );
    }

    return handle;
}


// Resource Destruction ///////////////////////////////////////////////////

void GpuDevice::destroy_buffer( BufferHandle buffer ) {
    if ( buffer.index < buffers.pool_size ) {
        resource_deletion_queue.push( { ResourceUpdateType::Buffer, buffer.index, current_frame + k_max_frames, 1 } );
    } else {
        rprint( "Graphics error: trying to free invalid Buffer %u\n", buffer.index );
    }
}

void GpuDevice::destroy_texture( TextureHandle texture ) {
    if ( texture.index < textures.pool_size ) {
        // Do not add textures to deletion queue, textures will be deleted after bindless descriptor is updated.
        texture_to_update_bindless.push( { ResourceUpdateType::Texture, texture.index, current_frame, 1 } );
    } else {
        rprint( "Graphics error: trying to free invalid Texture %u\n", texture.index );
    }
}

void GpuDevice::destroy_pipeline( PipelineHandle pipeline ) {
    if ( pipeline.index < pipelines.pool_size ) {
        resource_deletion_queue.push( { ResourceUpdateType::Pipeline, pipeline.index, current_frame, 1 } );
        // Shader state creation is handled internally when creating a pipeline, thus add this to track correctly.
        Pipeline* v_pipeline = access_pipeline( pipeline );

        ShaderState* shader_state_data = access_shader_state( v_pipeline->shader_state );
        for ( u32 l = 0; l < shader_state_data->parse_result->set_count; ++l ) {
            if ( v_pipeline->descriptor_set_layout_handles[ l ].index != k_invalid_index ) {
                destroy_descriptor_set_layout( v_pipeline->descriptor_set_layout_handles[ l ] );
            }
        }

        destroy_shader_state( v_pipeline->shader_state );
    } else {
        rprint( "Graphics error: trying to free invalid Pipeline %u\n", pipeline.index );
    }
}

void GpuDevice::destroy_sampler( SamplerHandle sampler ) {
    if ( sampler.index < samplers.pool_size ) {
        resource_deletion_queue.push( { ResourceUpdateType::Sampler, sampler.index, current_frame, 1 } );
    } else {
        rprint( "Graphics error: trying to free invalid Sampler %u\n", sampler.index );
    }
}

void GpuDevice::destroy_descriptor_set_layout( DescriptorSetLayoutHandle descriptor_set_layout ) {
    if ( descriptor_set_layout.index < descriptor_set_layouts.pool_size ) {
        resource_deletion_queue.push( { ResourceUpdateType::DescriptorSetLayout, descriptor_set_layout.index, current_frame, 1 } );
    } else {
        rprint( "Graphics error: trying to free invalid DescriptorSetLayout %u\n", descriptor_set_layout.index );
    }
}

void GpuDevice::destroy_descriptor_set( DescriptorSetHandle descriptor_set ) {
    if ( descriptor_set.index < descriptor_sets.pool_size ) {
        resource_deletion_queue.push( { ResourceUpdateType::DescriptorSet, descriptor_set.index, current_frame, 1 } );
    } else {
        rprint( "Graphics error: trying to free invalid DescriptorSet %u\n", descriptor_set.index );
    }
}

void GpuDevice::destroy_render_pass( RenderPassHandle render_pass ) {
    if ( render_pass.index < render_passes.pool_size ) {
        resource_deletion_queue.push( { ResourceUpdateType::RenderPass, render_pass.index, current_frame, 1 } );
    } else {
        rprint( "Graphics error: trying to free invalid RenderPass %u\n", render_pass.index );
    }
}

void GpuDevice::destroy_framebuffer( FramebufferHandle framebuffer ) {
    if ( framebuffer.index < framebuffers.pool_size ) {
        resource_deletion_queue.push( { ResourceUpdateType::Framebuffer, framebuffer.index, current_frame, 1 } );
    } else {
        rprint( "Graphics error: trying to free invalid Framebuffer %u\n", framebuffer.index );
    }
}

void GpuDevice::destroy_shader_state( ShaderStateHandle shader ) {
    if ( shader.index < shaders.pool_size ) {
        resource_deletion_queue.push( { ResourceUpdateType::ShaderState, shader.index, current_frame, 1 } );

        ShaderState* state = access_shader_state( shader );

        allocator->deallocate( state->parse_result );
    } else {
        rprint( "Graphics error: trying to free invalid Shader %u\n", shader.index );
    }
}

// Real destruction methods - the other enqueue only the resources.
void GpuDevice::destroy_buffer_instant( ResourceHandle buffer ) {

    Buffer* v_buffer = ( Buffer* )buffers.access_resource( buffer );

    if ( v_buffer && v_buffer->parent_buffer.index == k_invalid_buffer.index ) {
        vmaDestroyBuffer( vma_allocator, v_buffer->vk_buffer, v_buffer->vma_allocation );
    }
    buffers.release_resource( buffer );
}

void GpuDevice::destroy_texture_instant( ResourceHandle texture ) {
    Texture* v_texture = ( Texture* )textures.access_resource( texture );

    // Skip double frees
    if ( !v_texture->vk_image_view ) {
        return;
    }

    if ( v_texture ) {
        // Default texture view added as separate destroy command.
        vkDestroyImageView( vulkan_device, v_texture->vk_image_view, vulkan_allocation_callbacks );
        v_texture->vk_image_view = VK_NULL_HANDLE;

        if ( v_texture->vma_allocation != 0 && v_texture->parent_texture.index == k_invalid_texture.index ) {
            vmaDestroyImage( vma_allocator, v_texture->vk_image, v_texture->vma_allocation );
        } else if ( v_texture->vma_allocation == nullptr ) {
            // Aliased textures
            vkDestroyImage( vulkan_device, v_texture->vk_image, vulkan_allocation_callbacks );
        }
    }
    textures.release_resource( texture );
}

void GpuDevice::destroy_pipeline_instant( ResourceHandle pipeline ) {
    Pipeline* v_pipeline = ( Pipeline* )pipelines.access_resource( pipeline );

    if ( v_pipeline ) {
        vkDestroyPipeline( vulkan_device, v_pipeline->vk_pipeline, vulkan_allocation_callbacks );

        vkDestroyPipelineLayout( vulkan_device, v_pipeline->vk_pipeline_layout, vulkan_allocation_callbacks );
    }
    pipelines.release_resource( pipeline );
}

void GpuDevice::destroy_sampler_instant( ResourceHandle sampler ) {
    Sampler* v_sampler = ( Sampler* )samplers.access_resource( sampler );

    if ( v_sampler ) {
        vkDestroySampler( vulkan_device, v_sampler->vk_sampler, vulkan_allocation_callbacks );
    }
    samplers.release_resource( sampler );
}

void GpuDevice::destroy_descriptor_set_layout_instant( ResourceHandle descriptor_set_layout ) {
    DescriptorSetLayout* v_descriptor_set_layout = ( DescriptorSetLayout* )descriptor_set_layouts.access_resource( descriptor_set_layout );

    if ( v_descriptor_set_layout ) {
        vkDestroyDescriptorSetLayout( vulkan_device, v_descriptor_set_layout->vk_descriptor_set_layout, vulkan_allocation_callbacks );

        // This contains also vk_binding allocation.
        rfree( v_descriptor_set_layout->bindings, allocator );
    }
    descriptor_set_layouts.release_resource( descriptor_set_layout );
}

void GpuDevice::destroy_descriptor_set_instant( ResourceHandle descriptor_set ) {
    DescriptorSet* v_descriptor_set = ( DescriptorSet* )descriptor_sets.access_resource( descriptor_set );

    if ( v_descriptor_set ) {
        // Contains the allocation for all the resources, binding and samplers arrays.
        rfree( v_descriptor_set->resources, allocator );
        // This is freed with the DescriptorSet pool.
        //vkFreeDescriptorSets
    }
    descriptor_sets.release_resource( descriptor_set );
}

void GpuDevice::destroy_render_pass_instant( ResourceHandle render_pass ) {
    RenderPass* v_render_pass = ( RenderPass* )render_passes.access_resource( render_pass );

    if ( v_render_pass ) {

        // NOTE: this is now destroyed with the render pass cache, to avoid double deletes.
        //vkDestroyRenderPass( vulkan_device, v_render_pass->vk_render_pass, vulkan_allocation_callbacks );
    }
    render_passes.release_resource( render_pass );
}

void GpuDevice::destroy_framebuffer_instant( ResourceHandle framebuffer ) {
    Framebuffer* v_framebuffer = ( Framebuffer* )framebuffers.access_resource( framebuffer );

    if ( v_framebuffer ) {

        for ( u32 a = 0; a < v_framebuffer->num_color_attachments; ++a ) {
            destroy_texture_instant( v_framebuffer->color_attachments[ a ].index );
        }

        if ( v_framebuffer->depth_stencil_attachment.index != k_invalid_index ) {
            destroy_texture_instant( v_framebuffer->depth_stencil_attachment.index );
        }

        if ( !dynamic_rendering_extension_present ) {
            vkDestroyFramebuffer( vulkan_device, v_framebuffer->vk_framebuffer, vulkan_allocation_callbacks );
        }
    }
    framebuffers.release_resource( framebuffer );
}

void GpuDevice::destroy_shader_state_instant( ResourceHandle shader ) {
    ShaderState* v_shader_state = ( ShaderState* )shaders.access_resource( shader );
    if ( v_shader_state ) {

        for ( size_t i = 0; i < v_shader_state->active_shaders; i++ ) {
            vkDestroyShaderModule( vulkan_device, v_shader_state->shader_stage_info[ i ].module, vulkan_allocation_callbacks );
        }
    }
    shaders.release_resource( shader );
}

void GpuDevice::set_resource_name( VkObjectType type, u64 handle, const char* name ) {

    if ( !debug_utils_extension_present ) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT name_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    name_info.objectType = type;
    name_info.objectHandle = handle;
    name_info.pObjectName = name;
    pfnSetDebugUtilsObjectNameEXT( vulkan_device, &name_info );
}

void GpuDevice::push_marker( VkCommandBuffer command_buffer, cstring name ) {

    VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
    label.pLabelName = name;
    label.color[ 0 ] = 1.0f;
    label.color[ 1 ] = 1.0f;
    label.color[ 2 ] = 1.0f;
    label.color[ 3 ] = 1.0f;
    pfnCmdBeginDebugUtilsLabelEXT( command_buffer, &label );
}

void GpuDevice::pop_marker( VkCommandBuffer command_buffer ) {
    pfnCmdEndDebugUtilsLabelEXT( command_buffer );
}

// Swapchain //////////////////////////////////////////////////////////////

template<class T>
constexpr const T& clamp( const T& v, const T& lo, const T& hi ) {
    RASSERT( !( hi < lo ) );
    return ( v < lo ) ? lo : ( hi < v ) ? hi : v;
}

void GpuDevice::create_swapchain() {

    //// Check if surface is supported
    // TODO: Windows only!
    VkBool32 surface_supported;
    vkGetPhysicalDeviceSurfaceSupportKHR( vulkan_physical_device, vulkan_main_queue_family, vulkan_window_surface, &surface_supported );
    if ( surface_supported != VK_TRUE ) {
        rprint( "Error no WSI support on physical device 0\n" );
    }

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vulkan_physical_device, vulkan_window_surface, &surface_capabilities );

    VkExtent2D swapchain_extent = surface_capabilities.currentExtent;
    if ( swapchain_extent.width == UINT32_MAX ) {
        swapchain_extent.width = clamp( swapchain_extent.width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width );
        swapchain_extent.height = clamp( swapchain_extent.height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height );
    }

    rprint( "Create swapchain %u %u - saved %u %u, min image %u\n", swapchain_extent.width, swapchain_extent.height, swapchain_width, swapchain_height, surface_capabilities.minImageCount );

    swapchain_width = ( u16 )swapchain_extent.width;
    swapchain_height = ( u16 )swapchain_extent.height;

    //vulkan_swapchain_image_count = surface_capabilities.minImageCount + 2;

    VkSwapchainCreateInfoKHR swapchain_create_info = {};
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface = vulkan_window_surface;
    swapchain_create_info.minImageCount = vulkan_swapchain_image_count;
    swapchain_create_info.imageFormat = vulkan_surface_format.format;
    swapchain_create_info.imageExtent = swapchain_extent;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.preTransform = surface_capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = vulkan_present_mode;

    VkResult result = vkCreateSwapchainKHR( vulkan_device, &swapchain_create_info, 0, &vulkan_swapchain );
    check( result );

    if ( swapchain_render_pass.index == k_invalid_index ) {
        RenderPassCreation swapchain_pass_creation = {};
        swapchain_pass_creation.set_name( "Swapchain" );
        swapchain_pass_creation.add_attachment( vulkan_surface_format.format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, RenderPassOperation::Clear );
        swapchain_pass_creation.set_depth_stencil_texture( VK_FORMAT_D32_SFLOAT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
        swapchain_pass_creation.set_depth_stencil_operations( RenderPassOperation::Clear, RenderPassOperation::Clear );

        swapchain_render_pass = create_render_pass( swapchain_pass_creation );
    }

    //// Cache swapchain images
    vkGetSwapchainImagesKHR( vulkan_device, vulkan_swapchain, &vulkan_swapchain_image_count, NULL );

    Array<VkImage> swapchain_images;
    swapchain_images.init( allocator, vulkan_swapchain_image_count, vulkan_swapchain_image_count );
    vkGetSwapchainImagesKHR( vulkan_device, vulkan_swapchain, &vulkan_swapchain_image_count, swapchain_images.data );

    // Manually transition the texture
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    CommandBuffer* command_buffer = get_command_buffer( 0, current_frame, false );
    vkBeginCommandBuffer( command_buffer->vk_command_buffer, &beginInfo );


    for ( u32 iv = 0; iv < vulkan_swapchain_image_count; iv++ ) {
        vulkan_swapchain_framebuffers[ iv ].index = framebuffers.obtain_resource();
        Framebuffer* vk_framebuffer = access_framebuffer( vulkan_swapchain_framebuffers[ iv ] );

        vk_framebuffer->render_pass = swapchain_render_pass;

        vk_framebuffer->scale_x = 1.0f;
        vk_framebuffer->scale_y = 1.0f;
        vk_framebuffer->resize = 0;

        vk_framebuffer->num_color_attachments = 1;
        vk_framebuffer->color_attachments[ 0 ].index = textures.obtain_resource();

        vk_framebuffer->name = "Swapchain";

        vk_framebuffer->width = swapchain_width;
        vk_framebuffer->height = swapchain_height;

        // Manual creation of texture
        Texture* color = access_texture( vk_framebuffer->color_attachments[ 0 ] );
        color->vk_image = swapchain_images[ iv ];
        color->vk_format = vulkan_surface_format.format;
        color->type = TextureType::Texture2D;

        TextureViewCreation tvc;
        tvc.set_mips( 0, 1 ).set_array( 0, 1 ).set_name( "framebuffer" );

        vulkan_create_texture_view( *this, tvc, color );

        TextureCreation depth_texture_creation;// = { nullptr, swapchain_width, swapchain_height, 1, 1, 0, VK_FORMAT_D32_SFLOAT, TextureType::Texture2D, k_invalid_texture, "DepthImage_Texture" };
        depth_texture_creation.set_size( swapchain_width, swapchain_height, 1 ).set_format_type( VK_FORMAT_D32_SFLOAT, TextureType::Texture2D ).set_name( "DepthImage_Texture" );
        vk_framebuffer->depth_stencil_attachment = create_texture( depth_texture_creation );

        Texture* depth_stencil_texture = access_texture( vk_framebuffer->depth_stencil_attachment );

        if ( !dynamic_rendering_extension_present ) {
            vulkan_create_framebuffer( *this, vk_framebuffer );
        }

        util_add_image_barrier( this, command_buffer->vk_command_buffer, color->vk_image, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_PRESENT, 0, 1, false );
    }

    vkEndCommandBuffer( command_buffer->vk_command_buffer );

    // Submit command buffer
    if ( synchronization2_extension_present ) {
        VkCommandBufferSubmitInfoKHR command_buffer_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR };
        command_buffer_info.commandBuffer = command_buffer->vk_command_buffer;

        VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &command_buffer_info;

        queue_submit2( vulkan_main_queue, 1, &submit_info, VK_NULL_HANDLE );
    } else {
        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &command_buffer->vk_command_buffer;

        vkQueueSubmit( vulkan_main_queue, 1, &submitInfo, VK_NULL_HANDLE );
    }
    vkQueueWaitIdle( vulkan_main_queue );

    swapchain_images.shutdown();
}

void GpuDevice::destroy_swapchain() {

    for ( size_t iv = 0; iv < vulkan_swapchain_image_count; iv++ ) {
        Framebuffer* vk_framebuffer = access_framebuffer( vulkan_swapchain_framebuffers[ iv ] );

        if ( !vk_framebuffer) {
            continue;
        }

        for ( u32 a = 0; a < vk_framebuffer->num_color_attachments; ++a ) {
            Texture* vk_texture = access_texture( vk_framebuffer->color_attachments[ a ] );

            vkDestroyImageView( vulkan_device, vk_texture->vk_image_view, vulkan_allocation_callbacks );

            textures.release_resource( vk_framebuffer->color_attachments[ a ].index );
        }

        if ( vk_framebuffer->depth_stencil_attachment.index != k_invalid_index ) {
            destroy_texture_instant( vk_framebuffer->depth_stencil_attachment.index );
        }

        if ( !dynamic_rendering_extension_present ) {
            vkDestroyFramebuffer( vulkan_device, vk_framebuffer->vk_framebuffer, vulkan_allocation_callbacks );
        }

        framebuffers.release_resource( vulkan_swapchain_framebuffers[ iv ].index );
    }

    vkDestroySwapchainKHR( vulkan_device, vulkan_swapchain, vulkan_allocation_callbacks );
}

VkRenderPass GpuDevice::get_vulkan_render_pass( const RenderPassOutput& output, cstring name ) {

    // Hash the memory output and find a compatible VkRenderPass.
    // In current form RenderPassOutput should track everything needed, including load operations.
    u64 hashed_memory = raptor::hash_bytes( ( void* )&output, sizeof( RenderPassOutput ) );
    VkRenderPass vulkan_render_pass = render_pass_cache.get( hashed_memory );
    if ( vulkan_render_pass ) {
        return vulkan_render_pass;
    }
    vulkan_render_pass = vulkan_create_render_pass( *this, output, name );
    render_pass_cache.insert( hashed_memory, vulkan_render_pass );

    return vulkan_render_pass;
}

void GpuDevice::resize_swapchain() {

    vkDeviceWaitIdle( vulkan_device );

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vulkan_physical_device, vulkan_window_surface, &surface_capabilities );
    VkExtent2D swapchain_extent = surface_capabilities.currentExtent;

    // Skip zero-sized swapchain
    //rprint( "Requested swapchain resize %u %u\n", swapchain_extent.width, swapchain_extent.height );
    if ( swapchain_extent.width == 0 || swapchain_extent.height == 0 ) {
        //rprint( "Cannot create a zero-sized swapchain\n" );
        return;
    }


    // Destroy swapchain images and framebuffers
    destroy_swapchain();
    vkDestroySurfaceKHR( vulkan_instance, vulkan_window_surface, vulkan_allocation_callbacks );

    // Recreate window surface
    if ( SDL_Vulkan_CreateSurface( sdl_window, vulkan_instance, &vulkan_window_surface ) == SDL_FALSE ) {
        rprint( "Failed to create Vulkan surface.\n" );
    }

    // Create swapchain
    create_swapchain();

    vkDeviceWaitIdle( vulkan_device );
}

// Descriptor Set /////////////////////////////////////////////////////////

void GpuDevice::update_descriptor_set( DescriptorSetHandle descriptor_set ) {

    if ( descriptor_set.index < descriptor_sets.pool_size ) {

        DescriptorSetUpdate new_update = { descriptor_set, current_frame };
        descriptor_set_updates.push( new_update );


    } else {
        rprint( "Graphics error: trying to update invalid DescriptorSet %u\n", descriptor_set.index );
    }
}

void GpuDevice::update_descriptor_set_instant( const DescriptorSetUpdate& update ) {

    // Use a dummy descriptor set to delete the vulkan descriptor set handle
    DescriptorSetHandle dummy_delete_descriptor_set_handle = { descriptor_sets.obtain_resource() };
    DescriptorSet* dummy_delete_descriptor_set = access_descriptor_set( dummy_delete_descriptor_set_handle );

    DescriptorSet* descriptor_set = access_descriptor_set( update.descriptor_set );
    const DescriptorSetLayout* descriptor_set_layout = descriptor_set->layout;

    dummy_delete_descriptor_set->vk_descriptor_set = descriptor_set->vk_descriptor_set;
    dummy_delete_descriptor_set->bindings = nullptr;
    dummy_delete_descriptor_set->resources = nullptr;
    dummy_delete_descriptor_set->samplers = nullptr;
    dummy_delete_descriptor_set->num_resources = 0;

    destroy_descriptor_set( dummy_delete_descriptor_set_handle );

    // Allocate the new descriptor set and update its content.
    VkWriteDescriptorSet descriptor_write[ 8 ];
    VkDescriptorBufferInfo buffer_info[ 8 ];
    VkDescriptorImageInfo image_info[ 8 ];

    Sampler* vk_default_sampler = access_sampler( default_sampler );

    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = vulkan_descriptor_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptor_set->layout->vk_descriptor_set_layout;
    vkAllocateDescriptorSets( vulkan_device, &allocInfo, &descriptor_set->vk_descriptor_set );

    u32 num_resources = descriptor_set_layout->num_bindings;
    fill_write_descriptor_sets( *this, descriptor_set_layout, descriptor_set->vk_descriptor_set, descriptor_write, buffer_info, image_info, vk_default_sampler->vk_sampler,
                                num_resources, descriptor_set->resources, descriptor_set->samplers, descriptor_set->bindings );

    vkUpdateDescriptorSets( vulkan_device, num_resources, descriptor_write, 0, nullptr );
}

u32 GpuDevice::get_memory_heap_count() {
    return vma_allocator->GetMemoryHeapCount();
}

//
//
void GpuDevice::resize_output_textures( FramebufferHandle framebuffer, u32 width, u32 height ) {

    // For each texture, create a temporary pooled texture and cache the handles to delete.
    // This is because we substitute just the Vulkan texture when resizing so that
    // external users don't need to update the handle.

    Framebuffer* vk_framebuffer = access_framebuffer( framebuffer );
    if ( vk_framebuffer ) {
        // No need to resize!
        if ( !vk_framebuffer->resize ) {
            return;
        }

        // Calculate new width and height based on render pass sizing informations.
        u16 new_width = ( u16 )( width * vk_framebuffer->scale_x );
        u16 new_height = ( u16 )( height * vk_framebuffer->scale_y );

        // Resize textures if needed
        const u32 rts = vk_framebuffer->num_color_attachments;
        for ( u32 i = 0; i < rts; ++i ) {
            resize_texture( vk_framebuffer->color_attachments[ i ], new_width, new_height );
        }

        if ( vk_framebuffer->depth_stencil_attachment.index != k_invalid_index ) {
            resize_texture( vk_framebuffer->depth_stencil_attachment, new_width, new_height );
        }

        // Again: create temporary resource to use the standard deferred deletion mechanism.
        FramebufferHandle framebuffer_to_destroy = { framebuffers.obtain_resource() };
        Framebuffer* vk_framebuffer_to_destroy = access_framebuffer( framebuffer_to_destroy );
        // Cache framebuffer to be deleted
        vk_framebuffer_to_destroy->vk_framebuffer = vk_framebuffer->vk_framebuffer;
        // Textures are manually destroyed few lines above, so avoid doing it again.
        vk_framebuffer_to_destroy->num_color_attachments = 0;
        vk_framebuffer_to_destroy->depth_stencil_attachment.index = k_invalid_index;

        destroy_framebuffer( framebuffer_to_destroy );

        // Update render pass size
        vk_framebuffer->width = new_width;
        vk_framebuffer->height = new_height;

        // Recreate framebuffer if present (mainly for dispatch only passes)
        if ( vk_framebuffer->vk_framebuffer && !dynamic_rendering_extension_present ) {
            vulkan_create_framebuffer( *this, vk_framebuffer );
        }
    }
}

void GpuDevice::resize_texture( TextureHandle texture, u32 width, u32 height ) {

    Texture* vk_texture = access_texture( texture );

    if ( vk_texture->width == width && vk_texture->height == height ) {
        return;
    }

    // Queue deletion of texture by creating a temporary one
    TextureHandle texture_to_delete = { textures.obtain_resource() };
    Texture* vk_texture_to_delete = access_texture( texture_to_delete );

    // Cache all informations (image, image view, flags, ...) into texture to delete.
    // Missing even one information (like it is a texture view, sparse, ...)
    // can lead to memory leaks.
    memory_copy( vk_texture_to_delete, vk_texture, sizeof( Texture ) );
    // Update handle so it can be used to update bindless to dummy texture
    // and delete the old image and image view.
    vk_texture_to_delete->handle = texture_to_delete;

    // Re-create image in place.
    TextureCreation tc;
    tc.set_flags( vk_texture->flags ).set_format_type( vk_texture->vk_format, vk_texture->type )
        .set_name( vk_texture->name ).set_size( width, height, vk_texture->depth )
        .set_mips( vk_texture->mip_level_count );
    vulkan_create_texture( *this, tc, vk_texture->handle, vk_texture );

    destroy_texture( texture_to_delete );
}

//
//

void GpuDevice::fill_barrier( FramebufferHandle framebuffer, ExecutionBarrier& out_barrier ) {

    Framebuffer* vk_framebuffer = access_framebuffer( framebuffer );

    out_barrier.num_image_barriers = 0;

    if ( vk_framebuffer ) {
        const u32 rts = vk_framebuffer->num_color_attachments;
        for ( u32 i = 0; i < rts; ++i ) {
            out_barrier.image_barriers[ out_barrier.num_image_barriers++ ].texture = vk_framebuffer->color_attachments[ i ];
        }

        if ( vk_framebuffer->depth_stencil_attachment.index != k_invalid_index ) {
            out_barrier.image_barriers[ out_barrier.num_image_barriers++ ].texture = vk_framebuffer->depth_stencil_attachment;
        }
    }
}

bool GpuDevice::buffer_ready( BufferHandle buffer_ ) {
    Buffer* buffer = access_buffer( buffer_ );
    return buffer->ready;
}

void GpuDevice::new_frame() {

    // Fence wait and reset
    if ( timeline_semaphore_extension_present  ) {
        if ( absolute_frame >= k_max_frames ) {
            u64 graphics_timeline_value = absolute_frame - ( k_max_frames - 1 );
            u64 compute_timeline_value = last_compute_semaphore_value;

            u64 wait_values[]{ graphics_timeline_value, compute_timeline_value };

            VkSemaphore semaphores[]{ vulkan_graphics_semaphore, vulkan_compute_semaphore };

            VkSemaphoreWaitInfo semaphore_wait_info{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
            semaphore_wait_info.semaphoreCount = has_async_work ? 2 : 1;
            semaphore_wait_info.pSemaphores = semaphores;
            semaphore_wait_info.pValues = wait_values;

            vkWaitSemaphores( vulkan_device, &semaphore_wait_info, ~0ull );
        }
    } else {
        VkFence render_complete_fence = vulkan_command_buffer_executed_fence[ current_frame ];

        VkFence fences[]{ render_complete_fence, vulkan_compute_fence };

        // if ( vkGetFenceStatus( vulkan_device, *render_complete_fence ) != VK_SUCCESS ) {
        //     vkWaitForFences( vulkan_device, 1, render_complete_fence, VK_TRUE, UINT64_MAX );
        // }

        u32 fence_count = has_async_work ? 2 : 1;
        vkWaitForFences( vulkan_device, fence_count, fences, VK_TRUE, UINT64_MAX );

        vkResetFences( vulkan_device, fence_count, fences );
    }

    // Command pool reset
    command_buffer_ring.reset_pools( current_frame );
    // Dynamic memory update
    const u32 used_size = dynamic_allocated_size - ( dynamic_per_frame_size * previous_frame );
    dynamic_max_per_frame_size = raptor_max( used_size, dynamic_max_per_frame_size );
    dynamic_allocated_size = dynamic_per_frame_size * current_frame;

    // Descriptor Set Updates
    if ( descriptor_set_updates.size ) {
        for ( i32 i = descriptor_set_updates.size - 1; i >= 0; i-- ) {
            DescriptorSetUpdate& update = descriptor_set_updates[ i ];

            //if ( update.frame_issued == current_frame )
            {
                update_descriptor_set_instant( update );

                update.frame_issued = u32_max;
                descriptor_set_updates.delete_swap( i );
            }
        }
    }

    // Reset time queries
    for ( u32 i = 0; i < thread_frame_pools.size / k_max_frames; ++i ) {
        GpuThreadFramePools& thread_pool = thread_frame_pools[ ( current_frame * num_threads ) + i ];
        thread_pool.time_queries->reset();
    }
}

void GpuDevice::present( CommandBuffer* async_compute_command_buffer ) {

    VkResult result = vkAcquireNextImageKHR( vulkan_device, vulkan_swapchain, UINT64_MAX, vulkan_image_acquired_semaphore, VK_NULL_HANDLE, &vulkan_image_index );
    if ( result == VK_ERROR_OUT_OF_DATE_KHR ) {
        resize_swapchain();

        // Advance frame counters that are skipped during this frame.
        frame_counters_advance();

        return;
    }

    VkSemaphore* render_complete_semaphore = &vulkan_render_complete_semaphore[ current_frame ];

    // Copy all commands
    VkCommandBuffer enqueued_command_buffers[ 4 ];
    for ( u32 c = 0; c < num_queued_command_buffers; c++ ) {

        CommandBuffer* command_buffer = queued_command_buffers[ c ];

        enqueued_command_buffers[ c ] = command_buffer->vk_command_buffer;
        // NOTE: why it was needing current_pipeline to be setup ?
        // TODO(marco): store queue type in command buffer to avoid this if not needed
        command_buffer->end_current_render_pass();

        // If marker are present, then queries are as well.
        if ( command_buffer->thread_frame_pool->time_queries->allocated_time_query ) {
            vkCmdEndQuery( command_buffer->vk_command_buffer, command_buffer->thread_frame_pool->vulkan_pipeline_stats_query_pool, 0 );
        }

        vkEndCommandBuffer( command_buffer->vk_command_buffer );
        command_buffer->is_recording = false;
        command_buffer->current_render_pass = nullptr;
    }

    if (texture_to_update_bindless.size) {
        // Handle deferred writes to bindless textures.
        VkWriteDescriptorSet bindless_descriptor_writes[k_max_bindless_resources];
        VkDescriptorImageInfo bindless_image_info[k_max_bindless_resources];

        Texture* vk_dummy_texture = access_texture(dummy_texture);

        u32 current_write_index = 0;
        for (i32 it = texture_to_update_bindless.size - 1; it >= 0; it--) {
            ResourceUpdate& texture_to_update = texture_to_update_bindless[it];

            //if ( texture_to_update.current_frame == current_frame )
            {
                Texture* texture = access_texture({ texture_to_update.handle });

                if (texture->vk_image_view == VK_NULL_HANDLE) {
                    continue;
                }

                VkWriteDescriptorSet& descriptor_write = bindless_descriptor_writes[current_write_index];
                descriptor_write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                descriptor_write.descriptorCount = 1;
                descriptor_write.dstArrayElement = texture_to_update.handle;
                descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptor_write.dstSet = vulkan_bindless_descriptor_set_cached;
                descriptor_write.dstBinding = k_bindless_texture_binding;

                // Handles should be the same.
                RASSERT(texture->handle.index == texture_to_update.handle);

                Sampler* vk_default_sampler = access_sampler(default_sampler);
                VkDescriptorImageInfo& descriptor_image_info = bindless_image_info[current_write_index];

                // Update image view and sampler if valid
                if (!texture_to_update.deleting) {
                    descriptor_image_info.imageView = texture->vk_image_view;

                    if (texture->sampler != nullptr) {
                        descriptor_image_info.sampler = texture->sampler->vk_sampler;
                    }
                    else {
                        descriptor_image_info.sampler = vk_default_sampler->vk_sampler;
                    }
                }
                else {
                    // Deleting: set to default image view and sampler in the current slot.
                    descriptor_image_info.imageView = vk_dummy_texture->vk_image_view;
                    descriptor_image_info.sampler = vk_default_sampler->vk_sampler;
                }

                descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                descriptor_write.pImageInfo = &descriptor_image_info;

                texture_to_update.current_frame = u32_max;
                // Cache this value, as delete_swap will modify the texture_to_update reference.
                const bool add_texture_to_delete = texture_to_update.deleting;
                texture_to_update_bindless.delete_swap(it);

                ++current_write_index;

                // Add texture to delete
                if (add_texture_to_delete) {
                    resource_deletion_queue.push({ ResourceUpdateType::Texture, texture->handle.index, current_frame, 1 });
                }

                // Add optional compute bindless descriptor update
                if (texture->flags & TextureFlags::Compute_mask) {
                    VkWriteDescriptorSet& descriptor_write_image = bindless_descriptor_writes[current_write_index];
                    VkDescriptorImageInfo& descriptor_image_info_compute = bindless_image_info[current_write_index];

                    // Copy common data from descriptor and image info
                    descriptor_write_image = descriptor_write;
                    descriptor_image_info_compute = descriptor_image_info;

                    descriptor_image_info_compute.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                    descriptor_write_image.dstBinding = k_bindless_image_binding;
                    descriptor_write_image.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    descriptor_write_image.pImageInfo = &descriptor_image_info_compute;

                    ++current_write_index;
                }
            }
        }

        if (current_write_index) {
            vkUpdateDescriptorSets(vulkan_device, current_write_index, bindless_descriptor_writes, 0, nullptr);
        }
    }

    // Submit command buffers
    u32 wait_semaphore_count = 1;

    if ( timeline_semaphore_extension_present ) {
        bool wait_for_compute_semaphore = ( last_compute_semaphore_value > 0 ) && has_async_work;
        if ( wait_for_compute_semaphore ) wait_semaphore_count++;

        bool wait_for_timeline_semaphore = absolute_frame >= k_max_frames;
        if ( wait_for_timeline_semaphore ) wait_semaphore_count++;

        if ( synchronization2_extension_present ) {
            VkCommandBufferSubmitInfoKHR command_buffer_info[ 4 ]{ };
            for ( u32 c = 0; c < num_queued_command_buffers; c++ ) {
                command_buffer_info[ c ].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
                command_buffer_info[ c ].commandBuffer = enqueued_command_buffers[ c ];
            }

            VkSemaphoreSubmitInfoKHR wait_semaphores[]{
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkan_image_acquired_semaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 },
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkan_compute_semaphore, last_compute_semaphore_value, VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR, 0 },
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkan_graphics_semaphore, absolute_frame - ( k_max_frames - 1 ), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR , 0 },
            };

            VkSemaphoreSubmitInfoKHR signal_semaphores[]{
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, *render_complete_semaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 },
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkan_graphics_semaphore, absolute_frame + 1, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR , 0 }
            };

            VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
            submit_info.waitSemaphoreInfoCount = wait_semaphore_count;
            submit_info.pWaitSemaphoreInfos = wait_semaphores;
            submit_info.commandBufferInfoCount = num_queued_command_buffers;
            submit_info.pCommandBufferInfos = command_buffer_info;
            submit_info.signalSemaphoreInfoCount = 2;
            submit_info.pSignalSemaphoreInfos = signal_semaphores;

            queue_submit2( vulkan_main_queue, 1, &submit_info, VK_NULL_HANDLE );
        } else {
            VkSemaphore wait_semaphores[] = { vulkan_image_acquired_semaphore, vulkan_compute_semaphore, vulkan_graphics_semaphore };
            VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };

            VkSemaphore signal_semaphores[] = { *render_complete_semaphore, vulkan_graphics_semaphore };

            // NOTE(marco): timeline semaphore values have to be monotonically increasing, so we need to start from 1
            // NOTE(marco): we still have to provide a value even for non-timeline semaphores
            u64 signal_values[] = { 0, absolute_frame + 1 };
            VkTimelineSemaphoreSubmitInfo semaphore_info{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
            semaphore_info.signalSemaphoreValueCount = 2;
            semaphore_info.pSignalSemaphoreValues = signal_values;

            u64 wait_values[] = { 0, last_compute_semaphore_value, absolute_frame - ( k_max_frames - 1 ) };
            semaphore_info.waitSemaphoreValueCount = wait_semaphore_count;
            semaphore_info.pWaitSemaphoreValues = wait_values;

             VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit_info.waitSemaphoreCount = wait_semaphore_count;
            submit_info.pWaitSemaphores = wait_semaphores;
            submit_info.pWaitDstStageMask = wait_stages;
            submit_info.commandBufferCount = num_queued_command_buffers;
            submit_info.pCommandBuffers = enqueued_command_buffers;
            submit_info.signalSemaphoreCount = 2;
            submit_info.pSignalSemaphores = signal_semaphores;

            submit_info.pNext = &semaphore_info;

            vkQueueSubmit( vulkan_main_queue, 1, &submit_info, VK_NULL_HANDLE );
        }

    } else {
        VkFence render_complete_fence = vulkan_command_buffer_executed_fence[ current_frame ];

        if ( has_async_work ) wait_semaphore_count++;

        if ( synchronization2_extension_present ) {
            VkCommandBufferSubmitInfoKHR command_buffer_info[ 4 ]{ };
            for ( u32 c = 0; c < num_queued_command_buffers; c++ ) {
                command_buffer_info[ c ].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR;
                command_buffer_info[ c ].commandBuffer = enqueued_command_buffers[ c ];
            }

            VkSemaphoreSubmitInfoKHR wait_semaphores[]{
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkan_image_acquired_semaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 },
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkan_compute_semaphore, 0, VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR, 0 }
            };

            VkSemaphoreSubmitInfoKHR signal_semaphores[]{
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, *render_complete_semaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 },
            };

            VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
            submit_info.waitSemaphoreInfoCount = wait_semaphore_count;
            submit_info.pWaitSemaphoreInfos = wait_semaphores;
            submit_info.commandBufferInfoCount = num_queued_command_buffers;
            submit_info.pCommandBufferInfos = command_buffer_info;
            submit_info.signalSemaphoreInfoCount = 1;
            submit_info.pSignalSemaphoreInfos = signal_semaphores;

            queue_submit2( vulkan_main_queue, 1, &submit_info, render_complete_fence );
        } else {
            VkSemaphore wait_semaphores[] = { vulkan_image_acquired_semaphore, vulkan_compute_semaphore };
            VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT };

            VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit_info.waitSemaphoreCount = wait_semaphore_count;
            submit_info.pWaitSemaphores = wait_semaphores;
            submit_info.pWaitDstStageMask = wait_stages;
            submit_info.commandBufferCount = num_queued_command_buffers;
            submit_info.pCommandBuffers = enqueued_command_buffers;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = render_complete_semaphore;

            vkQueueSubmit( vulkan_main_queue, 1, &submit_info, render_complete_fence );
        }
    }

    has_async_work = false;

    if ( async_compute_command_buffer != nullptr ) {
        submit_compute_load( async_compute_command_buffer );
    }

    VkPresentInfoKHR present_info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = render_complete_semaphore;

    VkSwapchainKHR swap_chains[] = { vulkan_swapchain };
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swap_chains;
    present_info.pImageIndices = &vulkan_image_index;
    present_info.pResults = nullptr; // Optional
    result = vkQueuePresentKHR( vulkan_main_queue, &present_info );

    num_queued_command_buffers = 0;

    //
    // GPU Timestamp resolve
    if ( timestamps_enabled ) {

        // Reset the frame statistics
        gpu_time_queries_manager->frame_pipeline_statistics.reset();

        temporary_allocator->clear();

        // Query results from previous frame.
        for (u32 i = 0; i < num_threads; ++i) {
            const u32 pool_index = ( previous_frame * num_threads ) + i;
            GpuThreadFramePools& thread_pool = thread_frame_pools[ pool_index ];
            GpuTimeQueryTree* time_query = thread_pool.time_queries;

            // For each active time query pool
            if ( time_query && time_query->allocated_time_query ) {

                // Query GPU for all timestamps.
                const u32 query_offset = ( pool_index * gpu_time_queries_manager->queries_per_thread );
                const u32 query_count = time_query->allocated_time_query;
                u64* timestamps_data = ( u64* )ralloca( query_count * 2 * sizeof( u64 ), temporary_allocator );
                vkGetQueryPoolResults( vulkan_device, thread_pool.vulkan_timestamp_query_pool, 0, query_count * 2,
                                       sizeof( u64 ) * query_count * 2, timestamps_data,
                                       sizeof( u64 ), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT );

                // Calculate and cache the elapsed time
                for ( u32 i = 0; i < query_count; i++ ) {
                    u32 index = ( query_offset) + i;

                    GPUTimeQuery& timestamp = gpu_time_queries_manager->timestamps[ index ];

                    double start = ( double )timestamps_data[ ( i * 2 ) ];
                    double end = ( double )timestamps_data[ ( i * 2 ) + 1 ];
                    double range = end - start;
                    double elapsed_time = range * gpu_timestamp_frequency;

                    timestamp.elapsed_ms = elapsed_time;
                    timestamp.frame_index = absolute_frame;

                    //print_format( "%s: %2.3f d(%u) - ", timestamp.name, elapsed_time, timestamp.depth );
                }

                // Query and sum pipeline statistics
                u64* pipeline_statistics_data = ( u64* )ralloca( GpuPipelineStatistics::Count * sizeof( u64 ), temporary_allocator );
                vkGetQueryPoolResults( vulkan_device, thread_pool.vulkan_pipeline_stats_query_pool, 0, 1,
                                       GpuPipelineStatistics::Count * sizeof( u64 ), pipeline_statistics_data, sizeof( u64 ), VK_QUERY_RESULT_64_BIT );

                for ( u32 i = 0; i < GpuPipelineStatistics::Count; ++i ) {
                    gpu_time_queries_manager->frame_pipeline_statistics.statistics[ i ] += pipeline_statistics_data[ i ];
                }
            }

            temporary_allocator->clear();
        }

        //rprint( "%llu %f\n", gpu_time_queries_manager->frame_pipeline_statistics.statistics[ 6 ], ( gpu_time_queries_manager->frame_pipeline_statistics.statistics[ 6 ] * 1.0 ) / ( swapchain_width * swapchain_height ) );
    }

    if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || resized ) {
        resized = false;
        resize_swapchain();

        // Advance frame counters that are skipped during this frame.
        frame_counters_advance();

        return;
    }

    //raptor::print_format( "Index %u, %u, %u\n", current_frame, previous_frame, vulkan_image_index );

    // This is called inside resize_swapchain as well to correctly work.
    frame_counters_advance();

    // Resource deletion using reverse iteration and swap with last element.
    if ( resource_deletion_queue.size > 0 ) {
        for ( i32 i = resource_deletion_queue.size - 1; i >= 0; i-- ) {
            ResourceUpdate& resource_deletion = resource_deletion_queue[ i ];

            if ( resource_deletion.current_frame == current_frame ) {

                switch ( resource_deletion.type ) {

                    case ResourceUpdateType::Buffer:
                    {
                        destroy_buffer_instant( resource_deletion.handle );
                        break;
                    }

                    case ResourceUpdateType::Pipeline:
                    {
                        destroy_pipeline_instant( resource_deletion.handle );
                        break;
                    }

                    case ResourceUpdateType::RenderPass:
                    {
                        destroy_render_pass_instant( resource_deletion.handle );
                        break;
                    }

                    case ResourceUpdateType::Framebuffer:
                    {
                        destroy_framebuffer_instant( resource_deletion.handle );
                        break;
                    }

                    case ResourceUpdateType::DescriptorSet:
                    {
                        destroy_descriptor_set_instant( resource_deletion.handle );
                        break;
                    }

                    case ResourceUpdateType::DescriptorSetLayout:
                    {
                        destroy_descriptor_set_layout_instant( resource_deletion.handle );
                        break;
                    }

                    case ResourceUpdateType::Sampler:
                    {
                        destroy_sampler_instant( resource_deletion.handle );
                        break;
                    }

                    case ResourceUpdateType::ShaderState:
                    {
                        destroy_shader_state_instant( resource_deletion.handle );
                        break;
                    }

                    case ResourceUpdateType::Texture:
                    {
                        destroy_texture_instant( resource_deletion.handle );
                        break;
                    }
                }

                // Mark resource as free
                resource_deletion.current_frame = u32_max;

                // Swap element
                resource_deletion_queue.delete_swap( i );
            }
        }
    }
}

void GpuDevice::submit_compute_load( CommandBuffer* command_buffer ) {
    has_async_work = true;

    if ( timeline_semaphore_extension_present ) {
        bool has_wait_semaphore = last_compute_semaphore_value > 0;

        if ( synchronization2_extension_present ) {

            VkSemaphoreSubmitInfoKHR wait_semaphores[]{
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkan_compute_semaphore, last_compute_semaphore_value, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, 0 }
            };

            last_compute_semaphore_value++;

            VkSemaphoreSubmitInfoKHR signal_semaphores[]{
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkan_compute_semaphore, last_compute_semaphore_value, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR, 0 },
            };

            VkCommandBufferSubmitInfoKHR command_buffer_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR };
            command_buffer_info.commandBuffer = command_buffer->vk_command_buffer;

            VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
            submit_info.waitSemaphoreInfoCount = has_wait_semaphore ? 1 : 0;
            submit_info.pWaitSemaphoreInfos = wait_semaphores;
            submit_info.commandBufferInfoCount = 1;
            submit_info.pCommandBufferInfos = &command_buffer_info;
            submit_info.signalSemaphoreInfoCount = 1;
            submit_info.pSignalSemaphoreInfos = signal_semaphores;

            queue_submit2( vulkan_compute_queue, 1, &submit_info, VK_NULL_HANDLE );
        } else {
            VkSemaphore wait_semaphores[] = { vulkan_compute_semaphore };
            VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };

            VkSemaphore signal_semaphores[] = { vulkan_compute_semaphore };

            VkTimelineSemaphoreSubmitInfo semaphore_info{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };

            u64 wait_values[] = { last_compute_semaphore_value };
            semaphore_info.waitSemaphoreValueCount = has_wait_semaphore ? 1 : 0;
            semaphore_info.pWaitSemaphoreValues = wait_values;

            last_compute_semaphore_value++;

            u64 signal_values[] = { last_compute_semaphore_value };
            semaphore_info.signalSemaphoreValueCount = 1;
            semaphore_info.pSignalSemaphoreValues = signal_values;

            VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit_info.waitSemaphoreCount = has_wait_semaphore ? 1 : 0;
            submit_info.pWaitSemaphores = wait_semaphores;
            submit_info.pWaitDstStageMask = wait_stages;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer->vk_command_buffer;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = signal_semaphores;

            submit_info.pNext = &semaphore_info;

            vkQueueSubmit( vulkan_main_queue, 1, &submit_info, VK_NULL_HANDLE );
        }
    } else {
        if ( vkGetFenceStatus( vulkan_device, vulkan_compute_fence ) != VK_SUCCESS ) {
            vkWaitForFences( vulkan_device, 1, &vulkan_compute_fence, VK_TRUE, UINT64_MAX );
        }

        vkResetFences( vulkan_device, 1, &vulkan_compute_fence );

        if ( synchronization2_extension_present ) {
            VkCommandBufferSubmitInfoKHR command_buffer_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR };
            command_buffer_info.commandBuffer = command_buffer->vk_command_buffer;

            VkSemaphoreSubmitInfoKHR signal_semaphores[]{
                { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr, vulkan_compute_semaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, 0 },
            };

            VkSubmitInfo2KHR submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR };
            submit_info.waitSemaphoreInfoCount = 0;
            submit_info.pWaitSemaphoreInfos = nullptr;
            submit_info.commandBufferInfoCount = 1;
            submit_info.pCommandBufferInfos = &command_buffer_info;
            submit_info.signalSemaphoreInfoCount = 1;
            submit_info.pSignalSemaphoreInfos = signal_semaphores;

            queue_submit2( vulkan_compute_queue, 1, &submit_info, vulkan_compute_fence );
        } else {
            VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit_info.waitSemaphoreCount = 0;
            submit_info.pWaitSemaphores = nullptr;
            submit_info.pWaitDstStageMask = nullptr;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer->vk_command_buffer;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &vulkan_compute_semaphore;

            vkQueueSubmit( vulkan_compute_queue, 1, &submit_info, vulkan_compute_fence );
        }
    }
}

static VkPresentModeKHR to_vk_present_mode( PresentMode::Enum mode ) {
    switch ( mode ) {
        case PresentMode::VSyncFast:
            return VK_PRESENT_MODE_MAILBOX_KHR;
        case PresentMode::VSyncRelaxed:
            return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        case PresentMode::Immediate:
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        case PresentMode::VSync:
        default:
            return VK_PRESENT_MODE_FIFO_KHR;
    }
}

void GpuDevice::set_present_mode( PresentMode::Enum mode ) {

    // Request a certain mode and confirm that it is available. If not use VK_PRESENT_MODE_FIFO_KHR which is mandatory
    u32 supported_count = 0;

    static VkPresentModeKHR supported_mode_allocated[ 8 ];
    vkGetPhysicalDeviceSurfacePresentModesKHR( vulkan_physical_device, vulkan_window_surface, &supported_count, NULL );
    RASSERT( supported_count < 8 );
    vkGetPhysicalDeviceSurfacePresentModesKHR( vulkan_physical_device, vulkan_window_surface, &supported_count, supported_mode_allocated );

    bool mode_found = false;
    VkPresentModeKHR requested_mode = to_vk_present_mode( mode );
    for ( u32 j = 0; j < supported_count; j++ ) {
        if ( requested_mode == supported_mode_allocated[ j ] ) {
            mode_found = true;
            break;
        }
    }

    // Default to VK_PRESENT_MODE_FIFO_KHR that is guaranteed to always be supported
    vulkan_present_mode = mode_found ? requested_mode : VK_PRESENT_MODE_FIFO_KHR;
    // Use 4 for immediate ?
    vulkan_swapchain_image_count = 3;// vulkan_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? 2 : 3;

    present_mode = mode_found ? mode : PresentMode::VSync;
}

void GpuDevice::link_texture_sampler( TextureHandle texture, SamplerHandle sampler ) {

    Texture* texture_vk = access_texture( texture );
    Sampler* sampler_vk = access_sampler( sampler );

    texture_vk->sampler = sampler_vk;
}

void GpuDevice::frame_counters_advance() {
    previous_frame = current_frame;
    current_frame = ( current_frame + 1 ) % k_max_frames;

    ++absolute_frame;
}

//
//
void GpuDevice::queue_command_buffer( CommandBuffer* command_buffer ) {

    queued_command_buffers[ num_queued_command_buffers++ ] = command_buffer;
}

//
//
CommandBuffer* GpuDevice::get_command_buffer( u32 thread_index, u32 frame_index, bool begin ) {
    CommandBuffer* cb = command_buffer_ring.get_command_buffer( frame_index, thread_index, begin );
    return cb;
}

//
//
CommandBuffer* GpuDevice::get_secondary_command_buffer( u32 thread_index, u32 frame_index ) {
    CommandBuffer* cb = command_buffer_ring.get_secondary_command_buffer( frame_index, thread_index );
    return cb;
}

// Resource Description Query /////////////////////////////////////////////

void GpuDevice::query_buffer( BufferHandle buffer, BufferDescription& out_description ) {
    if ( buffer.index != k_invalid_index ) {
        const Buffer* buffer_data = access_buffer( buffer );

        out_description.name = buffer_data->name;
        out_description.size = buffer_data->size;
        out_description.type_flags = buffer_data->type_flags;
        out_description.usage = buffer_data->usage;
        out_description.parent_handle = buffer_data->parent_buffer;
        out_description.native_handle = ( void* )&buffer_data->vk_buffer;
    }
}

void GpuDevice::query_texture( TextureHandle texture, TextureDescription& out_description ) {
    if ( texture.index != k_invalid_index ) {
        const Texture* texture_data = access_texture( texture );

        out_description.width = texture_data->width;
        out_description.height = texture_data->height;
        out_description.depth = texture_data->depth;
        out_description.format = texture_data->vk_format;
        out_description.mipmaps = texture_data->mip_level_count;
        out_description.type = texture_data->type;
        out_description.render_target = ( texture_data->flags & TextureFlags::RenderTarget_mask ) == TextureFlags::RenderTarget_mask;
        out_description.compute_access = ( texture_data->flags & TextureFlags::Compute_mask ) == TextureFlags::Compute_mask;
        out_description.native_handle = ( void* )&texture_data->vk_image;
        out_description.name = texture_data->name;
    }
}

void GpuDevice::query_pipeline( PipelineHandle pipeline, PipelineDescription& out_description ) {
    if ( pipeline.index != k_invalid_index ) {
        const Pipeline* pipeline_data = access_pipeline( pipeline );

        out_description.shader = pipeline_data->shader_state;
    }
}

void GpuDevice::query_sampler( SamplerHandle sampler, SamplerDescription& out_description ) {
    if ( sampler.index != k_invalid_index ) {
        const Sampler* sampler_data = access_sampler( sampler );

        out_description.address_mode_u = sampler_data->address_mode_u;
        out_description.address_mode_v = sampler_data->address_mode_v;
        out_description.address_mode_w = sampler_data->address_mode_w;

        out_description.min_filter = sampler_data->min_filter;
        out_description.mag_filter = sampler_data->mag_filter;
        out_description.mip_filter = sampler_data->mip_filter;

        out_description.name = sampler_data->name;
    }
}

void GpuDevice::query_descriptor_set_layout( DescriptorSetLayoutHandle descriptor_set_layout, DescriptorSetLayoutDescription& out_description ) {
    if ( descriptor_set_layout.index != k_invalid_index ) {
        const DescriptorSetLayout* descriptor_set_layout_data = access_descriptor_set_layout( descriptor_set_layout );

        out_description.bindings = descriptor_set_layout_data->bindings;
        out_description.num_active_bindings = descriptor_set_layout_data->num_bindings;
    }
}

void GpuDevice::query_descriptor_set( DescriptorSetHandle descriptor_set, DesciptorSetDescription& out_description ) {
    if ( descriptor_set.index != k_invalid_index ) {
        const DescriptorSet* descriptor_set_data = access_descriptor_set( descriptor_set );

        out_description.num_active_resources = descriptor_set_data->num_resources;
        for ( u32 i = 0; i < out_description.num_active_resources; ++i ) {
            //out_description.resources[ i ].data = descriptor_set_data->resources[ i ].data;
        }
    }
}

const RenderPassOutput& GpuDevice::get_render_pass_output( RenderPassHandle render_pass ) const {
    const RenderPass* vulkan_render_pass = access_render_pass( render_pass );
    return vulkan_render_pass->output;
}

// Resource Map/Unmap /////////////////////////////////////////////////////
void* GpuDevice::map_buffer( const MapBufferParameters& parameters ) {
    if ( parameters.buffer.index == k_invalid_index )
        return nullptr;

    Buffer* buffer = access_buffer( parameters.buffer );

    if ( buffer->parent_buffer.index == dynamic_buffer.index ) {

        buffer->global_offset = dynamic_allocated_size;

        return dynamic_allocate( parameters.size == 0 ? buffer->size : parameters.size );
    }

    void* data;
    vmaMapMemory( vma_allocator, buffer->vma_allocation, &data );

    return data;
}

void GpuDevice::unmap_buffer( const MapBufferParameters& parameters ) {
    if ( parameters.buffer.index == k_invalid_index )
        return;

    Buffer* buffer = access_buffer( parameters.buffer );
    if ( buffer->parent_buffer.index == dynamic_buffer.index )
        return;

    vmaUnmapMemory( vma_allocator, buffer->vma_allocation );
}

void* GpuDevice::dynamic_allocate( u32 size ) {
    void* mapped_memory = dynamic_mapped_memory + dynamic_allocated_size;
    dynamic_allocated_size += ( u32 )raptor::memory_align( size, ubo_alignment );
    return mapped_memory;
}

void GpuDevice::set_buffer_global_offset( BufferHandle buffer, u32 offset ) {
    if ( buffer.index == k_invalid_index )
        return;

    Buffer* vulkan_buffer = access_buffer( buffer );
    vulkan_buffer->global_offset = offset;
}

u32 GpuDevice::copy_gpu_timestamps( GPUTimeQuery* out_timestamps ) {
    return gpu_time_queries_manager->resolve( previous_frame, out_timestamps );

}

// Utility methods ////////////////////////////////////////////////////////

void check_result( VkResult result ) {
    if ( result == VK_SUCCESS ) {
        return;
    }

    rprint( "Vulkan error: code(%u)", result );
    if ( result < 0 ) {
        RASSERTM( false, "Vulkan error: aborting." );
    }
}
// Device /////////////////////////////////////////////////////////////////

BufferHandle GpuDevice::get_fullscreen_vertex_buffer() const {
    return fullscreen_vertex_buffer;
}

RenderPassHandle GpuDevice::get_swapchain_pass() const {
    return swapchain_render_pass;
}

FramebufferHandle GpuDevice::get_current_framebuffer() const {
    return vulkan_swapchain_framebuffers[ vulkan_image_index ];
}

TextureHandle GpuDevice::get_dummy_texture() const {
    return dummy_texture;
}

BufferHandle GpuDevice::get_dummy_constant_buffer() const {
    return dummy_constant_buffer;
}

void GpuDevice::resize( u16 width, u16 height ) {
    swapchain_width = width;
    swapchain_height = height;

    resized = true;
}


// Resource Access ////////////////////////////////////////////////////////
ShaderState* GpuDevice::access_shader_state( ShaderStateHandle shader ) {
    return (ShaderState*)shaders.access_resource( shader.index );
}

const ShaderState* GpuDevice::access_shader_state( ShaderStateHandle shader ) const {
    return (const ShaderState*)shaders.access_resource( shader.index );
}

Texture* GpuDevice::access_texture( TextureHandle texture ) {
    return (Texture*)textures.access_resource( texture.index );
}

const Texture * GpuDevice::access_texture( TextureHandle texture ) const {
    return (const Texture*)textures.access_resource( texture.index );
}

Buffer* GpuDevice::access_buffer( BufferHandle buffer ) {
    return (Buffer*)buffers.access_resource( buffer.index );
}

const Buffer* GpuDevice::access_buffer( BufferHandle buffer ) const {
    return (const Buffer*)buffers.access_resource( buffer.index );
}

Pipeline* GpuDevice::access_pipeline( PipelineHandle pipeline ) {
    return (Pipeline*)pipelines.access_resource( pipeline.index );
}

const Pipeline* GpuDevice::access_pipeline( PipelineHandle pipeline ) const {
    return (const Pipeline*)pipelines.access_resource( pipeline.index );
}

Sampler* GpuDevice::access_sampler( SamplerHandle sampler ) {
    return (Sampler*)samplers.access_resource( sampler.index );
}

const Sampler* GpuDevice::access_sampler( SamplerHandle sampler ) const {
    return (const Sampler*)samplers.access_resource( sampler.index );
}

DescriptorSetLayout* GpuDevice::access_descriptor_set_layout( DescriptorSetLayoutHandle descriptor_set_layout ) {
    return (DescriptorSetLayout*)descriptor_set_layouts.access_resource( descriptor_set_layout.index );
}

const DescriptorSetLayout* GpuDevice::access_descriptor_set_layout( DescriptorSetLayoutHandle descriptor_set_layout ) const {
    return (const DescriptorSetLayout*)descriptor_set_layouts.access_resource( descriptor_set_layout.index );
}

DescriptorSetLayoutHandle GpuDevice::get_descriptor_set_layout( PipelineHandle pipeline_handle, int layout_index ) {
    Pipeline* pipeline = access_pipeline( pipeline_handle );
    RASSERT( pipeline != nullptr );

    return  pipeline->descriptor_set_layout_handles[ layout_index ];
}

DescriptorSetLayoutHandle GpuDevice::get_descriptor_set_layout( PipelineHandle pipeline_handle, int layout_index ) const {
    const Pipeline* pipeline = access_pipeline( pipeline_handle );
    RASSERT( pipeline != nullptr );

    return  pipeline->descriptor_set_layout_handles[ layout_index ];
}

DescriptorSet* GpuDevice::access_descriptor_set( DescriptorSetHandle descriptor_set ) {
    return (DescriptorSet*)descriptor_sets.access_resource( descriptor_set.index );
}

const DescriptorSet* GpuDevice::access_descriptor_set( DescriptorSetHandle descriptor_set ) const {
    return (const DescriptorSet*)descriptor_sets.access_resource( descriptor_set.index );
}

RenderPass* GpuDevice::access_render_pass( RenderPassHandle render_pass ) {
    return (RenderPass*)render_passes.access_resource( render_pass.index );
}

const RenderPass* GpuDevice::access_render_pass( RenderPassHandle render_pass ) const {
    return (const RenderPass*)render_passes.access_resource( render_pass.index );
}

Framebuffer* GpuDevice::access_framebuffer( FramebufferHandle framebuffer )
{
    return (Framebuffer*)framebuffers.access_resource( framebuffer.index );
}

const Framebuffer* GpuDevice::access_framebuffer( FramebufferHandle framebuffer ) const
{
    return (Framebuffer*)framebuffers.access_resource( framebuffer.index );
}

// GpuDeviceCreation //////////////////////////////////////////////////////
GpuDeviceCreation& GpuDeviceCreation::set_window( u32 width_, u32 height_, void* handle ) {
    width = ( u16 )width_;
    height = ( u16 )height_;
    window = handle;
    return *this;
}

GpuDeviceCreation& GpuDeviceCreation::set_allocator( Allocator* allocator_ ) {
    allocator = allocator_;
    return *this;
}

GpuDeviceCreation& GpuDeviceCreation::set_linear_allocator( StackAllocator* allocator ) {
    temporary_allocator = allocator;
    return *this;
}

GpuDeviceCreation& GpuDeviceCreation::set_num_threads( u32 value ) {
    num_threads = value;
    return *this;
}

} // namespace raptor
