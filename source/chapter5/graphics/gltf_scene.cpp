#include "graphics/gltf_scene.hpp"
#include "graphics/gpu_profiler.hpp"
#include "graphics/raptor_imgui.hpp"
#include "graphics/asynchronous_loader.hpp"
#include "graphics/scene_graph.hpp"

#include "foundation/file.hpp"
#include "foundation/time.hpp"
#include "foundation/numerics.hpp"

#include "external/imgui/imgui.h"
#include "external/stb_image.h"

#include "external/cglm/struct/affine.h"
#include "external/cglm/struct/mat4.h"
#include "external/cglm/struct/vec3.h"
#include "external/cglm/struct/quat.h"

#include "external/tracy/tracy/Tracy.hpp"


namespace raptor {

//
// glTFScene //////////////////////////////////////////////////////////////

void glTFScene::get_mesh_vertex_buffer( i32 accessor_index, u32 flag, BufferHandle& out_buffer_handle, u32& out_buffer_offset, u32& out_flags ) {
    if ( accessor_index != -1 ) {
        glTF::Accessor& buffer_accessor = gltf_scene.accessors[ accessor_index ];
        glTF::BufferView& buffer_view = gltf_scene.buffer_views[ buffer_accessor.buffer_view ];
        BufferResource& buffer_gpu = buffers[ buffer_view.buffer ];

        out_buffer_handle = buffer_gpu.handle;
        out_buffer_offset = glTF::get_data_offset( buffer_accessor.byte_offset, buffer_view.byte_offset );

        out_flags |= flag;
    }
}

void glTFScene::fill_pbr_material( Renderer& renderer, glTF::Material& material, PBRMaterial& pbr_material ) {
    GpuDevice& gpu = *renderer.gpu;

    // Handle flags
    if ( material.alpha_mode.data != nullptr && strcmp( material.alpha_mode.data, "MASK" ) == 0 ) {
        pbr_material.flags |= DrawFlags_AlphaMask;
    } else if ( material.alpha_mode.data != nullptr && strcmp( material.alpha_mode.data, "BLEND" ) == 0 ) {
        // TODO: how to choose when using dithering and traditional blending ?
        pbr_material.flags |= DrawFlags_Transparent;
        //pbr_material.flags |= DrawFlags_AlphaDither;
    }

    pbr_material.flags |= material.double_sided ? DrawFlags_DoubleSided : 0;
    // Alpha cutoff
    pbr_material.alpha_cutoff = material.alpha_cutoff != glTF::INVALID_FLOAT_VALUE ? material.alpha_cutoff : 1.f;

    if ( material.pbr_metallic_roughness != nullptr ) {
        if ( material.pbr_metallic_roughness->base_color_factor_count != 0 ) {
            RASSERT( material.pbr_metallic_roughness->base_color_factor_count == 4 );

            memcpy( pbr_material.base_color_factor.raw, material.pbr_metallic_roughness->base_color_factor, sizeof( vec4s ) );
        } else {
            pbr_material.base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
        }

        pbr_material.metallic_roughness_occlusion_factor.x = material.pbr_metallic_roughness->roughness_factor != glTF::INVALID_FLOAT_VALUE ? material.pbr_metallic_roughness->roughness_factor : 1.f;
        pbr_material.metallic_roughness_occlusion_factor.y = material.pbr_metallic_roughness->metallic_factor != glTF::INVALID_FLOAT_VALUE ? material.pbr_metallic_roughness->metallic_factor : 1.f;

        pbr_material.diffuse_texture_index = get_material_texture( gpu, material.pbr_metallic_roughness->base_color_texture );
        pbr_material.roughness_texture_index = get_material_texture( gpu, material.pbr_metallic_roughness->metallic_roughness_texture );
    }

    if ( material.emissive_texture != nullptr ) {
        pbr_material.emissive_texture_index = get_material_texture( gpu, material.emissive_texture );
    }

    if ( material.emissive_factor_count != 0 ) {
        RASSERT( material.emissive_factor_count == 3 );

        memcpy( pbr_material.emissive_factor.raw, material.emissive_factor, sizeof( vec3s ) );
    } else {
        pbr_material.emissive_factor = { 0.0f, 0.0f, 0.0f };
    }

    pbr_material.occlusion_texture_index = get_material_texture( gpu, ( material.occlusion_texture != nullptr ) ? material.occlusion_texture->index : -1 );
    pbr_material.normal_texture_index = get_material_texture( gpu, ( material.normal_texture != nullptr ) ? material.normal_texture->index : -1 );

    if ( material.occlusion_texture != nullptr ) {
        if ( material.occlusion_texture->strength != glTF::INVALID_FLOAT_VALUE ) {
            pbr_material.metallic_roughness_occlusion_factor.z = material.occlusion_texture->strength;
        } else {
            pbr_material.metallic_roughness_occlusion_factor.z = 1.0f;
        }
    }
}

u16 glTFScene::get_material_texture( GpuDevice& gpu, glTF::TextureInfo* texture_info ) {
    if ( texture_info != nullptr ) {
        glTF::Texture& gltf_texture = gltf_scene.textures[ texture_info->index ];
        TextureResource& texture_gpu = images[ gltf_texture.source ];
        SamplerResource& sampler_gpu = samplers[ gltf_texture.sampler ];

        gpu.link_texture_sampler( texture_gpu.handle, sampler_gpu.handle );

        return texture_gpu.handle.index;
    }
    else {
        return k_invalid_scene_texture_index;
    }
}

u16 glTFScene::get_material_texture( GpuDevice& gpu, i32 gltf_texture_index ) {
    if ( gltf_texture_index >= 0 ) {
        glTF::Texture& gltf_texture = gltf_scene.textures[ gltf_texture_index ];
        TextureResource& texture_gpu = images[ gltf_texture.source ];
        SamplerResource& sampler_gpu = samplers[ gltf_texture.sampler ];

        gpu.link_texture_sampler( texture_gpu.handle, sampler_gpu.handle );

        return texture_gpu.handle.index;
    } else {
        return k_invalid_scene_texture_index;
    }
}

void glTFScene::init( cstring filename, cstring path, Allocator* resident_allocator_, StackAllocator* temp_allocator, AsynchronousLoader* async_loader ) {

    resident_allocator = resident_allocator_;
    renderer = async_loader->renderer;
    enki::TaskScheduler* task_scheduler = async_loader->task_scheduler;
    sizet temp_allocator_initial_marker = temp_allocator->get_marker();

    // Time statistics
    i64 start_scene_loading = time_now();

    gltf_scene = gltf_load_file( filename );

    i64 end_loading_file = time_now();

    // Load all textures
    images.init( resident_allocator, gltf_scene.images_count );

    Array<TextureCreation> tcs;
    tcs.init( temp_allocator, gltf_scene.images_count, gltf_scene.images_count );

    StringBuffer temp_name_buffer;
    temp_name_buffer.init( 4096, temp_allocator );

    for ( u32 image_index = 0; image_index < gltf_scene.images_count; ++image_index ) {
        glTF::Image& image = gltf_scene.images[ image_index ];

        int comp, width, height;

        stbi_info( image.uri.data, &width, &height, &comp );

        u32 mip_levels = 1;
        if ( true ) {
            u32 w = width;
            u32 h = height;

            while ( w > 1 && h > 1 ) {
                w /= 2;
                h /= 2;

                ++mip_levels;
            }
        }

        TextureCreation tc;
        tc.set_data( nullptr ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D ).set_flags( mip_levels, 0 ).set_size( ( u16 )width, ( u16 )height, 1 ).set_name( image.uri.data );
        TextureResource* tr = renderer->create_texture( tc );
        RASSERT( tr != nullptr );

        images.push( *tr );

        // Reconstruct file path
        char* full_filename = temp_name_buffer.append_use_f( "%s%s", path, image.uri.data );
        async_loader->request_texture_data( full_filename, tr->handle );
        // Reset name buffer
        temp_name_buffer.clear();
    }

    i64 end_loading_textures_files = time_now();

    i64 end_creating_textures = time_now();

    names_buffer.init( rkilo( 64 ), resident_allocator );

    // Load all samplers
    samplers.init( resident_allocator, gltf_scene.samplers_count );

    for ( u32 sampler_index = 0; sampler_index < gltf_scene.samplers_count; ++sampler_index ) {
        glTF::Sampler& sampler = gltf_scene.samplers[ sampler_index ];

        char* sampler_name = names_buffer.append_use_f( "sampler_%u", sampler_index );

        SamplerCreation creation;
        switch ( sampler.min_filter ) {
            case glTF::Sampler::NEAREST:
                creation.min_filter = VK_FILTER_NEAREST;
                break;
            case glTF::Sampler::LINEAR:
                creation.min_filter = VK_FILTER_LINEAR;
                break;
            case glTF::Sampler::LINEAR_MIPMAP_NEAREST:
                creation.min_filter = VK_FILTER_LINEAR;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                break;
            case glTF::Sampler::LINEAR_MIPMAP_LINEAR:
                creation.min_filter = VK_FILTER_LINEAR;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                break;
            case glTF::Sampler::NEAREST_MIPMAP_NEAREST:
                creation.min_filter = VK_FILTER_NEAREST;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                break;
            case glTF::Sampler::NEAREST_MIPMAP_LINEAR:
                creation.min_filter = VK_FILTER_NEAREST;
                creation.mip_filter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                break;
        }

        creation.mag_filter = sampler.mag_filter == glTF::Sampler::Filter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

        switch ( sampler.wrap_s ) {
            case glTF::Sampler::CLAMP_TO_EDGE:
                creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                break;
            case glTF::Sampler::MIRRORED_REPEAT:
                creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                break;
            case glTF::Sampler::REPEAT:
                creation.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                break;
        }

        switch ( sampler.wrap_t ) {
            case glTF::Sampler::CLAMP_TO_EDGE:
                creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                break;
            case glTF::Sampler::MIRRORED_REPEAT:
                creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                break;
            case glTF::Sampler::REPEAT:
                creation.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                break;
        }

        creation.name = sampler_name;

        SamplerResource* sr = renderer->create_sampler( creation );
        RASSERT( sr != nullptr );

        samplers.push( *sr );
    }

    i64 end_creating_samplers = time_now();

    // Temporary array of buffer data
    Array<void*> buffers_data;
    buffers_data.init( resident_allocator, gltf_scene.buffers_count );

    for ( u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count; ++buffer_index ) {
        glTF::Buffer& buffer = gltf_scene.buffers[ buffer_index ];

        FileReadResult buffer_data = file_read_binary( buffer.uri.data, resident_allocator );
        buffers_data.push( buffer_data.data );
    }

    i64 end_reading_buffers_data = time_now();

    // Load all buffers and initialize them with buffer data
    buffers.init( resident_allocator, gltf_scene.buffer_views_count );

    for ( u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count; ++buffer_index ) {

        glTF::Buffer& buffer = gltf_scene.buffers[ buffer_index ];

        VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        char* buffer_name = names_buffer.append_use_f( "buffer_%u", buffer_index );

        u8* buffer_data = ( u8* )buffers_data[ buffer_index ];
        BufferResource* br = renderer->create_buffer( flags, ResourceUsageType::Immutable, buffer.byte_length, buffer_data, buffer_name );
        buffers.push( *br );
    }

    // Before unloading buffer data, load animations
    animations.init( resident_allocator, gltf_scene.animations_count );

    for ( u32 animation_index = 0; animation_index < gltf_scene.animations_count; ++animation_index ) {
        glTF::Animation& gltf_animation = gltf_scene.animations[ animation_index ];

        Animation& animation = animations.push_use();
        animation.time_start = FLT_MAX;
        animation.time_end = -FLT_MAX;
        animation.channels.init( resident_allocator, gltf_animation.channels_count, gltf_animation.channels_count );
        for ( u32 channel_index = 0; channel_index < gltf_animation.channels_count; ++channel_index ) {

            glTF::AnimationChannel& gltf_channel = gltf_animation.channels[ channel_index ];
            AnimationChannel& channel = animation.channels[ channel_index ];

            channel.sampler = gltf_channel.sampler;
            channel.target_node = gltf_channel.target_node;
            channel.target_type = (raptor::AnimationChannel::TargetType)gltf_channel.target_type;
        }

        animation.samplers.init( resident_allocator, gltf_animation.samplers_count, gltf_animation.samplers_count );
        for ( u32 sampler_index = 0; sampler_index < gltf_animation.samplers_count; ++sampler_index ) {

            glTF::AnimationSampler& gltf_sampler = gltf_animation.samplers[ sampler_index ];
            AnimationSampler& sampler = animation.samplers[ sampler_index ];

            sampler.interpolation_type = ( raptor::AnimationSampler::Interpolation )gltf_sampler.interpolation;

            i32 key_frames_count = 0;

            // Copy keyframe data
            {
                glTF::Accessor& buffer_accessor = gltf_scene.accessors[ gltf_sampler.input_keyframe_buffer_index ];
                glTF::BufferView& buffer_view = gltf_scene.buffer_views[ buffer_accessor.buffer_view ];

                i32 byte_offset = glTF::get_data_offset( buffer_accessor.byte_offset, buffer_view.byte_offset );

                u8* buffer_data = ( u8* )buffers_data[ buffer_view.buffer ] + byte_offset;
                sampler.key_frames.init( resident_allocator, buffer_accessor.count, buffer_accessor.count );

                const f32* key_frames = ( const f32* )buffer_data;
                for ( u32 i = 0; i < buffer_accessor.count; ++i ) {
                    sampler.key_frames[ i ] = key_frames[ i ];

                    animation.time_start = glm_min( animation.time_start, key_frames[ i ] );
                    animation.time_end = glm_max( animation.time_end, key_frames[ i ] );
                }

                key_frames_count = buffer_accessor.count;
            }
            // Copy animation data
            {
                glTF::Accessor& buffer_accessor = gltf_scene.accessors[ gltf_sampler.output_keyframe_buffer_index ];
                glTF::BufferView& buffer_view = gltf_scene.buffer_views[ buffer_accessor.buffer_view ];

                i32 byte_offset = glTF::get_data_offset( buffer_accessor.byte_offset, buffer_view.byte_offset );

                RASSERT( buffer_accessor.count == key_frames_count );

                u8* buffer_data = ( u8* )buffers_data[ buffer_view.buffer ] + byte_offset;

                sampler.data = (vec4s*)rallocaa( sizeof(vec4s) * buffer_accessor.count, resident_allocator, 16 );

                switch ( buffer_accessor.type ) {
                    case glTF::Accessor::Vec3:
                    {
                        const vec3s* animation_data = ( const vec3s* )buffer_data;
                        for ( u32 i = 0; i < buffer_accessor.count; ++i ) {
                            sampler.data[ i ] = glms_vec4( animation_data[ i ], 0.f );
                        }
                        break;
                    }
                    case glTF::Accessor::Vec4:
                    {
                        const f32* animation_data = ( const f32* )buffer_data;
                        for ( u32 i = 0; i < buffer_accessor.count; ++i ) {
                            sampler.data[ i ] = vec4s{ animation_data[ i * 4 ], animation_data[ i * 4 + 1 ], animation_data[ i * 4 + 2 ], animation_data[ i * 4 + 3 ] };
                        }
                        break;
                    }
                    default:
                    {
                        RASSERT( false );
                        break;
                    }
                }

            }
        }

        //rprint( "Done loading animation %f %f\n", animation.time_start, animation.time_end );
    }

    // Load skins
    skins.init( resident_allocator, gltf_scene.skins_count );

    for ( u32 si = 0; si < gltf_scene.skins_count; ++si ) {
        glTF::Skin& gltf_skin = gltf_scene.skins[ si ];

        Skin& skin = skins.push_use();
        skin.skeleton_root_index = gltf_skin.skeleton_root_node_index;

        // Copy joints
        skin.joints.init( resident_allocator, gltf_skin.joints_count, gltf_skin.joints_count );
        memory_copy( skin.joints.data, gltf_skin.joints, sizeof( i32 ) * gltf_skin.joints_count );

        // Copy inverse bind matrices
        glTF::Accessor& buffer_accessor = gltf_scene.accessors[ gltf_skin.inverse_bind_matrices_buffer_index ];
        glTF::BufferView& buffer_view = gltf_scene.buffer_views[ buffer_accessor.buffer_view ];

        i32 byte_offset = glTF::get_data_offset( buffer_accessor.byte_offset, buffer_view.byte_offset );

        RASSERT( buffer_accessor.count == skin.joints.size );
        skin.inverse_bind_matrices = ( mat4s* )rallocaa( sizeof( mat4s ) * buffer_accessor.count, resident_allocator, 16 );

        u8* buffer_data = ( u8* )buffers_data[ buffer_view.buffer ] + byte_offset;
        memory_copy( skin.inverse_bind_matrices, buffer_data, sizeof( mat4s ) * buffer_accessor.count );

        // Create matrix ssbo.
        // TODO: transforms use absolute indices, thus we need all nodes.
        BufferCreation bc;
        bc.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( mat4s ) * gltf_scene.nodes_count )
                  .set_data( buffer_data ).set_name( "Skin ssbo" );

        skin.joint_transforms = renderer->gpu->create_buffer( bc );
    }

