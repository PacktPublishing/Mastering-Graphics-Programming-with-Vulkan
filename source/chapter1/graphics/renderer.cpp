
#include "graphics/renderer.hpp"

#include "graphics/command_buffer.hpp"

#include "foundation/memory.hpp"
#include "foundation/file.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

namespace raptor {

// Resource Loaders ///////////////////////////////////////////////////////

struct TextureLoader : public raptor::ResourceLoader {

    Resource*                       get( cstring name ) override;
    Resource*                       get( u64 hashed_name ) override;

    Resource*                       unload( cstring name ) override;

    Resource*                       create_from_file( cstring name, cstring filename, ResourceManager* resource_manager ) override;

    Renderer*                       renderer;
}; // struct TextureLoader

struct BufferLoader : public raptor::ResourceLoader {

    Resource*                       get( cstring name ) override;
    Resource*                       get( u64 hashed_name ) override;

    Resource*                       unload( cstring name ) override;

    Renderer*                       renderer;
}; // struct BufferLoader

struct SamplerLoader : public raptor::ResourceLoader {

    Resource*                       get( cstring name ) override;
    Resource*                       get( u64 hashed_name ) override;

    Resource*                       unload( cstring name ) override;

    Renderer*                       renderer;
}; // struct SamplerLoader

//
//
static TextureHandle create_texture_from_file( GpuDevice& gpu, cstring filename, cstring name ) {

    if ( filename ) {
        int comp, width, height;
        uint8_t* image_data = stbi_load( filename, &width, &height, &comp, 4 );
        if ( !image_data ) {
            rprint( "Error loading texture %s", filename );
            return k_invalid_texture;
        }

        TextureCreation creation;
        creation.set_data( image_data ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D ).set_flags( 1, 0 ).set_size( ( u16 )width, ( u16 )height, 1 ).set_name( name );

        raptor::TextureHandle new_texture = gpu.create_texture( creation );

        // IMPORTANT:
        // Free memory loaded from file, it should not matter!
        free( image_data );

        return new_texture;
    }

    return k_invalid_texture;
}


// Renderer /////////////////////////////////////////////////////////////////////

u64 TextureResource::k_type_hash = 0;
u64 BufferResource::k_type_hash = 0;
u64 SamplerResource::k_type_hash = 0;

static TextureLoader s_texture_loader;
static BufferLoader s_buffer_loader;
static SamplerLoader s_sampler_loader;

static Renderer s_renderer;

Renderer* Renderer::instance() {
    return &s_renderer;
}

void Renderer::init( const RendererCreation& creation ) {

    rprint( "Renderer init\n" );

    gpu = creation.gpu;

    width = gpu->swapchain_width;
    height = gpu->swapchain_height;

    textures.init( creation.allocator, 512 );
    buffers.init( creation.allocator, 4096 );
    samplers.init( creation.allocator, 128 );

    resource_cache.init( creation.allocator );

    // Init resource hashes
    TextureResource::k_type_hash = hash_calculate( TextureResource::k_type );
    BufferResource::k_type_hash = hash_calculate( BufferResource::k_type );
    SamplerResource::k_type_hash = hash_calculate( SamplerResource::k_type );

    s_texture_loader.renderer = this;
    s_buffer_loader.renderer = this;
    s_sampler_loader.renderer = this;
}

void Renderer::shutdown() {

    resource_cache.shutdown( this );

    textures.shutdown();
    buffers.shutdown();
    samplers.shutdown();

    rprint( "Renderer shutdown\n" );

    gpu->shutdown();
}

void Renderer::set_loaders( raptor::ResourceManager* manager ) {

    manager->set_loader( TextureResource::k_type, &s_texture_loader );
    manager->set_loader( BufferResource::k_type, &s_buffer_loader );
    manager->set_loader( SamplerResource::k_type, &s_sampler_loader );
}

void Renderer::begin_frame() {
    gpu->new_frame();
}

void Renderer::end_frame() {
    // Present
    gpu->present();
}

void Renderer::resize_swapchain( u32 width_, u32 height_ ) {
    gpu->resize( (u16)width_, (u16)height_ );

    width = gpu->swapchain_width;
    height = gpu->swapchain_height;
}

f32 Renderer::aspect_ratio() const {
    return gpu->swapchain_width * 1.f / gpu->swapchain_height;
}

BufferResource* Renderer::create_buffer( const BufferCreation& creation ) {

    BufferResource* buffer = buffers.obtain();
    if ( buffer ) {
        BufferHandle handle = gpu->create_buffer( creation );
        buffer->handle = handle;
        buffer->name = creation.name;
        gpu->query_buffer( handle, buffer->desc );

        if ( creation.name != nullptr ) {
            resource_cache.buffers.insert( hash_calculate( creation.name ), buffer );
        }

        buffer->references = 1;

        return buffer;
    }
    return nullptr;
}

BufferResource* Renderer::create_buffer( VkBufferUsageFlags type, ResourceUsageType::Enum usage, u32 size, void* data, cstring name ) {
    BufferCreation creation{ type, usage, size, data, name };
    return create_buffer( creation );
}

TextureResource* Renderer::create_texture( const TextureCreation& creation ) {
    TextureResource* texture = textures.obtain();

    if ( texture ) {
        TextureHandle handle = gpu->create_texture( creation );
        texture->handle = handle;
        texture->name = creation.name;
        gpu->query_texture( handle, texture->desc );

        if ( creation.name != nullptr ) {
            resource_cache.textures.insert( hash_calculate( creation.name ), texture );
        }

        texture->references = 1;

        return texture;
    }
    return nullptr;
}

TextureResource* Renderer::create_texture( cstring name, cstring filename ) {
    TextureResource* texture = textures.obtain();

    if ( texture ) {
        TextureHandle handle = create_texture_from_file( *gpu, filename, name );
        texture->handle = handle;
        gpu->query_texture( handle, texture->desc );
        texture->references = 1;
        texture->name = name;

        resource_cache.textures.insert( hash_calculate( name ), texture );

        return texture;
    }
    return nullptr;
}

SamplerResource* Renderer::create_sampler( const SamplerCreation& creation ) {
    SamplerResource* sampler = samplers.obtain();
    if ( sampler ) {
        SamplerHandle handle = gpu->create_sampler( creation );
        sampler->handle = handle;
        sampler->name = creation.name;
        gpu->query_sampler( handle, sampler->desc );

        if ( creation.name != nullptr ) {
            resource_cache.samplers.insert( hash_calculate( creation.name ), sampler );
        }

        sampler->references = 1;

        return sampler;
    }
    return nullptr;
}

void Renderer::destroy_buffer( BufferResource* buffer ) {
    if ( !buffer ) {
        return;
    }

    buffer->remove_reference();
    if ( buffer->references ) {
        return;
    }

    resource_cache.buffers.remove( hash_calculate( buffer->desc.name ) );
    gpu->destroy_buffer( buffer->handle );
    buffers.release( buffer );
}

void Renderer::destroy_texture( TextureResource* texture ) {
    if ( !texture ) {
        return;
    }

    texture->remove_reference();
    if ( texture->references ) {
        return;
    }

    resource_cache.textures.remove( hash_calculate( texture->desc.name ) );
    gpu->destroy_texture( texture->handle );
    textures.release( texture );
}

void Renderer::destroy_sampler( SamplerResource* sampler ) {
    if ( !sampler ) {
        return;
    }

    sampler->remove_reference();
    if ( sampler->references ) {
        return;
    }

    resource_cache.samplers.remove( hash_calculate( sampler->desc.name ) );
    gpu->destroy_sampler( sampler->handle );
    samplers.release( sampler );
}

void* Renderer::map_buffer( BufferResource* buffer, u32 offset, u32 size ) {

    MapBufferParameters cb_map = { buffer->handle, offset, size };
    return gpu->map_buffer( cb_map );
}

void Renderer::unmap_buffer( BufferResource* buffer ) {

    if ( buffer->desc.parent_handle.index == k_invalid_index ) {
        MapBufferParameters cb_map = { buffer->handle, 0, 0 };
        gpu->unmap_buffer( cb_map );
    }
}

// Resource Loaders ///////////////////////////////////////////////////////

// Texture Loader /////////////////////////////////////////////////////////
Resource* TextureLoader::get( cstring name ) {
    const u64 hashed_name = hash_calculate( name );
    return renderer->resource_cache.textures.get( hashed_name );
}

Resource* TextureLoader::get( u64 hashed_name ) {
    return renderer->resource_cache.textures.get( hashed_name );
}

Resource* TextureLoader::unload( cstring name ) {
    const u64 hashed_name = hash_calculate( name );
    TextureResource* texture = renderer->resource_cache.textures.get( hashed_name );
    if ( texture ) {
        renderer->destroy_texture( texture );
    }
    return nullptr;
}

Resource* TextureLoader::create_from_file( cstring name, cstring filename, ResourceManager* resource_manager ) {
    return renderer->create_texture( name, filename );
}

// BufferLoader //////////////////////////////////////////////////////////
Resource* BufferLoader::get( cstring name ) {
    const u64 hashed_name = hash_calculate( name );
    return renderer->resource_cache.buffers.get( hashed_name );
}

Resource* BufferLoader::get( u64 hashed_name ) {
    return renderer->resource_cache.buffers.get( hashed_name );
}

Resource* BufferLoader::unload( cstring name ) {
    const u64 hashed_name = hash_calculate( name );
    BufferResource* buffer = renderer->resource_cache.buffers.get( hashed_name );
    if ( buffer ) {
        renderer->destroy_buffer( buffer );
    }

    return nullptr;
}

// SamplerLoader /////////////////////////////////////////////////////////
Resource* SamplerLoader::get( cstring name ) {
    const u64 hashed_name = hash_calculate( name );
    return renderer->resource_cache.samplers.get( hashed_name );
}

Resource* SamplerLoader::get( u64 hashed_name ) {
    return renderer->resource_cache.samplers.get( hashed_name );
}

Resource* SamplerLoader::unload( cstring name ) {
    const u64 hashed_name = hash_calculate( name );
    SamplerResource* sampler = renderer->resource_cache.samplers.get( hashed_name );
    if ( sampler ) {
        renderer->destroy_sampler( sampler );
    }
    return nullptr;
}

// ResourceCache
void ResourceCache::init( Allocator* allocator ) {
    // Init resources caching
    textures.init( allocator, 16 );
    buffers.init( allocator, 16 );
    samplers.init( allocator, 16 );
}

void ResourceCache::shutdown( Renderer* renderer ) {

    raptor::FlatHashMapIterator it = textures.iterator_begin();

    while ( it.is_valid() ) {
        raptor::TextureResource* texture = textures.get( it );
        renderer->destroy_texture( texture );

        textures.iterator_advance( it );
    }

    it = buffers.iterator_begin();

    while ( it.is_valid() ) {
        raptor::BufferResource* buffer = buffers.get( it );
        renderer->destroy_buffer( buffer );

        buffers.iterator_advance( it );
    }

    it = samplers.iterator_begin();

    while ( it.is_valid() ) {
        raptor::SamplerResource* sampler = samplers.get( it );
        renderer->destroy_sampler( sampler );

        samplers.iterator_advance( it );
    }

    textures.shutdown();
    buffers.shutdown();
    samplers.shutdown();
}

} // namespace raptor
