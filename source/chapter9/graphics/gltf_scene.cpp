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
#include "external/meshoptimizer/meshoptimizer.h"

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

        pbr_material.roughness = material.pbr_metallic_roughness->roughness_factor != glTF::INVALID_FLOAT_VALUE ? material.pbr_metallic_roughness->roughness_factor : 1.f;
        pbr_material.metallic = material.pbr_metallic_roughness->metallic_factor != glTF::INVALID_FLOAT_VALUE ? material.pbr_metallic_roughness->metallic_factor : 0.f;

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
            pbr_material.occlusion = material.occlusion_texture->strength;
        } else {
            pbr_material.occlusion = 1.0f;
        }
    }
}

u16 glTFScene::get_material_texture( GpuDevice& gpu, glTF::TextureInfo* texture_info ) {
    if ( texture_info != nullptr ) {
        glTF::Texture& gltf_texture = gltf_scene.textures[ texture_info->index ];
        TextureResource& texture_gpu = images[ gltf_texture.source ];

        if ( gltf_texture.sampler != i32_max ) {
            SamplerResource& sampler_gpu = samplers[ gltf_texture.sampler ];

            gpu.link_texture_sampler( texture_gpu.handle, sampler_gpu.handle );
        }

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

        if ( gltf_texture.sampler != i32_max ) {
            SamplerResource& sampler_gpu = samplers[ gltf_texture.sampler ];

            gpu.link_texture_sampler( texture_gpu.handle, sampler_gpu.handle );
        }

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
        tc.set_data( nullptr ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D ).set_flags( 0 ).set_size( ( u16 )width, ( u16 )height, 1 ).set_name( image.uri.data ).set_mips( mip_levels );
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


    i64 end_reading_buffers_data = time_now();

    // Build meshlets
    const sizet max_vertices = 64;
    const sizet max_triangles = 124;
    const f32 cone_weight = 0.0f;

    meshes.init( resident_allocator_, 16 );
    meshlets.init( resident_allocator, 16 );
    meshlets_data.init( resident_allocator, 16 );
    meshlets_vertex_positions.init( resident_allocator, 16 );
    meshlets_vertex_data.init( resident_allocator, 16 );
    gltf_mesh_to_mesh_offset.init( resident_allocator_, 16 );

    u32 mesh_index = 0;
    u32 meshlets_index_count = 0;

    mesh_aabb[0] = vec3s{ FLT_MAX, FLT_MAX, FLT_MAX };
    mesh_aabb[1] = vec3s{ FLT_MIN, FLT_MIN, FLT_MIN };

    for ( u32 mi = 0; mi < gltf_scene.meshes_count; ++mi ) {
        glTF::Mesh& mesh = gltf_scene.meshes[ mi ];

        gltf_mesh_to_mesh_offset.push( meshes.size );

        for ( u32 p = 0; p < mesh.primitives_count; ++p ) {
            glTF::MeshPrimitive& mesh_primitive = mesh.primitives[ p ];

            /*if ( mesh_primitive.material != glTF::INVALID_INT_VALUE ) {
                glTF::Material& material = gltf_scene.materials[ mesh_primitive.material ];

                if ( ( material.alpha_mode.data != nullptr && strcmp( material.alpha_mode.data, "MASK" ) == 0 ) ||
                     ( material.alpha_mode.data != nullptr && strcmp( material.alpha_mode.data, "BLEND" ) == 0 ) ) {
                    continue;
                }
            }*/

            // Add meshes
            Mesh mesh{};
            // Load material defaults: flags is modified after this point.
            mesh.pbr_material = {};

            // Vertex positions
            const i32 position_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "POSITION" );
            glTF::Accessor& position_buffer_accessor = gltf_scene.accessors[ position_accessor_index ];
            glTF::BufferView& position_buffer_view = gltf_scene.buffer_views[ position_buffer_accessor.buffer_view ];
            i32 position_data_offset = glTF::get_data_offset( position_buffer_accessor.byte_offset, position_buffer_view.byte_offset );
            f32* vertices = ( f32* )((u8*)buffers_data[ position_buffer_view.buffer ] + position_data_offset);

            // Calculate bounding sphere center
            vec3s position_min{ position_buffer_accessor.min[ 0 ], position_buffer_accessor.min[ 1 ], position_buffer_accessor.min[ 2 ] };
            vec3s position_max{ position_buffer_accessor.max[ 0 ], position_buffer_accessor.max[ 1 ], position_buffer_accessor.max[ 2 ] };
            vec3s bounding_center = glms_vec3_add( position_min, position_max );
            bounding_center = glms_vec3_divs( bounding_center, 2.0f );

            // Calculate bounding sphere radius
            f32 radius = raptor::max( glms_vec3_distance( position_max, bounding_center ), glms_vec3_distance( position_min, bounding_center ) );
            mesh.bounding_sphere = { bounding_center.x, bounding_center.y, bounding_center.z, radius };

            // Vertex normals
            const i32 normal_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "NORMAL" );
            f32* normals = nullptr;
            if ( normal_accessor_index != -1 ) {
                glTF::Accessor& normal_buffer_accessor = gltf_scene.accessors[ normal_accessor_index ];
                glTF::BufferView& normal_buffer_view = gltf_scene.buffer_views[ normal_buffer_accessor.buffer_view ];
                i32 normal_data_offset = glTF::get_data_offset( normal_buffer_accessor.byte_offset, normal_buffer_view.byte_offset );
                normals = ( f32* )((u8*)buffers_data[ normal_buffer_view.buffer ] + normal_data_offset);
            }

            // Vertex texture coords
            const i32 tex_coord_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "TEXCOORD_0" );
            f32* tex_coords = nullptr;
            if ( tex_coord_accessor_index != -1 ) {
                glTF::Accessor& tex_coord_buffer_accessor = gltf_scene.accessors[ tex_coord_accessor_index ];
                glTF::BufferView& tex_coord_buffer_view = gltf_scene.buffer_views[ tex_coord_buffer_accessor.buffer_view ];
                i32 tex_coord_data_offset = glTF::get_data_offset( tex_coord_buffer_accessor.byte_offset, tex_coord_buffer_view.byte_offset );
                tex_coords = ( f32* )((u8*)buffers_data[ tex_coord_buffer_view.buffer ] + tex_coord_data_offset);
            }

            // Vertex tangents
            const i32 tangent_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "TANGENT" );
            f32* tangents = nullptr;
            if ( tangent_accessor_index != -1 ) {
                glTF::Accessor& tangent_buffer_accessor = gltf_scene.accessors[ tangent_accessor_index ];
                glTF::BufferView& tangent_buffer_view = gltf_scene.buffer_views[ tangent_buffer_accessor.buffer_view ];
                i32 tangent_data_offset = glTF::get_data_offset( tangent_buffer_accessor.byte_offset, tangent_buffer_view.byte_offset );
                tangents = ( f32* )((u8*)buffers_data[ tangent_buffer_view.buffer ] + tangent_data_offset);
            }

            // Cache vertex buffers
            get_mesh_vertex_buffer( position_accessor_index, 0, mesh.position_buffer, mesh.position_offset, mesh.pbr_material.flags );
            get_mesh_vertex_buffer( tangent_accessor_index, DrawFlags_HasTangents, mesh.tangent_buffer, mesh.tangent_offset, mesh.pbr_material.flags );
            get_mesh_vertex_buffer( normal_accessor_index, DrawFlags_HasNormals, mesh.normal_buffer, mesh.normal_offset, mesh.pbr_material.flags );
            get_mesh_vertex_buffer( tex_coord_accessor_index, DrawFlags_HasTexCoords, mesh.texcoord_buffer, mesh.texcoord_offset, mesh.pbr_material.flags );

            const i32 joints_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "JOINTS_0" );
            const i32 weights_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "WEIGHTS_0" );

            get_mesh_vertex_buffer( joints_accessor_index, DrawFlags_HasJoints, mesh.joints_buffer, mesh.joints_offset, mesh.pbr_material.flags );
            get_mesh_vertex_buffer( weights_accessor_index, DrawFlags_HasWeights, mesh.weights_buffer, mesh.weights_offset, mesh.pbr_material.flags );


            // Index buffer
            glTF::Accessor& indices_accessor = gltf_scene.accessors[ mesh_primitive.indices ];
            glTF::BufferView& indices_buffer_view = gltf_scene.buffer_views[ indices_accessor.buffer_view ];
            u8* buffer_data = ( u8* )buffers_data[ indices_buffer_view.buffer ];
            i32 index_data_offset = glTF::get_data_offset( indices_accessor.byte_offset, indices_buffer_view.byte_offset );
            u16* indices = ( u16* )(buffer_data + index_data_offset);

            // Read pbr material data if present
            if ( mesh_primitive.material != glTF::INVALID_INT_VALUE ) {
                glTF::Material& material = gltf_scene.materials[ mesh_primitive.material ];
                fill_pbr_material( *renderer, material, mesh.pbr_material );
            }

            BufferResource& indices_buffer_gpu = buffers[ indices_buffer_view.buffer ];
            mesh.index_buffer = indices_buffer_gpu.handle;
            mesh.index_offset = glTF::get_data_offset( indices_accessor.byte_offset, indices_buffer_view.byte_offset );
            mesh.primitive_count = indices_accessor.count;

            mesh.gpu_mesh_index = meshes.size;

            const sizet max_meshlets = meshopt_buildMeshletsBound( indices_accessor.count, max_vertices, max_triangles );
            sizet temp_marker = temp_allocator->get_marker();

            Array<meshopt_Meshlet> local_meshlets;
            local_meshlets.init( temp_allocator, max_meshlets, max_meshlets );

            Array<u32> meshlet_vertex_indices;
            meshlet_vertex_indices.init( temp_allocator, max_meshlets * max_vertices, max_meshlets* max_vertices );

            Array<u8> meshlet_triangles;
            meshlet_triangles.init( temp_allocator, max_meshlets * max_triangles * 3, max_meshlets * max_triangles * 3 );

            sizet meshlet_count = meshopt_buildMeshlets( local_meshlets.data, meshlet_vertex_indices.data, meshlet_triangles.data, indices,
                                                         indices_accessor.count, vertices, position_buffer_accessor.count, sizeof( vec3s ),
                                                         max_vertices, max_triangles, cone_weight );

            u32 meshlet_vertex_offset = meshlets_vertex_positions.size;
            for ( u32 v = 0; v < position_buffer_accessor.count; ++v ) {
                GpuMeshletVertexPosition meshlet_vertex_pos{ };

                f32 x = vertices[ v * 3 + 0 ];
                f32 y = vertices[ v * 3 + 1 ];
                f32 z = vertices[ v * 3 + 2 ];

                if ( x < mesh_aabb[ 0 ].x )
                {
                    mesh_aabb[ 0 ].x = x;
                }
                if ( y < mesh_aabb[ 0 ].y )
                {
                    mesh_aabb[ 0 ].y = y;
                }
                if ( z < mesh_aabb[ 0 ].z )
                {
                    mesh_aabb[ 0 ].z = z;
                }

                if ( x > mesh_aabb[ 1 ].x )
                {
                    mesh_aabb[ 1 ].x = x;
                }
                if ( y > mesh_aabb[ 1 ].y )
                {
                    mesh_aabb[ 1 ].y = y;
                }
                if ( z > mesh_aabb[ 1 ].z )
                {
                    mesh_aabb[ 1 ].z = z;
                }

                meshlet_vertex_pos.position[ 0 ] = x;
                meshlet_vertex_pos.position[ 1 ] = y;
                meshlet_vertex_pos.position[ 2 ] = z;

                meshlets_vertex_positions.push( meshlet_vertex_pos );

                GpuMeshletVertexData meshlet_vertex_data{ };

                if ( normals != nullptr ) {
                    meshlet_vertex_data.normal[ 0 ] = ( normals[ v * 3 + 0 ] + 1.0f ) * 127.0f;
                    meshlet_vertex_data.normal[ 1 ] = ( normals[ v * 3 + 1 ] + 1.0f ) * 127.0f;
                    meshlet_vertex_data.normal[ 2 ] = ( normals[ v * 3 + 2 ] + 1.0f ) * 127.0f;
                }

                if ( tangents != nullptr ) {
                    meshlet_vertex_data.tangent[ 0 ] = ( tangents[ v * 3 + 0 ] + 1.0f ) * 127.0f;
                    meshlet_vertex_data.tangent[ 1 ] = ( tangents[ v * 3 + 1 ] + 1.0f ) * 127.0f;
                    meshlet_vertex_data.tangent[ 2 ] = ( tangents[ v * 3 + 2 ] + 1.0f ) * 127.0f;
                    meshlet_vertex_data.tangent[ 3 ] = ( tangents[ v * 3 + 3 ] + 1.0f ) * 127.0f;
                }

                meshlet_vertex_data.uv_coords[ 0 ] = meshopt_quantizeHalf( tex_coords[ v * 2 + 0 ] );
                meshlet_vertex_data.uv_coords[ 1 ] = meshopt_quantizeHalf( tex_coords[ v * 2 + 1 ] );

                meshlets_vertex_data.push( meshlet_vertex_data );
            }

            // Cache meshlet offset
            mesh.meshlet_offset = meshlets.size;
            mesh.meshlet_count = meshlet_count;
            mesh.meshlet_index_count = 0;

            // Append meshlet data
            for ( u32 m = 0; m < meshlet_count; ++m ) {
                meshopt_Meshlet& local_meshlet = local_meshlets[ m ];

                meshopt_Bounds meshlet_bounds = meshopt_computeMeshletBounds(meshlet_vertex_indices.data + local_meshlet.vertex_offset,
                                                                             meshlet_triangles.data + local_meshlet.triangle_offset, local_meshlet.triangle_count,
                                                                             vertices, position_buffer_accessor.count, sizeof( vec3s ));

                GpuMeshlet meshlet{};
                meshlet.data_offset = meshlets_data.size;
                meshlet.vertex_count = local_meshlet.vertex_count;
                meshlet.triangle_count = local_meshlet.triangle_count;

                meshlet.center = vec3s{ meshlet_bounds.center[ 0 ], meshlet_bounds.center[ 1 ], meshlet_bounds.center[ 2 ] };
                meshlet.radius = meshlet_bounds.radius;

                meshlet.cone_axis[ 0 ] = meshlet_bounds.cone_axis_s8[ 0 ];
                meshlet.cone_axis[ 1 ] = meshlet_bounds.cone_axis_s8[ 1 ];
                meshlet.cone_axis[ 2 ] = meshlet_bounds.cone_axis_s8[ 2 ];

                meshlet.cone_cutoff = meshlet_bounds.cone_cutoff_s8;
                meshlet.mesh_index = meshes.size;

                // Resize data array
                const u32 index_group_count = ( local_meshlet.triangle_count * 3 + 3 ) / 4;
                meshlets_data.set_capacity( meshlets_data.size + local_meshlet.vertex_count + index_group_count );

                for ( u32 i = 0; i < meshlet.vertex_count; ++i ) {
                    const u32 vertex_index = meshlet_vertex_offset + meshlet_vertex_indices[ local_meshlet.vertex_offset + i ];
                    meshlets_data.push( vertex_index );
                }

                // Store indices as uint32
                // NOTE(marco): we write 4 indices at at time, it will come in handy in the mesh shader
                const u32* index_groups = reinterpret_cast< const u32* >( meshlet_triangles.data + local_meshlet.triangle_offset );
                for ( u32 i = 0; i < index_group_count; ++i ) {
                    const u32 index_group = index_groups[ i ];
                    meshlets_data.push( index_group );
                }

                // Writing in group of fours can be problematic, if there are non multiple of 3
                // indices a triangle can be shared between meshlets.
                // We need to add some padding for that.
                // This is visible only when emulating meshlets, so probably there are controls
                // at driver level that avoid this problems when using mesh shaders.
                // Check for the last 3 indices: if last one are two are zero, then add one or two
                // groups of empty triangles.
                u32 last_index_group = index_groups[ index_group_count - 1 ];
                u32 last_index = ( last_index_group >> 8 ) & 0xff;
                u32 second_last_index = ( last_index_group >> 16 ) & 0xff;
                u32 third_last_index = ( last_index_group >> 24 ) & 0xff;
                if ( last_index != 0 && third_last_index == 0 ) {

                    if ( second_last_index != 0 ) {
                        // Add a single index group of zeroes
                        meshlets_data.push( 0 );
                        meshlet.triangle_count++;
                    }

                    meshlet.triangle_count++;
                    // Add another index group of zeroes
                    meshlets_data.push( 0 );
                }

                mesh.meshlet_index_count += meshlet.triangle_count * 3;

                meshlets.push( meshlet );

                meshlets_index_count += index_group_count;
            }

            // Add mesh with all data
            meshes.push( mesh );

            while ( meshlets.size % 32 )
                meshlets.push( GpuMeshlet() );

            temp_allocator->free_marker( temp_marker );

            mesh_index++;
        }
    }

    // Create meshlets index buffer, that will be used to emulate meshlets if mesh shaders are not present.
    BufferCreation bc{};
    bc.set( VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Stream, meshlets_index_count * sizeof( u32 ) * 8 ).set_name( "meshlets_index_buffer" );

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        meshlets_index_buffer_sb[ i ] = renderer->gpu->create_buffer( bc );
    }

    // Create meshlets instances buffer
    bc.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Stream, (meshlets.size * 2 /* not sure about max number here */ ) * sizeof(u32) * 2).set_name("meshlets_instances_buffer");

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        meshlets_instances_sb[ i ] = renderer->gpu->create_buffer( bc );
    }

    // Create meshlets visible instances buffer
    bc.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Stream, ( meshlets.size * 2 /* not sure about max number here */ ) * sizeof( u32 ) * 2 ).set_name( "meshlets_instances_buffer" );

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        meshlets_visible_instances_sb[ i ] = renderer->gpu->create_buffer( bc );
    }

    i64 end_building_meshlets = time_now();

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

    // Init mesh instances with at least meshes count.
    mesh_instances.init( resident_allocator_, meshes.size );

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

    // Unload meshlets
    meshlets.shutdown();
    meshlets_vertex_data.shutdown();
    meshlets_vertex_positions.shutdown();
    meshlets_data.shutdown();
    gltf_mesh_to_mesh_offset.shutdown();

    // Unload meshes
    for ( u32 mesh_index = 0; mesh_index < meshes.size; ++mesh_index ) {
        Mesh& mesh = meshes[ mesh_index ];

        gpu.destroy_buffer( mesh.pbr_material.material_buffer );
        gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set_transparent );
        gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set_main );
    }

    gpu.destroy_buffer( scene_cb );
    gpu.destroy_buffer( meshes_sb );
    gpu.destroy_buffer( mesh_bounds_sb );
    gpu.destroy_buffer( mesh_instances_sb );
    gpu.destroy_buffer( meshlets_sb );
    gpu.destroy_buffer( meshlets_vertex_pos_sb );
    gpu.destroy_buffer( meshlets_vertex_data_sb );
    gpu.destroy_buffer( meshlets_data_sb );

    gpu.destroy_buffer( debug_line_sb );
    gpu.destroy_buffer( debug_line_count_sb );
    gpu.destroy_buffer( debug_line_commands_sb );

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        gpu.destroy_buffer( meshlets_index_buffer_sb[ i ] );
        gpu.destroy_buffer( meshlets_instances_sb[ i ] );
        gpu.destroy_buffer( meshlets_visible_instances_sb[ i ] );

        gpu.destroy_buffer( mesh_task_indirect_early_commands_sb[ i ] );
        gpu.destroy_buffer( mesh_task_indirect_culled_commands_sb[ i ] );
        gpu.destroy_buffer( mesh_task_indirect_count_early_sb[ i ] );

        gpu.destroy_buffer( mesh_task_indirect_late_commands_sb[ i ] );
        gpu.destroy_buffer( mesh_task_indirect_count_late_sb[ i ] );
        gpu.destroy_buffer( meshlet_instances_indirect_count_sb[ i ] );

        gpu.destroy_buffer( lights_lut_sb[ i ] );
        gpu.destroy_buffer( lights_tiles_sb[ i ] );
        gpu.destroy_buffer( lights_indices_sb[ i ] );
        gpu.destroy_buffer( lighting_constants_cb[ i ] );

        gpu.destroy_descriptor_set( mesh_shader_early_descriptor_set[ i ] );
        gpu.destroy_descriptor_set( mesh_shader_late_descriptor_set[ i ] );
        gpu.destroy_descriptor_set( mesh_shader_transparent_descriptor_set[ i ] );
        gpu.destroy_descriptor_set( meshlet_emulation_descriptor_set[ i ] );
    }

    for ( u32 i = 0; i < images.size; ++i) {
        renderer->destroy_texture( &images[ i ] );
    }

    for ( u32 i = 0; i < samplers.size; ++i ) {
        renderer->destroy_sampler( &samplers[ i ] );
    }

    for ( u32 i = 0; i < buffers.size; ++i ) {
        renderer->destroy_buffer( &buffers[ i ] );
    }

    gpu.destroy_buffer( lights_list_sb );
    gpu.destroy_texture( fragment_shading_rate_image );

    lights.shutdown();
    lights_lut.shutdown();

    meshes.shutdown();
    mesh_instances.shutdown();

    names_buffer.shutdown();

    // Free scene buffers
    samplers.shutdown();
    images.shutdown();
    buffers.shutdown();

    // NOTE(marco): we can't destroy this sooner as textures and buffers
    // hold a pointer to the names stored here
    gltf_free( gltf_scene );

    debug_renderer.shutdown();
}