    // Deallocate file-read buffer data
    for ( u32 buffer_index = 0; buffer_index < gltf_scene.buffers_count; ++buffer_index ) {
        void* buffer = buffers_data[ buffer_index ];
        resident_allocator->deallocate( buffer );
    }
    buffers_data.shutdown();

    i64 end_creating_buffers = time_now();

    // This is not needed anymore, free all temp memory after.
    //resource_name_buffer.shutdown();
    temp_allocator->free_marker( temp_allocator_initial_marker );

    // Init runtime meshes
    meshes.init( resident_allocator, gltf_scene.meshes_count );

    i64 end_loading = time_now();

    rprint( "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating %f seconds\n\tCreating Samplers %f seconds\n\tReading Buffers Data %f seconds\n\tCreating Buffers %f seconds\n", filename,
            time_delta_seconds( start_scene_loading, end_loading ), time_delta_seconds( start_scene_loading, end_loading_file ), time_delta_seconds( end_loading_file, end_creating_textures ),
            time_delta_seconds( end_creating_textures, end_creating_samplers ),
            time_delta_seconds( end_creating_samplers, end_reading_buffers_data ), time_delta_seconds( end_reading_buffers_data, end_creating_buffers ) );
}

void glTFScene::shutdown( Renderer* renderer ) {
    GpuDevice& gpu = *renderer->gpu;

    // Unload animations
    for ( u32 ai = 0; ai < animations.size; ++ai ) {
        Animation& animation = animations[ ai ];
        animation.channels.shutdown();

        for ( u32 si = 0; si < animation.samplers.size; ++si ) {
            AnimationSampler& sampler = animation.samplers[ si ];
            sampler.key_frames.shutdown();
            rfree( sampler.data, resident_allocator );
        }
        animation.samplers.shutdown();
    }
    animations.shutdown();

    // Unload skins
    for ( u32 si = 0; si < skins.size; ++si ) {
        Skin& skin = skins[ si ];
        skin.joints.shutdown();
        rfree( skin.inverse_bind_matrices, resident_allocator );

        renderer->gpu->destroy_buffer( skin.joint_transforms );
    }
    skins.shutdown();

    // Unload meshes
    for ( u32 mesh_index = 0; mesh_index < meshes.size; ++mesh_index ) {
        Mesh& mesh = meshes[ mesh_index ];

        gpu.destroy_buffer( mesh.pbr_material.material_buffer );
        gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set );
    }

    gpu.destroy_buffer( scene_cb );

    for ( u32 i = 0; i < images.size; ++i) {
        renderer->destroy_texture( &images[ i ] );
    }

    for ( u32 i = 0; i < samplers.size; ++i ) {
        renderer->destroy_sampler( &samplers[ i ] );
    }

    for ( u32 i = 0; i < buffers.size; ++i ) {
        renderer->destroy_buffer( &buffers[ i ] );
    }

    meshes.shutdown();

    names_buffer.shutdown();

    // Free scene buffers
    samplers.shutdown();
    images.shutdown();
    buffers.shutdown();

    // NOTE(marco): we can't destroy this sooner as textures and buffers
    // hold a pointer to the names stored here
    gltf_free( gltf_scene );
}

