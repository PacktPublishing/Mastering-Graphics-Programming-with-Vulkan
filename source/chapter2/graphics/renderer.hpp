#pragma once

#include "graphics/gpu_device.hpp"
#include "graphics/gpu_resources.hpp"

#include "foundation/resource_manager.hpp"

namespace raptor {

struct Renderer;

//
//
struct BufferResource : public raptor::Resource {

    BufferHandle                    handle;
    u32                             pool_index;
    BufferDescription               desc;

    static constexpr cstring        k_type = "raptor_buffer_type";
    static u64                      k_type_hash;

}; // struct Buffer

//
//
struct TextureResource : public raptor::Resource {

    TextureHandle                   handle;
    u32                             pool_index;
    TextureDescription              desc;

    static constexpr cstring        k_type = "raptor_texture_type";
    static u64                      k_type_hash;

}; // struct Texture

//
//
struct SamplerResource : public raptor::Resource {

    SamplerHandle                   handle;
    u32                             pool_index;
    SamplerDescription              desc;

    static constexpr cstring        k_type = "raptor_sampler_type";
    static u64                      k_type_hash;

}; // struct Sampler

// Material/Shaders /////////////////////////////////////////////////////////////

//
//
struct ProgramPass {

    PipelineHandle                  pipeline;
    DescriptorSetLayoutHandle       descriptor_set_layout;
}; // struct ProgramPass

//
//
struct ProgramCreation {

    // NOTE(marco): not much benefit having this abstraction for now,
    // but it will become more powerful soon
    PipelineCreation                pipeline_creation;

}; // struct ProgramCreation

//
//
struct Program : public raptor::Resource {

    u32                             get_num_passes() const;

    Array<ProgramPass>              passes;

    u32                             pool_index;

    static constexpr cstring        k_type = "raptor_program_type";
    static u64                      k_type_hash;

}; // struct Program

//
//
struct MaterialCreation {

    MaterialCreation&               reset();
    MaterialCreation&               set_program( Program* program );
    MaterialCreation&               set_name( cstring name );
    MaterialCreation&               set_render_index( u32 render_index );

    Program*                        program          = nullptr;
    cstring                         name             = nullptr;
    u32                             render_index     = ~0u;

}; // struct MaterialCreation

//
//
struct Material : public raptor::Resource {

    Program*                        program;

    u32                             render_index;

    u32                             pool_index;

    static constexpr cstring        k_type = "raptor_material_type";
    static u64                      k_type_hash;

}; // struct Material

// ResourceCache ////////////////////////////////////////////////////////////////

//
//
struct ResourceCache {

    void                            init( Allocator* allocator );
    void                            shutdown( Renderer* renderer );

    FlatHashMap<u64, TextureResource*> textures;
    FlatHashMap<u64, BufferResource*>  buffers;
    FlatHashMap<u64, SamplerResource*> samplers;
    FlatHashMap<u64, Program*>         programs;
    FlatHashMap<u64, Material*>        materials;

}; // struct ResourceCache

// Renderer /////////////////////////////////////////////////////////////////////


struct RendererCreation {

    raptor::GpuDevice*          gpu;
    Allocator*                  allocator;

}; // struct RendererCreation

//
// Main class responsible for handling all high level resources
//
struct Renderer : public Service {

    RAPTOR_DECLARE_SERVICE( Renderer );

    void                        init( const RendererCreation& creation );
    void                        shutdown();

    void                        set_loaders( raptor::ResourceManager* manager );

    void                        begin_frame();
    void                        end_frame();

    void                        resize_swapchain( u32 width, u32 height );

    f32                         aspect_ratio() const;

    // Creation/destruction
    BufferResource*             create_buffer( const BufferCreation& creation );
    BufferResource*             create_buffer( VkBufferUsageFlags type, ResourceUsageType::Enum usage, u32 size, void* data, cstring name );

    TextureResource*            create_texture( const TextureCreation& creation );
    TextureResource*            create_texture( cstring name, cstring filename, bool create_mipmaps );

    SamplerResource*            create_sampler( const SamplerCreation& creation );

    Program*                    create_program( const ProgramCreation& creation );

    Material*                   create_material( const MaterialCreation& creation );
    Material*                   create_material( Program* program, cstring name );

    // Draw
    PipelineHandle              get_pipeline( Material* material );
    DescriptorSetHandle         create_descriptor_set( CommandBuffer* gpu_commands, Material* material, DescriptorSetCreation& ds_creation );

    void                        destroy_buffer( BufferResource* buffer );
    void                        destroy_texture( TextureResource* texture );
    void                        destroy_sampler( SamplerResource* sampler );
    void                        destroy_program( Program* program );
    void                        destroy_material( Material* material );

    // Update resources
    void*                       map_buffer( BufferResource* buffer, u32 offset = 0, u32 size = 0 );
    void                        unmap_buffer( BufferResource* buffer );

    CommandBuffer*              get_command_buffer( QueueType::Enum type, bool begin )  { return gpu->get_command_buffer( type, begin ); }
    void                        queue_command_buffer( raptor::CommandBuffer* commands ) { gpu->queue_command_buffer( commands ); }

    ResourcePoolTyped<TextureResource>  textures;
    ResourcePoolTyped<BufferResource>   buffers;
    ResourcePoolTyped<SamplerResource>  samplers;
    ResourcePoolTyped<Program>          programs;
    ResourcePoolTyped<Material>         materials;

    ResourceCache               resource_cache;

    raptor::GpuDevice*          gpu;

    u16                         width;
    u16                         height;

    static constexpr cstring    k_name = "raptor_rendering_service";

}; // struct Renderer

} // namespace raptor