void glTFScene::prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph_ ) {

    scene_graph = scene_graph_;

    sizet cached_scratch_size = scratch_allocator->get_marker();

    // Scene constant buffer
    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuSceneData ) ).set_name( "scene_cb" );
    scene_cb = renderer->gpu->create_buffer( buffer_creation );

    buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, sizeof( u32 ) * meshlets_data.size ).set_name( "meshlet_data_sb" ).set_data( meshlets_data.data );
    meshlets_data_sb = renderer->gpu->create_buffer( buffer_creation );

    buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, sizeof( GpuMeshletVertexPosition ) * meshlets_vertex_positions.size ).set_name( "meshlet_vertex_sb" ).set_data( meshlets_vertex_positions.data );
    meshlets_vertex_pos_sb = renderer->gpu->create_buffer( buffer_creation );

    buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, sizeof( GpuMeshletVertexData ) * meshlets_vertex_data.size ).set_name( "meshlet_vertex_sb" ).set_data( meshlets_vertex_data.data );
    meshlets_vertex_data_sb = renderer->gpu->create_buffer( buffer_creation );

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

    u32 total_meshlets = 0;

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
        u32 gltf_mesh_offset = gltf_mesh_to_mesh_offset[ node.mesh ];

        // Gltf primitives are conceptually submeshes.
        for ( u32 primitive_index = 0; primitive_index < gltf_mesh.primitives_count; ++primitive_index ) {
            MeshInstance mesh_instance{ };
            // Assign scene graph node index
            mesh_instance.scene_graph_node_index = node_index;

            glTF::MeshPrimitive& mesh_primitive = gltf_mesh.primitives[ primitive_index ];

            // Cache parent mesh and assign material
            u32 mesh_primitive_index = gltf_mesh_offset + primitive_index;
            mesh_instance.mesh = &meshes[ mesh_primitive_index ];
            mesh_instance.mesh->pbr_material.material = pbr_material;
            // Cache gpu mesh instance index, used to retrieve data on gpu.
            mesh_instance.gpu_mesh_instance_index = mesh_instances.size;

            // Found a skin index, cache it
            mesh_instance.mesh->skin_index = i32_max;
            if ( node.skin != glTF::INVALID_INT_VALUE ) {
                RASSERT( node.skin < skins.size );

                mesh_instance.mesh->skin_index = node.skin;
            }

            total_meshlets += mesh_instance.mesh->meshlet_count;

            mesh_instances.push( mesh_instance );
        }
    }

    rprint( "Total meshlet instances %u\n", total_meshlets );

    // Meshlets buffers
    buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, sizeof( GpuMeshlet ) * meshlets.size ).set_name( "meshlet_sb" ).set_data( meshlets.data );
    meshlets_sb = renderer->gpu->create_buffer( buffer_creation );

    // Create mesh ssbo
    // TODO[gabriel] : move this to be static?
    buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuMaterialData ) * meshes.size ).set_name( "meshes_sb" );
    meshes_sb = renderer->gpu->create_buffer( buffer_creation );

    // Create mesh bound ssbo
    buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( vec4s ) * meshes.size ).set_name( "mesh_bound_sb" );
    mesh_bounds_sb = renderer->gpu->create_buffer( buffer_creation );

    // Create mesh instances ssbo
    buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuMeshInstanceData ) * mesh_instances.size ).set_name( "mesh_instances_sb" );
    mesh_instances_sb = renderer->gpu->create_buffer( buffer_creation );

    // Create indirect buffers, dynamic so need multiple buffering.
    for ( u32 i = 0; i < k_max_frames; ++i ) {
        // This buffer contains both opaque and transparent commands, thus is multiplied by two.
        buffer_creation.reset().set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, mesh_instances.size * sizeof( GpuMeshDrawCommand ) * 2).set_name( "early_draw_commands_sb" );
        mesh_task_indirect_early_commands_sb[ i ] = renderer->gpu->create_buffer( buffer_creation );

        buffer_creation.reset().set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, mesh_instances.size * sizeof( GpuMeshDrawCommand ) * 2).set_name( "culled_draw_commands_sb" );
        mesh_task_indirect_culled_commands_sb[ i ] = renderer->gpu->create_buffer( buffer_creation );

        buffer_creation.reset().set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, mesh_instances.size * sizeof( GpuMeshDrawCommand ) * 2).set_name( "late_draw_commands_sb" );
        mesh_task_indirect_late_commands_sb[ i ] = renderer->gpu->create_buffer( buffer_creation );

        buffer_creation.reset().set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuMeshDrawCounts ) ).set_name( "early_mesh_count_sb" );
        mesh_task_indirect_count_early_sb[ i ] = renderer->gpu->create_buffer( buffer_creation );

        buffer_creation.reset().set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuMeshDrawCounts ) ).set_name( "late_mesh_count_sb" );
        mesh_task_indirect_count_late_sb[ i ] = renderer->gpu->create_buffer( buffer_creation );

        buffer_creation.reset().set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( u32 ) * 4 ).set_name( "meshlet_instances_indirect_sb" );
        meshlet_instances_indirect_count_sb[ i ] = renderer->gpu->create_buffer( buffer_creation );
    }

    // Create debug draw buffers
    {
        static constexpr u32 k_max_lines = 64000 + 64000;   // 3D + 2D lines in the same buffer
        buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, k_max_lines * sizeof( vec4s ) * 2 ).set_name( "debug_line_sb" );
        debug_line_sb = renderer->gpu->create_buffer( buffer_creation );

        buffer_creation.reset().set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( vec4s ) ).set_name( "debug_line_count_sb" );
        debug_line_count_sb = renderer->gpu->create_buffer( buffer_creation );

        // Gather 3D and 2D gpu drawing commands
        buffer_creation.reset().set( VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( VkDrawIndirectCommand ) * 2 ).set_name( "debug_line_commands_sb" );
        debug_line_commands_sb = renderer->gpu->create_buffer( buffer_creation );
    }

    // Create per mesh descriptor sets, using the mesh draw ssbo
    for ( u32 m = 0; m < meshes.size; ++m ) {
        Mesh& mesh = meshes[ m ];

        // Create material buffer
        BufferCreation buffer_creation;
        buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuMaterialData ) ).set_name( "mesh_data" );
        mesh.pbr_material.material_buffer = renderer->gpu->create_buffer( buffer_creation );

        DescriptorSetCreation ds_creation{};
        u32 pass_index = 0;
        u32 depth_pass_index = 0;
        if ( mesh.has_skinning() ) {
            pass_index = main_technique->name_hash_to_index.get( hash_calculate( "transparent_skinning_no_cull" ) );
            depth_pass_index = main_technique->name_hash_to_index.get( hash_calculate( "depth_pre_skinning" ) );
        } else {
            pass_index = main_technique->name_hash_to_index.get( hash_calculate( "transparent_no_cull" ) );
            depth_pass_index = main_technique->name_hash_to_index.get( hash_calculate( "depth_pre" ) );
        }

        DescriptorSetLayoutHandle layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ pass_index ].pipeline, k_material_descriptor_set_index );
        //DescriptorSetLayout* ds = renderer->gpu->access_descriptor_set_layout( layout );
        ds_creation.buffer( scene_cb, 0 ).buffer( meshes_sb, 2 ).buffer( mesh_instances_sb, 10 ).buffer( mesh_bounds_sb, 12 )
            .buffer( debug_line_sb, 20 ).buffer( debug_line_count_sb, 21 ).buffer( debug_line_commands_sb, 22).buffer( mesh_bounds_sb, 25 ).set_layout(layout);

        if ( mesh.has_skinning() ) {
            ds_creation.buffer( skins[ mesh.skin_index ].joint_transforms, 3 );
        }
        // Create main descriptor set
        mesh.pbr_material.descriptor_set_transparent = renderer->gpu->create_descriptor_set( ds_creation );

        // Create depth descriptor set
        layout = renderer->gpu->get_descriptor_set_layout( main_technique->passes[ depth_pass_index ].pipeline, k_material_descriptor_set_index );
        //ds = renderer->gpu->access_descriptor_set_layout( layout );
        ds_creation.reset().buffer( scene_cb, 0 ).buffer( meshes_sb, 2 ).buffer( mesh_instances_sb, 10 ).buffer( mesh_bounds_sb, 12 ).set_layout( layout );
        mesh.pbr_material.descriptor_set_main = renderer->gpu->create_descriptor_set( ds_creation );
    }


    // Meshlet and meshlet emulation descriptors
    {
        const u64 meshlet_hashed_name = hash_calculate( "meshlet" );
        GpuTechnique* meshlet_technique = renderer->resource_cache.techniques.get( meshlet_hashed_name );

        if ( renderer->gpu->mesh_shaders_extension_present ) {

            u32 meshlet_index = meshlet_technique->get_pass_index( "gbuffer_culling" );
            GpuTechniquePass& meshlet_pass = meshlet_technique->passes[ meshlet_index ];
            DescriptorSetLayoutHandle layout = meshlet_index != u16_max ? renderer->gpu->get_descriptor_set_layout( meshlet_pass.pipeline, k_material_descriptor_set_index ) : k_invalid_layout;

            for ( u32 i = 0; i < k_max_frames; ++i ) {
                DescriptorSetCreation ds_creation{};

                ds_creation.reset();
                add_scene_descriptors( ds_creation, meshlet_pass );
                add_mesh_descriptors( ds_creation, meshlet_pass );
                add_debug_descriptors( ds_creation, meshlet_pass );
                add_meshlet_descriptors( ds_creation, meshlet_pass );

                ds_creation.buffer( mesh_task_indirect_early_commands_sb[ i ], 6 ).buffer( mesh_task_indirect_count_early_sb[ i ], 7 ).set_layout( layout );

                mesh_shader_early_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );

                ds_creation.reset();
                add_scene_descriptors( ds_creation, meshlet_pass );
                add_mesh_descriptors( ds_creation, meshlet_pass );
                add_debug_descriptors( ds_creation, meshlet_pass );
                add_meshlet_descriptors( ds_creation, meshlet_pass );

                ds_creation.buffer( mesh_task_indirect_late_commands_sb[ i ], 6 ).buffer( mesh_task_indirect_count_late_sb[ i ], 7 ).set_layout( layout );

                mesh_shader_late_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
            }
        }

        u32 meshlet_emulation_index = meshlet_technique->get_pass_index( "emulation_gbuffer_culling" );
        GpuTechniquePass& meshlet_emulation_pass = meshlet_technique->passes[ meshlet_emulation_index ];
        DescriptorSetLayoutHandle meshlet_emulation_layout = renderer->gpu->get_descriptor_set_layout( meshlet_emulation_pass.pipeline, k_material_descriptor_set_index );

        for ( u32 i = 0; i < k_max_frames; ++i ) {
            DescriptorSetCreation ds_creation{};

            ds_creation.reset();
            add_scene_descriptors( ds_creation, meshlet_emulation_pass );
            add_mesh_descriptors( ds_creation, meshlet_emulation_pass );
            add_debug_descriptors( ds_creation, meshlet_emulation_pass );
            add_meshlet_descriptors( ds_creation, meshlet_emulation_pass );

            ds_creation.buffer( mesh_task_indirect_early_commands_sb[ i ], 6 ).buffer( mesh_task_indirect_count_early_sb[ i ], 7 )
                .buffer( meshlets_instances_sb[ i ], meshlet_emulation_pass.get_binding_index( "MeshletInstances" ) ).set_layout( meshlet_emulation_layout );

            meshlet_emulation_descriptor_set[ i ] = renderer->gpu->create_descriptor_set( ds_creation );
        }
    }

    scratch_allocator->free_marker( cached_scratch_size );

    lights.init( resident_allocator, k_num_lights );

    // Add a first light in a fixed position and then random lights.
    const u32 lights_per_side = raptor::ceilu32( sqrtf( active_lights * 1.f ) );
    {
        const f32 x = 0;
        const f32 y = .5f;
        const f32 z = -1.2f;

        float r = 1;
        float g = 1;
        float b = 1;

        {
            Light new_light{ };
            new_light.world_position = vec3s{ x, y, z };
            new_light.radius = 3.888f;

            new_light.color = vec3s{ r, g, b };
            new_light.intensity = 3.0f;

            vec3s aabb_min = glms_vec3_adds( new_light.world_position, -new_light.radius );
            vec3s aabb_max = glms_vec3_adds( new_light.world_position, new_light.radius );

            new_light.aabb_min = vec4s{ aabb_min.x, aabb_min.y, aabb_min.z, 1.0f };
            new_light.aabb_max = vec4s{ aabb_max.x, aabb_max.y, aabb_max.z, 1.0f };

            lights.push( new_light );
        }

        for ( u32 i = 1; i < k_num_lights; ++i ) {

            const f32 x = ( i % lights_per_side ) - lights_per_side * .7f;
            const f32 y = 0.05f;
            const f32 z = ( i / lights_per_side ) - lights_per_side * .7f;

            /*float x = get_random_value( mesh_aabb[ 0 ].x * scale, mesh_aabb[ 1 ].x * scale );
            float y = get_random_value( mesh_aabb[ 0 ].y * scale, mesh_aabb[ 1 ].y * scale );
            float z = get_random_value( mesh_aabb[ 0 ].z * scale, mesh_aabb[ 1 ].z * scale );*/

            f32 r = get_random_value( 0.1f, 1.0f );
            f32 g = get_random_value( 0.1f, 1.0f );
            f32 b = get_random_value( 0.1f, 1.0f );

            Light new_light{ };
            new_light.world_position = vec3s{ x, y, z };
            new_light.radius = 0.6f;

            vec3s aabb_min = glms_vec3_adds( new_light.world_position, -new_light.radius );
            vec3s aabb_max = glms_vec3_adds( new_light.world_position,  new_light.radius );

            new_light.aabb_min = vec4s{ aabb_min.x, aabb_min.y, aabb_min.z, 1.0f };
            new_light.aabb_max = vec4s{ aabb_max.x, aabb_max.y, aabb_max.z, 1.0f };

            new_light.color = vec3s{ r, g, b };
            new_light.intensity = 3.0f;

            lights.push( new_light );
        }
    }

    {
        buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuLight ) * k_num_lights ).set_name( "light_array" );
        lights_list_sb = renderer->gpu->create_buffer( buffer_creation );
    }

    lights_lut.init( resident_allocator, k_light_z_bins, k_light_z_bins );

    for ( u32 i = 0; i < k_max_frames; ++i ) {
        buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( u32 ) * k_light_z_bins ).set_name( "light_z_bins" );
        lights_lut_sb[ i ] = renderer->gpu->create_buffer( buffer_creation );

        buffer_creation.reset().set( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( u32 )* k_num_lights ).set_name( "light_indices_sb" );
        lights_indices_sb[ i ] = renderer->gpu->create_buffer( buffer_creation );

        buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuLightingData ) ).set_name( "lighting_constants_cb" );
        lighting_constants_cb[ i ] = renderer->gpu->create_buffer( buffer_creation );
    }

    debug_renderer.init( *this, resident_allocator, scratch_allocator );

}

} // namespace raptor
