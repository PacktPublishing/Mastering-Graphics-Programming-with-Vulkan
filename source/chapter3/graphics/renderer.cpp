
#include "graphics/renderer.hpp"

#include "graphics/command_buffer.hpp"

#include "foundation/memory.hpp"
#include "foundation/file.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"
#include "external/imgui/imgui.h"

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

// MaterialCreation ///////////////////////////////////////////////////////
MaterialCreation& MaterialCreation::reset() {
    program = nullptr;
    name = nullptr;
    render_index = ~0u;
    return *this;
}

MaterialCreation& MaterialCreation::set_program( Program* program_ ) {
    program = program_;
    return *this;
}

MaterialCreation& MaterialCreation::set_render_index( u32 render_index_ ) {
    render_index = render_index_;
    return *this;
}

MaterialCreation& MaterialCreation::set_name( cstring name_ ) {
    name = name_;
    return *this;
}

//
//
static TextureHandle create_texture_from_file( GpuDevice& gpu, cstring filename, cstring name, bool create_mipmaps ) {

    if ( filename ) {
        int comp, width, height;
        uint8_t* image_data = stbi_load( filename, &width, &height, &comp, 4 );
        if ( !image_data ) {
            rprint( "Error loading texture %s", filename );
            return k_invalid_texture;
        }

        u32 mip_levels = 1;
        if ( create_mipmaps ) {
            u32 w = width;
            u32 h = height;

            while (w > 1 && h > 1) {
                w /= 2;
                h /= 2;

                ++mip_levels;
            }
        }

        TextureCreation creation;
        creation.set_data( image_data ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D ).set_flags( mip_levels, 0 ).set_size( ( u16 )width, ( u16 )height, 1 ).set_name( name );

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
u64 Program::k_type_hash = 0;
u64 Material::k_type_hash = 0;

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

    textures.init( creation.allocator, k_textures_pool_size );
    buffers.init( creation.allocator, k_buffers_pool_size );
    samplers.init( creation.allocator, k_samplers_pool_size );
    programs.init( creation.allocator, k_pipelines_pool_size );
    materials.init( creation.allocator, 128 );

    resource_cache.init( creation.allocator );

    // Init resource hashes
    TextureResource::k_type_hash = hash_calculate( TextureResource::k_type );
    BufferResource::k_type_hash = hash_calculate( BufferResource::k_type );
    SamplerResource::k_type_hash = hash_calculate( SamplerResource::k_type );
    Program::k_type_hash = hash_calculate( Program::k_type );
    Material::k_type_hash = hash_calculate( Material::k_type );

    s_texture_loader.renderer = this;
    s_buffer_loader.renderer = this;
    s_sampler_loader.renderer = this;

    const u32 gpu_heap_counts = gpu->get_memory_heap_count();
    gpu_heap_budgets.init( gpu->allocator, gpu_heap_counts, gpu_heap_counts );
}

void Renderer::shutdown() {

    resource_cache.shutdown( this );
    gpu_heap_budgets.shutdown();

    textures.shutdown();
    buffers.shutdown();
    samplers.shutdown();
    materials.shutdown();
    programs.shutdown();

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

void Renderer::imgui_draw() {

    // Print memory stats
    vmaGetHeapBudgets( gpu->vma_allocator, gpu_heap_budgets.data );

    sizet total_memory_used = 0;
    for ( u32 i = 0; i < gpu->get_memory_heap_count(); ++i ) {
        total_memory_used += gpu_heap_budgets[ i ].usage;
    }

    ImGui::Text( "GPU Memory Total: %lluMB", total_memory_used / ( 1024 * 1024 ) );
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
    BufferCreation creation{ type, usage, size, 0, 0, data, name };
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

TextureResource* Renderer::create_texture( cstring name, cstring filename, bool create_mipmaps ) {
    TextureResource* texture = textures.obtain();

    if ( texture ) {
        TextureHandle handle = create_texture_from_file( *gpu, filename, name, create_mipmaps );
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

Program* Renderer::create_program( const ProgramCreation& creation ) {
    Program* program = programs.obtain();
    if ( program ) {
        const u32 num_passes = 1;
        // First create arrays
        program->passes.init( gpu->allocator, num_passes, num_passes );

        program->name = creation.pipeline_creation.name;

        StringBuffer pipeline_cache_path;
        pipeline_cache_path.init( 1024, gpu->allocator );

        for ( uint32_t i = 0; i < num_passes; ++i ) {
            ProgramPass& pass = program->passes[ i ];

            if ( creation.pipeline_creation.name != nullptr ) {
                char* cache_path = pipeline_cache_path.append_use_f("%s%s.cache", RAPTOR_SHADER_FOLDER, creation.pipeline_creation.name );

                pass.pipeline = gpu->create_pipeline( creation.pipeline_creation, cache_path );
            } else {
                pass.pipeline = gpu->create_pipeline( creation.pipeline_creation );
            }

            pass.descriptor_set_layout = gpu->get_descriptor_set_layout( pass.pipeline, 0 );
        }

        pipeline_cache_path.shutdown();

        if ( creation.pipeline_creation.name != nullptr ) {
            resource_cache.programs.insert( hash_calculate( creation.pipeline_creation.name ), program );
        }

        program->references = 1;

        return program;
    }
    return nullptr;
}

Material* Renderer::create_material( const MaterialCreation& creation ) {
    Material* material = materials.obtain();
    if ( material ) {
        material->program = creation.program;
        material->name = creation.name;
        material->render_index = creation.render_index;

        if ( creation.name != nullptr ) {
            resource_cache.materials.insert( hash_calculate( creation.name ), material );
        }

        material->references = 1;

        return material;
    }
    return nullptr;
}

Material* Renderer::create_material( Program* program, cstring name ) {
    MaterialCreation creation{ program, name };
    return create_material( creation );
}

PipelineHandle Renderer::get_pipeline( Material* material ) {
    RASSERT( material != nullptr );

    return material->program->passes[ 0 ].pipeline;
}

DescriptorSetHandle Renderer::create_descriptor_set( CommandBuffer* gpu_commands, Material* material, DescriptorSetCreation& ds_creation ) {
    RASSERT( material != nullptr );

    DescriptorSetLayoutHandle set_layout = material->program->passes[ 0 ].descriptor_set_layout;

    ds_creation.set_layout( set_layout );

    return gpu_commands->create_descriptor_set( ds_creation );
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

void Renderer::destroy_program( Program* program ) {
    if ( !program ) {
        return;
    }

    program->remove_reference();
    if ( program->references ) {
        return;
    }

    resource_cache.programs.remove( hash_calculate( program->name ) );

    gpu->destroy_pipeline( program->passes[ 0 ].pipeline );
    program->passes.shutdown();

    programs.release( program );
}

void Renderer::destroy_material( Material* material ) {
    if ( !material ) {
        return;
    }

    material->remove_reference();
    if ( material->references ) {
        return;
    }

    resource_cache.materials.remove( hash_calculate( material->name ) );
    materials.release( material );
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

void Renderer::add_texture_to_update( raptor::TextureHandle texture ) {
    std::lock_guard<std::mutex> guard( texture_update_mutex );

    textures_to_update[ num_textures_to_update++ ] = texture;
}

//TODO:

static VkImageLayout add_image_barrier2( VkCommandBuffer command_buffer, VkImage image, raptor::ResourceState old_state, raptor::ResourceState new_state,
                                u32 base_mip_level, u32 mip_count, bool is_depth, u32 source_family, u32 destination_family ) {
    using namespace raptor;
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = image;
    barrier.srcQueueFamilyIndex = source_family;
    barrier.dstQueueFamilyIndex = destination_family;
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

    return barrier.newLayout;
}

static void generate_mipmaps( raptor::Texture* texture, raptor::CommandBuffer* cb, bool from_transfer_queue ) {
    using namespace raptor;

    if ( texture->mipmaps > 1 ) {
        util_add_image_barrier( cb->vk_command_buffer, texture->vk_image, from_transfer_queue ? RESOURCE_STATE_COPY_SOURCE : RESOURCE_STATE_COPY_SOURCE, RESOURCE_STATE_COPY_SOURCE, 0, 1, false );
    }

    i32 w = texture->width;
    i32 h = texture->height;

    for ( int mip_index = 1; mip_index < texture->mipmaps; ++mip_index ) {
        util_add_image_barrier( cb->vk_command_buffer, texture->vk_image, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_COPY_DEST, mip_index, 1, false );

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

        vkCmdBlitImage( cb->vk_command_buffer, texture->vk_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_region, VK_FILTER_LINEAR );

        // Prepare current mip for next level
        util_add_image_barrier( cb->vk_command_buffer, texture->vk_image, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_COPY_SOURCE, mip_index, 1, false );
    }

    // Transition
    if ( from_transfer_queue && false ) {
        util_add_image_barrier( cb->vk_command_buffer, texture->vk_image, ( texture->mipmaps > 1 ) ? RESOURCE_STATE_COPY_SOURCE : RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_SHADER_RESOURCE, 0, texture->mipmaps, false );
    } else {
        util_add_image_barrier( cb->vk_command_buffer, texture->vk_image, RESOURCE_STATE_UNDEFINED, RESOURCE_STATE_SHADER_RESOURCE, 0, texture->mipmaps, false );
    }


    texture->vk_image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}


void Renderer::add_texture_update_commands( u32 thread_id ) {
    std::lock_guard<std::mutex> guard( texture_update_mutex );

    if ( num_textures_to_update == 0 ) {
        return;
    }

    CommandBuffer* cb = gpu->get_command_buffer( thread_id, false );
    cb->begin();

    for ( u32 i = 0; i < num_textures_to_update; ++i ) {

        Texture* texture = gpu->access_texture( textures_to_update[i] );

        texture->vk_image_layout = add_image_barrier2( cb->vk_command_buffer, texture->vk_image, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_COPY_SOURCE,
                            0, 1, false, gpu->vulkan_transfer_queue_family, gpu->vulkan_main_queue_family );

        generate_mipmaps( texture, cb, true );
    }

    // TODO: this is done before submitting to the queue in the device.
    //cb->end();
    gpu->queue_command_buffer( cb );

    num_textures_to_update = 0;
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
    return renderer->create_texture( name, filename, true );
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
    programs.init( allocator, 16 );
    materials.init( allocator, 16 );
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

    it = materials.iterator_begin();

    while ( it.is_valid() ) {
        raptor::Material* material = materials.get( it );
        renderer->destroy_material( material );

        materials.iterator_advance( it );
    }

    it = programs.iterator_begin();

    while ( it.is_valid() ) {
        raptor::Program* program = programs.get( it );
        renderer->destroy_program( program );

        programs.iterator_advance( it );
    }

    textures.shutdown();
    buffers.shutdown();
    samplers.shutdown();
    materials.shutdown();
    programs.shutdown();
}

} // namespace raptor