void glTFScene::prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph_ ) {

    scene_graph = scene_graph_;

    sizet cached_scratch_size = scratch_allocator->get_marker();

    // Scene constant buffer
    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuSceneData ) ).set_name( "scene_cb" );
    scene_cb = renderer->gpu->create_buffer( buffer_creation );

    // Create material
    const u64 hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( hashed_name );

    MaterialCreation material_creation;
    material_creation.set_name( "material_no_cull_opaque" ).set_technique( main_technique ).set_render_index( 0 );

    Material* pbr_material = renderer->create_material( material_creation );

    glTF::Scene& root_gltf_scene = gltf_scene.scenes[ gltf_scene.scene ];

    //
    Array<i32> nodes_to_visit;
    nodes_to_visit.init( scratch_allocator, 4 );

    // Calculate total node count: add first the root nodes.
    u32 total_node_count = root_gltf_scene.nodes_count;

    // Add initial nodes
    for ( u32 node_index = 0; node_index < root_gltf_scene.nodes_count; ++node_index ) {
        const i32 node = root_gltf_scene.nodes[ node_index ];
        nodes_to_visit.push( node );
    }
    // Visit nodes
    while ( nodes_to_visit.size ) {
        i32 node_index = nodes_to_visit.front();
        nodes_to_visit.delete_swap( 0 );

        glTF::Node& node = gltf_scene.nodes[ node_index ];
        for ( u32 ch = 0; ch < node.children_count; ++ch ) {
            const i32 children_index = node.children[ ch ];
            nodes_to_visit.push( children_index );
        }

        // Add only children nodes to the count, as the current node is
        // already calculated when inserting it.
        total_node_count += node.children_count;
    }

    scene_graph->resize( total_node_count );

    // Populate scene graph: visit again
    nodes_to_visit.clear();
    // Add initial nodes
    for ( u32 node_index = 0; node_index < root_gltf_scene.nodes_count; ++node_index ) {
        const i32 node = root_gltf_scene.nodes[ node_index ];
        nodes_to_visit.push( node );
    }

    while ( nodes_to_visit.size ) {
        i32 node_index = nodes_to_visit.front();
        nodes_to_visit.delete_swap( 0 );

        glTF::Node& node = gltf_scene.nodes[ node_index ];

        // Compute local transform: read either raw matrix or individual Scale/Rotation/Translation components
        if ( node.matrix_count ) {
            // CGLM and glTF have the same matrix layout, just memcopy it
            memcpy( &scene_graph->local_matrices[ node_index ], node.matrix, sizeof( mat4s ) );
            scene_graph->updated_nodes.set_bit( node_index );
        }
        else {
            // Handle individual transform components: SRT (scale, rotation, translation)
            vec3s node_scale{ 1.0f, 1.0f, 1.0f };
            if ( node.scale_count ) {
                RASSERT( node.scale_count == 3 );
                node_scale = vec3s{ node.scale[ 0 ], node.scale[ 1 ], node.scale[ 2 ] };
            }

            vec3s node_translation{ 0.f, 0.f, 0.f };
            if ( node.translation_count ) {
                RASSERT( node.translation_count == 3 );
                node_translation = vec3s{ node.translation[ 0 ], node.translation[ 1 ], node.translation[ 2 ] };
            }

            // Rotation is written as a plain quaternion
            versors node_rotation = glms_quat_identity();
            if ( node.rotation_count ) {
                RASSERT( node.rotation_count == 4 );
                node_rotation = glms_quat_init( node.rotation[ 0 ], node.rotation[ 1 ], node.rotation[ 2 ], node.rotation[ 3 ] );
            }

            Transform transform;
            transform.translation = node_translation;
            transform.scale = node_scale;
            transform.rotation = node_rotation;

            // Final SRT composition
            const mat4s local_matrix = transform.calculate_matrix();
            scene_graph->set_local_matrix( node_index, local_matrix );
        }

        // Handle parent-relationship
        if ( node.children_count ) {
            const Hierarchy& node_hierarchy = scene_graph->nodes_hierarchy[ node_index ];

            for ( u32 ch = 0; ch < node.children_count; ++ch) {
                const i32 children_index = node.children[ ch ];
                Hierarchy& children_hierarchy = scene_graph->nodes_hierarchy[ children_index ];
                scene_graph->set_hierarchy( children_index, node_index, node_hierarchy.level + 1 );

                nodes_to_visit.push( children_index );
            }
        }

        // Cache node name
        scene_graph->set_debug_data( node_index, node.name.data );

        if ( node.mesh == glTF::INVALID_INT_VALUE ) {
            continue;
        }

        // Start mesh part
        glTF::Mesh& gltf_mesh = gltf_scene.meshes[ node.mesh ];

        // Gltf primitives are conceptually submeshes.
        for ( u32 primitive_index = 0; primitive_index < gltf_mesh.primitives_count; ++primitive_index ) {
            Mesh mesh{ };
            // Assign scene graph node index
            mesh.scene_graph_node_index = node_index;

            glTF::MeshPrimitive& mesh_primitive = gltf_mesh.primitives[ primitive_index ];

            // Load material defaults: flags is modified after this point.
            mesh.pbr_material = {};

            const i32 position_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "POSITION" );
            const i32 tangent_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "TANGENT" );
            const i32 normal_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "NORMAL" );
            const i32 texcoord_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "TEXCOORD_0" );

            get_mesh_vertex_buffer( position_accessor_index, 0, mesh.position_buffer, mesh.position_offset, mesh.pbr_material.flags );
            get_mesh_vertex_buffer( tangent_accessor_index, DrawFlags_HasTangents, mesh.tangent_buffer, mesh.tangent_offset, mesh.pbr_material.flags );
            get_mesh_vertex_buffer( normal_accessor_index, DrawFlags_HasNormals, mesh.normal_buffer, mesh.normal_offset, mesh.pbr_material.flags );
            get_mesh_vertex_buffer( texcoord_accessor_index, DrawFlags_HasTexCoords, mesh.texcoord_buffer, mesh.texcoord_offset, mesh.pbr_material.flags );

            // Read skinning data
            mesh.skin_index = i32_max;
            if ( node.skin != glTF::INVALID_INT_VALUE ) {
                RASSERT( node.skin < skins.size );
                const i32 joints_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "JOINTS_0" );
                const i32 weights_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "WEIGHTS_0" );

                get_mesh_vertex_buffer( joints_accessor_index, DrawFlags_HasJoints, mesh.joints_buffer, mesh.joints_offset, mesh.pbr_material.flags );
                get_mesh_vertex_buffer( weights_accessor_index, DrawFlags_HasWeights, mesh.weights_buffer, mesh.weights_offset, mesh.pbr_material.flags );

                mesh.skin_index = node.skin;
            }

            // Create index buffer
            glTF::Accessor& indices_accessor = gltf_scene.accessors[ mesh_primitive.indices ];
            RASSERT( indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_SHORT || indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_INT );
            mesh.index_type = ( indices_accessor.component_type == glTF::Accessor::ComponentType::UNSIGNED_SHORT ) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;

            glTF::BufferView& indices_buffer_view = gltf_scene.buffer_views[ indices_accessor.buffer_view ];
            BufferResource& indices_buffer_gpu = buffers[ indices_buffer_view.buffer ];
            mesh.index_buffer = indices_buffer_gpu.handle;
            mesh.index_offset = glTF::get_data_offset( indices_accessor.byte_offset, indices_buffer_view.byte_offset );
            mesh.primitive_count = indices_accessor.count;

            // Read pbr material data if present
            if ( mesh_primitive.material != glTF::INVALID_INT_VALUE ) {
                glTF::Material& material = gltf_scene.materials[ mesh_primitive.material ];
                fill_pbr_material( *renderer, material, mesh.pbr_material );
            }

            // Create material buffer
            BufferCreation buffer_creation;
            buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuMeshData ) ).set_name( "mesh_data" );
            mesh.pbr_material.material_buffer = renderer->gpu->create_buffer( buffer_creation );

            DescriptorSetCreation ds_creation{};
            u32 pass_index = 0;
            if ( mesh.has_skinning() ) {
                pass_index = main_technique->name_hash_to_index.get( hash_calculate( "transparent_skinning_no_cull" ) );
            }
            else {
                pass_index = main_technique->name_hash_to_index.get( hash_calculate( "transparent_no_cull" ) );
            }

            DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ pass_index ].pipeline, k_material_descriptor_set_index );
            ds_creation.buffer( scene_cb, 0 ).buffer( mesh.pbr_material.material_buffer, 2 ).set_layout(layout);

            if ( mesh.has_skinning() ) {
                ds_creation.buffer( skins[ mesh.skin_index ].joint_transforms, 3 );
            }
            mesh.pbr_material.descriptor_set = renderer->gpu->create_descriptor_set( ds_creation );

            mesh.pbr_material.material = pbr_material;

            meshes.push(mesh);
        }
    }

    //qsort( meshes.data, meshes.size, sizeof( Mesh ), gltf_mesh_material_compare );

    scratch_allocator->free_marker( cached_scratch_size );
}

} // namespace raptor
