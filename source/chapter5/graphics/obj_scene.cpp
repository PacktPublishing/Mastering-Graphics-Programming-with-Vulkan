#include "graphics/obj_scene.hpp"
#include "graphics/gpu_profiler.hpp"
#include "graphics/raptor_imgui.hpp"
#include "graphics/scene_graph.hpp"
#include "graphics/asynchronous_loader.hpp"

#include "foundation/file.hpp"
#include "foundation/time.hpp"

#include "external/stb_image.h"

#include "external/cglm/struct/affine.h"
#include "external/cglm/struct/mat4.h"
#include "external/cglm/struct/vec3.h"
#include "external/tracy/tracy/Tracy.hpp"

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace raptor {

static bool is_shared_vertex( PhysicsVertex* vertices, PhysicsVertex& src, u32 dst ) {
    u32 shared_count = 0;

    f32 max_distance = 0.0f;
    f32 min_distance = 10000.0f;

    for ( u32 j = 0; j < src.joint_count; ++j ) {
        PhysicsVertex& joint_vertex = vertices[ src.joints[ j ].vertex_index ];
        f32 distance = glms_vec3_distance( src.start_position, joint_vertex.start_position );

        max_distance = ( distance > max_distance ) ? distance : max_distance;
        min_distance = ( distance < min_distance ) ? distance : min_distance;
    }

    // NOTE(marco): this is to add joints with the next-next vertex either in horizontal
    // or vertical direction.
    min_distance *= 2;
    max_distance = ( min_distance > max_distance ) ? min_distance : max_distance;

    PhysicsVertex& dst_vertex = vertices[ dst ];
    f32 distance = glms_vec3_distance( src.start_position, dst_vertex.start_position );

    // NOTE(marco): this only works if we work with a plane with equal size subdivision
    return ( distance <= max_distance );
}

void ObjScene::init( cstring filename, cstring path, Allocator* resident_allocator_, StackAllocator* temp_allocator, AsynchronousLoader* async_loader_ )
{
    async_loader = async_loader_;
    resident_allocator = resident_allocator_;
    renderer = async_loader->renderer;

    sizet temp_allocator_initial_marker = temp_allocator->get_marker();

    // Time statistics
    i64 start_scene_loading = time_now();

    assimp_scene = aiImportFile( filename,
        aiProcess_CalcTangentSpace       |
        aiProcess_GenNormals             |
        aiProcess_Triangulate            |
        aiProcess_JoinIdenticalVertices  |
        aiProcess_SortByPType);

    i64 end_loading_file = time_now();

    // If the import failed, report it
    if( assimp_scene == nullptr ) {
        RASSERT(false);
        return;
    }

    SamplerCreation sampler_creation{ };
    sampler_creation.set_address_mode_uv( VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT ).set_min_mag_mip( VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    sampler = renderer->create_sampler( sampler_creation );

    images.init( resident_allocator, 1024 );

    Array<PBRMaterial> materials;
    materials.init( resident_allocator, assimp_scene->mNumMaterials );

    for ( u32 material_index = 0; material_index < assimp_scene->mNumMaterials; ++material_index ) {
        aiMaterial* material = assimp_scene->mMaterials[ material_index ];

        PBRMaterial raptor_material{ };

        aiString texture_file;

        if( aiGetMaterialString( material, AI_MATKEY_TEXTURE( aiTextureType_DIFFUSE, 0 ), &texture_file ) == AI_SUCCESS ) {
            raptor_material.diffuse_texture_index = load_texture( texture_file.C_Str(), path, temp_allocator );
        } else {
            raptor_material.diffuse_texture_index = k_invalid_scene_texture_index;
        }

        if( aiGetMaterialString( material, AI_MATKEY_TEXTURE( aiTextureType_NORMALS, 0 ), &texture_file ) == AI_SUCCESS )
        {
            raptor_material.normal_texture_index = load_texture( texture_file.C_Str(), path, temp_allocator );
        } else {
            raptor_material.normal_texture_index = k_invalid_scene_texture_index;
        }

        raptor_material.roughness_texture_index = k_invalid_scene_texture_index;
        raptor_material.occlusion_texture_index = k_invalid_scene_texture_index;

        aiColor4D color;
        if ( aiGetMaterialColor( material, AI_MATKEY_COLOR_DIFFUSE, &color ) == AI_SUCCESS ) {
            raptor_material.diffuse_colour = { color.r, color.g, color.b, 1.0f };
        }

        if ( aiGetMaterialColor( material, AI_MATKEY_COLOR_AMBIENT, &color ) == AI_SUCCESS ) {
            raptor_material.ambient_colour = { color.r, color.g, color.b };
        }

        if ( aiGetMaterialColor( material, AI_MATKEY_COLOR_SPECULAR, &color ) == AI_SUCCESS ) {
            raptor_material.specular_colour = { color.r, color.g, color.b };
        }

        float f_value;
        if ( aiGetMaterialFloat( material, AI_MATKEY_SHININESS, &f_value ) == AI_SUCCESS ) {
            raptor_material.specular_exp = f_value;
        }

        if ( aiGetMaterialFloat( material, AI_MATKEY_OPACITY, &f_value ) == AI_SUCCESS ) {
            raptor_material.diffuse_colour.w = f_value;
        }

        materials.push( raptor_material );
    }

    i64 end_loading_textures_files = time_now();

    i64 end_creating_textures = time_now();

    const u32 k_num_buffers = 5;
    cpu_buffers.init( resident_allocator, k_num_buffers );
    gpu_buffers.init( resident_allocator, k_num_buffers );

    // Init runtime meshes
    meshes.init( resident_allocator, assimp_scene->mNumMeshes );

    Array<vec3s> positions;
    positions.init( resident_allocator, rkilo( 64 ) );
    sizet positions_offset = 0;

    Array<vec3s> tangents;
    tangents.init( resident_allocator, rkilo( 64 ) );
    sizet tangents_offset = 0;

    Array<vec3s> normals;
    normals.init( resident_allocator, rkilo( 64 ) );
    sizet normals_offset = 0;

    Array<vec2s> uv_coords;
    uv_coords.init( resident_allocator, rkilo( 64 ) );
    sizet uv_coords_offset = 0;

    Array<u32> indices;
    indices.init( resident_allocator, rkilo( 64 ) );
    sizet indices_offset = 0;

    for ( u32 mesh_index = 0; mesh_index < assimp_scene->mNumMeshes; ++mesh_index ) {
        aiMesh* mesh = assimp_scene->mMeshes[ mesh_index ];

        Mesh render_mesh{ };
        PhysicsMesh* physics_mesh = ( PhysicsMesh* )resident_allocator->allocate( sizeof( PhysicsMesh ), 64 );

        physics_mesh->vertices.init( resident_allocator, mesh->mNumVertices );

        RASSERT( ( mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE ) != 0 );

        for ( u32 vertex_index = 0; vertex_index < mesh->mNumVertices; ++vertex_index ) {
            vec3s position{
                mesh->mVertices[ vertex_index ].x,
                mesh->mVertices[ vertex_index ].y,
                mesh->mVertices[ vertex_index ].z
            };

            positions.push( position );

            PhysicsVertex physics_vertex{ };
            physics_vertex.start_position = position;
            physics_vertex.previous_position = position;
            physics_vertex.position = position;
            physics_vertex.mass = 1.0f;
            physics_vertex.fixed = false;

            vec3s normal = vec3s{
                mesh->mNormals[ vertex_index ].x,
                mesh->mNormals[ vertex_index ].y,
                mesh->mNormals[ vertex_index ].z
            };

            normals.push( normal );

            physics_vertex.normal = normal;

            tangents.push( vec3s{
                mesh->mTangents[ vertex_index ].x,
                mesh->mTangents[ vertex_index ].y,
                mesh->mTangents[ vertex_index ].z
            } );

            uv_coords.push( vec2s{
                mesh->mTextureCoords[ 0 ][ vertex_index ].x,
                mesh->mTextureCoords[ 0 ][ vertex_index ].y,
            } );

            physics_mesh->vertices.push( physics_vertex );
        }

        for ( u32 face_index = 0; face_index < mesh->mNumFaces; ++face_index ) {
            RASSERT( mesh->mFaces[ face_index ].mNumIndices == 3 );

            u32 index_a = mesh->mFaces[ face_index ].mIndices[ 0 ];
            u32 index_b = mesh->mFaces[ face_index ].mIndices[ 1 ];
            u32 index_c = mesh->mFaces[ face_index ].mIndices[ 2 ];

            indices.push( index_a );
            indices.push( index_b );
            indices.push( index_c );

            // NOTE(marco): compute cloth joints

            PhysicsVertex& vertex_a = physics_mesh->vertices[ index_a ];
            vertex_a.add_joint( index_b );
            vertex_a.add_joint( index_c );

            PhysicsVertex& vertex_b = physics_mesh->vertices[ index_b ];
            vertex_b.add_joint( index_a );
            vertex_b.add_joint( index_c );

            PhysicsVertex& vertex_c = physics_mesh->vertices[ index_c ];
            vertex_c.add_joint( index_a );
            vertex_c.add_joint( index_b );
        }

        for ( u32 face_index = 0; face_index < mesh->mNumFaces; ++face_index ) {
            u32 index_a = mesh->mFaces[ face_index ].mIndices[ 0 ];
            u32 index_b = mesh->mFaces[ face_index ].mIndices[ 1 ];
            u32 index_c = mesh->mFaces[ face_index ].mIndices[ 2 ];

            PhysicsVertex& vertex_a = physics_mesh->vertices[ index_a ];

            PhysicsVertex& vertex_b = physics_mesh->vertices[ index_b ];

            PhysicsVertex& vertex_c = physics_mesh->vertices[ index_c ];

            // NOTE(marco): check for adjacent triangles to get diagonal joints
            for ( u32 other_face_index = 0; other_face_index < mesh->mNumFaces; ++other_face_index ) {
                if ( other_face_index == face_index ) {
                    continue;
                }

                u32 other_index_a = mesh->mFaces[ other_face_index ].mIndices[ 0 ];
                u32 other_index_b = mesh->mFaces[ other_face_index ].mIndices[ 1 ];
                u32 other_index_c = mesh->mFaces[ other_face_index ].mIndices[ 2 ];

                // check for vertex_a
                if ( other_index_a == index_b && other_index_b == index_c ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_a, other_index_c ) ) {
                        vertex_a.add_joint( other_index_c );
                    }
                }
                if ( other_index_a == index_c && other_index_b == index_b ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_a, other_index_c ) ) {
                        vertex_a.add_joint( other_index_c );
                    }
                }
                if ( other_index_a == index_b && other_index_c == index_c ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_a, other_index_b ) ) {
                        vertex_a.add_joint( other_index_b );
                    }
                }
                if ( other_index_a == index_c && other_index_c == index_b ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_a, other_index_b ) ) {
                        vertex_a.add_joint( other_index_b );
                    }
                }
                if ( other_index_c == index_b && other_index_b == index_c ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_a, other_index_a ) ) {
                        vertex_a.add_joint( other_index_a );
                    }
                }
                if ( other_index_c == index_c && other_index_b == index_b ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_a, other_index_a ) ) {
                        vertex_a.add_joint( other_index_a );
                    }
                }

                // check for vertex_b
                if ( other_index_a == index_a && other_index_b == index_c ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_b, other_index_c ) ) {
                        vertex_b.add_joint( other_index_c );
                    }
                }
                if ( other_index_a == index_c && other_index_b == index_a ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_b, other_index_c ) ) {
                        vertex_b.add_joint( other_index_c );
                    }
                }
                if ( other_index_a == index_a && other_index_c == index_c ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_b, other_index_b ) ) {
                        vertex_b.add_joint( other_index_b );
                    }
                }
                if ( other_index_a == index_c && other_index_c == index_a ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_b, other_index_b ) ) {
                        vertex_b.add_joint( other_index_b );
                    }
                }
                if ( other_index_c == index_a && other_index_b == index_c ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_b, other_index_a ) ) {
                        vertex_b.add_joint( other_index_a );
                    }
                }
                if ( other_index_c == index_c && other_index_b == index_a ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_b, other_index_a) ) {
                        vertex_b.add_joint( other_index_a );
                    }
                }

                // check for vertex_c
                if ( other_index_a == index_a && other_index_b == index_b ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_c, other_index_c ) ) {
                        vertex_c.add_joint( other_index_c );
                    }
                }
                if ( other_index_a == index_b && other_index_b == index_a ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_c, other_index_c ) ) {
                        vertex_c.add_joint( other_index_c );
                    }
                }
                if ( other_index_a == index_a && other_index_c == index_b ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_c, other_index_b ) ) {
                        vertex_c.add_joint( other_index_b );
                    }
                }
                if ( other_index_a == index_b && other_index_c == index_a ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_c, other_index_b ) ) {
                        vertex_c.add_joint( other_index_b );
                    }
                }

                if ( other_index_c == index_a && other_index_b == index_b ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_c, other_index_a ) ) {
                        vertex_c.add_joint( other_index_a );
                    }
                }
                if ( other_index_c == index_b && other_index_b == index_a ) {
                    if ( is_shared_vertex( physics_mesh->vertices.data, vertex_c, other_index_a ) ) {
                        vertex_c.add_joint( other_index_a );
                    }
                }
            }
        }

        render_mesh.position_offset = positions_offset;
        positions_offset = positions.size * sizeof( vec3s );

        render_mesh.tangent_offset = tangents_offset;
        tangents_offset = tangents.size * sizeof( vec3s );

        render_mesh.normal_offset = normals_offset;
        normals_offset = normals.size * sizeof( vec3s );

        render_mesh.texcoord_offset = uv_coords_offset;
        uv_coords_offset = uv_coords.size * sizeof( vec2s );

        render_mesh.index_offset = indices_offset;
        indices_offset = indices.size * sizeof( u32 );
        render_mesh.index_type = VK_INDEX_TYPE_UINT32;

        render_mesh.primitive_count = mesh->mNumFaces * 3;

        render_mesh.physics_mesh = physics_mesh;

        render_mesh.pbr_material = materials[ mesh->mMaterialIndex ];
        render_mesh.pbr_material.flags = DrawFlags_HasNormals;
        render_mesh.pbr_material.flags |= DrawFlags_HasTangents;
        render_mesh.pbr_material.flags |= DrawFlags_HasTexCoords;

        {
            BufferCreation creation{ };
            creation.set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuMeshData ) ).set_name( "mesh_data" );

            render_mesh.pbr_material.material_buffer = renderer->gpu->create_buffer( creation );
        }

        // Physics data
        {
            BufferCreation creation{ };
            sizet buffer_size = positions.size * sizeof( PhysicsVertexGpuData ) + sizeof( PhysicsMeshGpuData );
            creation.set( VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( nullptr ).set_name( "physics_mesh_data_cpu" ).set_persistent( true );

            BufferHandle cpu_buffer = renderer->gpu->create_buffer( creation );

            Buffer* physics_vertex_buffer = renderer->gpu->access_buffer( cpu_buffer );

            PhysicsMeshGpuData* mesh_data = ( PhysicsMeshGpuData* )( physics_vertex_buffer->mapped_data );
            mesh_data->index_count = render_mesh.primitive_count;
            mesh_data->vertex_count = positions.size;

            PhysicsVertexGpuData* vertex_data = ( PhysicsVertexGpuData* )( physics_vertex_buffer->mapped_data + sizeof( PhysicsMeshGpuData ) );

            Array<VkDrawIndirectCommand> indirect_commands;
            indirect_commands.init( resident_allocator, physics_mesh->vertices.size, physics_mesh->vertices.size );

            // TODO(marco): some of these might change at runtime
            for ( u32 vertex_index = 0; vertex_index < physics_mesh->vertices.size; ++vertex_index ) {
                PhysicsVertex& cpu_data = physics_mesh->vertices[ vertex_index ];

                VkDrawIndirectCommand& indirect_command = indirect_commands[ vertex_index ];

                PhysicsVertexGpuData gpu_data{ };
                gpu_data.position = cpu_data.position;
                gpu_data.start_position = cpu_data.start_position;
                gpu_data.previous_position = cpu_data.previous_position;
                gpu_data.normal = cpu_data.normal;
                gpu_data.joint_count = cpu_data.joint_count;
                gpu_data.velocity = cpu_data.velocity;
                gpu_data.mass = cpu_data.mass;
                gpu_data.force = cpu_data.force;

                for ( u32 j = 0; j < cpu_data.joint_count; ++j ) {
                    gpu_data.joints[ j ] = cpu_data.joints[ j ].vertex_index;
                }

                indirect_command.vertexCount = 2;
                indirect_command.instanceCount = cpu_data.joint_count;
                indirect_command.firstVertex = 0;
                indirect_command.firstInstance = 0;

                vertex_data[ vertex_index ] = gpu_data;
            }

            creation.reset().set( VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_device_only( true ).set_name( "physics_mesh_data_gpu" );

            BufferResource* gpu_buffer = renderer->create_buffer( creation );
            gpu_buffers.push( *gpu_buffer );

            physics_mesh->gpu_buffer = gpu_buffer->handle;

            async_loader->request_buffer_copy( cpu_buffer, gpu_buffer->handle );

            // NOTE(marco): indirect command data
            buffer_size = sizeof( VkDrawIndirectCommand ) * indirect_commands.size;
            creation.reset().set( VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( indirect_commands.data ).set_name( "indirect_buffer_cpu" );

            cpu_buffer = renderer->gpu->create_buffer( creation );

            creation.reset().set( VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, ResourceUsageType::Immutable, buffer_size ).set_device_only( true ).set_name( "indirect_buffer_gpu" );

            gpu_buffer = renderer->create_buffer( creation );
            gpu_buffers.push( *gpu_buffer );

            physics_mesh->draw_indirect_buffer = gpu_buffer->handle;

            async_loader->request_buffer_copy( cpu_buffer, gpu_buffer->handle );

            indirect_commands.shutdown();
        }

        meshes.push( render_mesh );
    }

    materials.shutdown();

    VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    // Positions
    {
        BufferCreation creation{ };
        sizet buffer_size = positions.size * sizeof( vec3s );
        creation.set( VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( positions.data ).set_name( "obj_positions" ).set_persistent( true );

        BufferHandle cpu_buffer = renderer->gpu->create_buffer( creation );

        creation.reset().set( flags, ResourceUsageType::Immutable, buffer_size ).set_device_only( true ).set_name( "position_attribute_buffer" );

        BufferResource* gpu_buffer = renderer->create_buffer( creation );
        gpu_buffers.push( *gpu_buffer );

        // TODO(marco): ideally the CPU buffer would be using staging memory
        async_loader->request_buffer_copy( cpu_buffer, gpu_buffer->handle );
    }

    // Tangents
    {
        BufferCreation creation{ };
        sizet buffer_size = tangents.size * sizeof( vec3s );
        creation.set( VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( tangents.data ).set_name( "obj_tangents" ).set_persistent( true );

        BufferHandle cpu_buffer = renderer->gpu->create_buffer( creation );

        creation.reset().set( flags, ResourceUsageType::Immutable, buffer_size ).set_device_only( true ).set_name( "tangent_attribute_buffer" );

        BufferResource* gpu_buffer = renderer->create_buffer( creation );
        gpu_buffers.push( *gpu_buffer );

        async_loader->request_buffer_copy( cpu_buffer, gpu_buffer->handle );
    }

    // Normals
    {
        BufferCreation creation{ };
        sizet buffer_size = normals.size * sizeof( vec3s );
        creation.set( VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( normals.data ).set_name( "obj_normals" ).set_persistent( true );

        BufferHandle cpu_buffer = renderer->gpu->create_buffer( creation );

        creation.reset().set( flags, ResourceUsageType::Immutable, buffer_size ).set_device_only( true ).set_name( "normal_attribute_buffer" );

        BufferResource* gpu_buffer = renderer->create_buffer( creation );
        gpu_buffers.push( *gpu_buffer );

        async_loader->request_buffer_copy( cpu_buffer, gpu_buffer->handle );
    }

    // TexCoords
    {
        BufferCreation creation{ };
        sizet buffer_size = uv_coords.size * sizeof( vec2s );
        creation.set( VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( uv_coords.data ).set_name( "obj_tex_coords" );

        BufferHandle cpu_buffer = renderer->gpu->create_buffer( creation );

        creation.reset().set( flags, ResourceUsageType::Immutable, buffer_size ).set_device_only( true ).set_name( "texcoords_attribute_buffer" );

        BufferResource* gpu_buffer = renderer->create_buffer( creation );
        gpu_buffers.push( *gpu_buffer );

        async_loader->request_buffer_copy( cpu_buffer, gpu_buffer->handle );
    }

    // Indices
    {
        BufferCreation creation{ };
        sizet buffer_size = indices.size * sizeof( u32 );
        creation.set( VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ResourceUsageType::Immutable, buffer_size ).set_data( indices.data ).set_name( "obj_indices" ).set_persistent( true );

        BufferHandle cpu_buffer = renderer->gpu->create_buffer( creation );

        creation.reset().set( flags, ResourceUsageType::Immutable, buffer_size ).set_device_only( true ).set_name( "index_buffer" );

        BufferResource* gpu_buffer = renderer->create_buffer( creation );
        gpu_buffers.push( *gpu_buffer );

        async_loader->request_buffer_copy( cpu_buffer, gpu_buffer->handle );
    }

    positions.shutdown();
    normals.shutdown();
    uv_coords.shutdown();
    tangents.shutdown();
    indices.shutdown();

    temp_allocator->free_marker( temp_allocator_initial_marker );

    animations.init( resident_allocator, 0 );
    skins.init( resident_allocator, 0 );

    i64 end_reading_buffers_data = time_now();

    i64 end_creating_buffers = time_now();

    i64 end_loading = time_now();

    rprint( "Loaded scene %s in %f seconds.\nStats:\n\tReading GLTF file %f seconds\n\tTextures Creating %f seconds\n\tReading Buffers Data %f seconds\n\tCreating Buffers %f seconds\n", filename,
            time_delta_seconds( start_scene_loading, end_loading ), time_delta_seconds( start_scene_loading, end_loading_file ), time_delta_seconds( end_loading_file, end_creating_textures ),
            time_delta_seconds( end_creating_textures, end_reading_buffers_data ), time_delta_seconds( end_reading_buffers_data, end_creating_buffers ) );
}

u32 ObjScene::load_texture( cstring texture_path, cstring path, StackAllocator* temp_allocator ) {
    using namespace raptor;

    int comp, width, height;

    stbi_info( texture_path, &width, &height, &comp );

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
    tc.set_data( nullptr ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D ).set_flags( mip_levels, 0 ).set_size( ( u16 )width, ( u16 )height, 1 ).set_name( nullptr );
    TextureResource* tr = renderer->create_texture( tc );
    RASSERT( tr != nullptr );

    images.push( *tr );

    renderer->gpu->link_texture_sampler( tr->handle, sampler->handle );

    StringBuffer name_buffer;
    name_buffer.init( 4096, temp_allocator );

    // Reconstruct file path
    char* full_filename = name_buffer.append_use_f( "%s%s", path, texture_path );
    async_loader->request_texture_data( full_filename, tr->handle );
    // Reset name buffer
    name_buffer.clear();

    return tr->handle.index;
}

void ObjScene::shutdown( Renderer* renderer ) {
    GpuDevice& gpu = *renderer->gpu;

    for ( u32 mesh_index = 0; mesh_index < meshes.size; ++mesh_index ) {
        Mesh& mesh = meshes[ mesh_index ];

        gpu.destroy_buffer( mesh.pbr_material.material_buffer );
        gpu.destroy_descriptor_set( mesh.pbr_material.descriptor_set );

        PhysicsMesh* physics_mesh = mesh.physics_mesh;

        if ( physics_mesh != nullptr ) {
            gpu.destroy_descriptor_set( physics_mesh->descriptor_set );
            gpu.destroy_descriptor_set( physics_mesh->debug_mesh_descriptor_set );

            physics_mesh->vertices.shutdown();

            resident_allocator->deallocate( physics_mesh );
        }
    }

    gpu.destroy_buffer( scene_cb );
    gpu.destroy_buffer( physics_cb );

    for ( u32 i = 0; i < images.size; ++i) {
        renderer->destroy_texture( &images[ i ] );
    }

    renderer->destroy_sampler( sampler );

    for ( u32 i = 0; i < cpu_buffers.size; ++i ) {
        renderer->destroy_buffer( &cpu_buffers[ i ] );
    }

    for ( u32 i = 0; i < gpu_buffers.size; ++i ) {
        renderer->destroy_buffer( &gpu_buffers[ i ] );
    }

    meshes.shutdown();

    // Free scene buffers
    images.shutdown();
    cpu_buffers.shutdown();
    gpu_buffers.shutdown();
}

void ObjScene::prepare_draws( Renderer* renderer, StackAllocator* scratch_allocator, SceneGraph* scene_graph_ ) {
    ZoneScoped;

    using namespace raptor;

    scene_graph = scene_graph_;

    // Create pipeline state
    PipelineCreation pipeline_creation;

    sizet cached_scratch_size = scratch_allocator->get_marker();

    StringBuffer path_buffer;
    path_buffer.init( 1024, scratch_allocator );

    // Create material
    const u64 main_hashed_name = hash_calculate( "main" );
    GpuTechnique* main_technique = renderer->resource_cache.techniques.get( main_hashed_name );

    MaterialCreation material_creation;
    material_creation.set_name( "material_no_cull_opaque" ).set_technique( main_technique ).set_render_index( 0 );

    Material* pbr_material = renderer->create_material( material_creation );

    const u64 cloth_hashed_name = hash_calculate( "cloth" );
    GpuTechnique* cloth_technique = renderer->resource_cache.techniques.get( cloth_hashed_name );

    const u64 debug_hashed_name = hash_calculate( "debug" );
    GpuTechnique* debug_technique = renderer->resource_cache.techniques.get( debug_hashed_name );

    // Constant buffer
    BufferCreation buffer_creation;
    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( GpuSceneData ) ).set_name( "scene_cb" );
    scene_cb = renderer->gpu->create_buffer( buffer_creation );

    buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( PhysicsSceneData ) ).set_name( "physics_cb" );
    physics_cb = renderer->gpu->create_buffer( buffer_creation );

    // Add a dummy single node used by all meshes.
    scene_graph->resize( 1 );
    scene_graph->set_local_matrix( 0, glms_mat4_identity() );
    scene_graph->set_debug_data( 0, "Dummy" );

    // TODO(marco): not all meshes will create physics buffers
    u32 buffer_index_offset = meshes.size * 2;
    for ( u32 mesh_index = 0; mesh_index < meshes.size; ++mesh_index ) {
        Mesh& mesh = meshes[ mesh_index ];

        mesh.position_buffer = gpu_buffers[ buffer_index_offset + 0 ].handle;
        mesh.tangent_buffer = gpu_buffers[ buffer_index_offset + 1 ].handle;
        mesh.normal_buffer = gpu_buffers[ buffer_index_offset + 2 ].handle;
        mesh.texcoord_buffer = gpu_buffers[ buffer_index_offset + 3 ].handle;
        mesh.index_buffer = gpu_buffers[ buffer_index_offset + 4 ].handle;

        mesh.scene_graph_node_index = 0;
        mesh.pbr_material.material = pbr_material;

        mesh.pbr_material.flags |= DrawFlags_Phong;
        if ( mesh.pbr_material.diffuse_colour.w < 1.0f ) {
            mesh.pbr_material.flags |= DrawFlags_Transparent;
        }

        // Descriptor set
        const u32 pass_index = mesh.has_skinning() ? 5 : 3;

        DescriptorSetCreation ds_creation{};
        DescriptorSetLayoutHandle main_layout = renderer->gpu->get_descriptor_set_layout( mesh.pbr_material.material->technique->passes[ pass_index ].pipeline, k_material_descriptor_set_index );
        ds_creation.reset().buffer( scene_cb, 0 ).buffer( mesh.pbr_material.material_buffer, 2 ).set_layout( main_layout );
        mesh.pbr_material.descriptor_set = renderer->gpu->create_descriptor_set( ds_creation );

        if ( mesh.physics_mesh != nullptr ) {
            DescriptorSetLayoutHandle physics_layout = renderer->gpu->get_descriptor_set_layout( cloth_technique->passes[ 0 ].pipeline, k_material_descriptor_set_index );
            ds_creation.reset().buffer( physics_cb, 0 ).buffer( mesh.physics_mesh->gpu_buffer, 1 ).buffer( mesh.position_buffer, 2 ).buffer( mesh.normal_buffer, 3 ).buffer( mesh.index_buffer, 4 ).set_layout( physics_layout );

            mesh.physics_mesh->descriptor_set = renderer->gpu->create_descriptor_set( ds_creation );

            DescriptorSetLayoutHandle debug_mesh_layout = renderer->gpu->get_descriptor_set_layout( debug_technique->passes[ 0 ].pipeline, k_material_descriptor_set_index );
            ds_creation.reset().buffer(scene_cb, 0).buffer( mesh.physics_mesh->gpu_buffer, 1 ).set_layout( debug_mesh_layout );

            mesh.physics_mesh->debug_mesh_descriptor_set = renderer->gpu->create_descriptor_set( ds_creation );
        }
    }

    // We're done. Release all resources associated with this import
    aiReleaseImport( assimp_scene );
    assimp_scene = nullptr;
}

} // namespace raptor
