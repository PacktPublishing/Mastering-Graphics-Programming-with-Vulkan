#include "gltf.hpp"

#include "external/json.hpp"

#include "assert.hpp"
#include "file.hpp"

using json = nlohmann::json;

namespace raptor {

static void* allocate_and_zero( Allocator* allocator, sizet size ) {
    void* result = allocator->allocate( size, 64 );
    memset( result, 0, size );

    return result;
}

static void try_load_string( json& json_data, cstring key, StringBuffer& string_buffer, Allocator* allocator ) {
    auto it = json_data.find( key );
    if ( it == json_data.end() )
        return;

    std::string value = json_data.value( key, "" );

    string_buffer.init( value.length() + 1, allocator );
    string_buffer.append( value.c_str() );
}

static void try_load_int( json& json_data, cstring key, i32& value ) {
    auto it = json_data.find( key );
    if ( it == json_data.end() )
    {
        value = glTF::INVALID_INT_VALUE;
        return;
    }

    value = json_data.value( key, 0 );
}

static void try_load_float( json& json_data, cstring key, f32& value ) {
    auto it = json_data.find( key );
    if ( it == json_data.end() )
    {
        value = glTF::INVALID_FLOAT_VALUE;
        return;
    }

    value = json_data.value( key, 0.0f );
}

static void try_load_bool( json& json_data, cstring key, bool& value ) {
    auto it = json_data.find( key );
    if ( it == json_data.end() )
    {
        value = false;
        return;
    }

    value = json_data.value( key, false );
}

static void try_load_type( json& json_data, cstring key, glTF::Accessor::Type& type ) {
    std::string value = json_data.value( key, "" );
    if ( value == "SCALAR" ) {
        type = glTF::Accessor::Type::Scalar;
    }
    else if ( value == "VEC2" ) {
        type = glTF::Accessor::Type::Vec2;
    }
    else if ( value == "VEC3" ) {
        type = glTF::Accessor::Type::Vec3;
    }
    else if ( value == "VEC4" ) {
        type = glTF::Accessor::Type::Vec4;
    }
    else if ( value == "MAT2" ) {
        type = glTF::Accessor::Type::Mat2;
    }
    else if ( value == "MAT3" ) {
        type = glTF::Accessor::Type::Mat3;
    }
    else if ( value == "MAT4" ) {
        type = glTF::Accessor::Type::Mat4;
    }
    else {
        RASSERT( false );
    }
}

static void try_load_int_array( json& json_data, cstring key, u32& count, i32** array, Allocator* allocator ) {
    auto it = json_data.find( key );
    if ( it == json_data.end() ) {
        count = 0;
        *array = nullptr;
        return;
    }

    json json_array = json_data.at( key );

    count = json_array.size();

    i32* values = ( i32* )allocate_and_zero( allocator, sizeof( i32 ) * count );

    for ( sizet i = 0; i < count; ++i ) {
        values[ i ] = json_array.at( i );
    }

    *array = values;
}

static void try_load_float_array( json& json_data, cstring key, u32& count, float** array, Allocator* allocator ) {
    auto it = json_data.find( key );
    if ( it == json_data.end() ) {
        count = 0;
        *array = nullptr;
        return;
    }

    json json_array = json_data.at( key );

    count = json_array.size();

    float* values = ( float* )allocate_and_zero( allocator, sizeof( float ) * count );

    for ( sizet i = 0; i < count; ++i ) {
        values[ i ] = json_array.at( i );
    }

    *array = values;
}

static void load_asset( json& json_data, glTF::Asset& asset, Allocator* allocator ) {
    json json_asset = json_data[ "asset" ];

    try_load_string( json_asset, "copyright", asset.copyright, allocator );
    try_load_string( json_asset, "generator", asset.generator, allocator );
    try_load_string( json_asset, "minVersion", asset.minVersion, allocator );
    try_load_string( json_asset, "version", asset.version, allocator );
}

static void load_scene( json& json_data, glTF::Scene& scene, Allocator* allocator ) {
    try_load_int_array( json_data, "nodes", scene.nodes_count, &scene.nodes, allocator );
}

static void load_scenes( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json scenes = json_data[ "scenes" ];

    sizet scene_count = scenes.size();
    gltf_data.scenes = ( glTF::Scene* )allocate_and_zero( allocator, sizeof( glTF::Scene ) * scene_count );
    gltf_data.scenes_count = scene_count;

    for ( sizet i = 0; i < scene_count; ++i ) {
        load_scene( scenes[ i ], gltf_data.scenes[ i ], allocator );
    }
}

static void load_buffer( json& json_data, glTF::Buffer& buffer, Allocator* allocator ) {
    try_load_string( json_data, "uri", buffer.uri, allocator );
    try_load_int( json_data, "byteLength", buffer.byte_length );
    try_load_string( json_data, "name", buffer.name, allocator );
}

static void load_buffers( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json buffers = json_data[ "buffers" ];

    sizet buffer_count = buffers.size();
    gltf_data.buffers = ( glTF::Buffer* )allocate_and_zero( allocator, sizeof( glTF::Buffer ) * buffer_count );
    gltf_data.buffers_count = buffer_count;

    for ( sizet i = 0; i < buffer_count; ++i ) {
        load_buffer( buffers[ i ], gltf_data.buffers[ i ], allocator );
    }
}

static void load_buffer_view( json& json_data, glTF::BufferView& buffer_view, Allocator* allocator ) {
    try_load_int( json_data, "buffer", buffer_view.buffer );
    try_load_int( json_data, "byteLength", buffer_view.byte_length );
    try_load_int( json_data, "byteOffset", buffer_view.byte_offset );
    try_load_int( json_data, "byteStride", buffer_view.byte_stride );
    try_load_int( json_data, "target", buffer_view.target );
    try_load_string( json_data, "name", buffer_view.name, allocator );
}

static void load_buffer_views( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json buffers = json_data[ "bufferViews" ];

    sizet buffer_count = buffers.size();
    gltf_data.buffer_views = ( glTF::BufferView* )allocate_and_zero( allocator, sizeof( glTF::BufferView ) * buffer_count );
    gltf_data.buffer_views_count = buffer_count;

    for ( sizet i = 0; i < buffer_count; ++i ) {
        load_buffer_view( buffers[ i ], gltf_data.buffer_views[ i ], allocator );
    }
}

static void load_node( json& json_data, glTF::Node& node, Allocator* allocator ) {
    try_load_int( json_data, "camera", node.camera );
    try_load_int( json_data, "mesh", node.mesh );
    try_load_int( json_data, "skin", node.skin );
    try_load_int_array( json_data, "children", node.children_count, &node.children, allocator );
    try_load_float_array( json_data, "matrix", node.matrix_count, &node.matrix, allocator );
    try_load_float_array( json_data, "rotation", node.rotation_count, &node.rotation, allocator );
    try_load_float_array( json_data, "scale", node.scale_count, &node.scale, allocator );
    try_load_float_array( json_data, "translation", node.translation_count, &node.translation, allocator );
    try_load_float_array( json_data, "weights", node.weights_count, &node.weights, allocator );
    try_load_string( json_data, "name", node.name, allocator );
}

static void load_nodes( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json array = json_data[ "nodes" ];

    sizet array_count = array.size();
    gltf_data.nodes = ( glTF::Node* )allocate_and_zero( allocator, sizeof( glTF::Node ) * array_count );
    gltf_data.nodes_count = array_count;

    for ( sizet i = 0; i < array_count; ++i ) {
        load_node( array[ i ], gltf_data.nodes[ i ], allocator );
    }
}

static void load_mesh_primitive( json& json_data, glTF::MeshPrimitive& mesh_primitive, Allocator* allocator ) {
    try_load_int( json_data, "indices", mesh_primitive.indices );
    try_load_int( json_data, "material", mesh_primitive.material );
    try_load_int( json_data, "mode", mesh_primitive.mode );

    json attributes = json_data[ "attributes" ];

    mesh_primitive.attributes = ( glTF::MeshPrimitive::Attribute* )allocate_and_zero( allocator, sizeof( glTF::MeshPrimitive::Attribute ) * attributes.size() );
    mesh_primitive.attribute_count = attributes.size();

    u32 index = 0;
    for ( auto json_attribute : attributes.items() ) {
        std::string key = json_attribute.key();
        glTF::MeshPrimitive::Attribute& attribute = mesh_primitive.attributes[ index ];

        attribute.key.init( key.size() + 1, allocator );
        attribute.key.append( key.c_str() );

        attribute.accessor_index = json_attribute.value();

        ++index;
    }
}

static void load_mesh_primitives( json& json_data, glTF::Mesh& mesh, Allocator* allocator ) {
    json array = json_data[ "primitives" ];

    sizet array_count = array.size();
    mesh.primitives = ( glTF::MeshPrimitive* )allocate_and_zero( allocator, sizeof( glTF::MeshPrimitive ) * array_count );
    mesh.primitives_count = array_count;

    for ( sizet i = 0; i < array_count; ++i ) {
        load_mesh_primitive( array[ i ], mesh.primitives[ i ], allocator );
    }
}

static void load_mesh( json& json_data, glTF::Mesh& mesh, Allocator* allocator ) {
    load_mesh_primitives( json_data, mesh, allocator );
    try_load_float_array( json_data, "weights", mesh.weights_count, &mesh.weights, allocator );
    try_load_string( json_data, "name", mesh.name, allocator );
}

static void load_meshes( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json array = json_data[ "meshes" ];

    sizet array_count = array.size();
    gltf_data.meshes = ( glTF::Mesh* )allocate_and_zero( allocator, sizeof( glTF::Mesh ) * array_count );
    gltf_data.meshes_count = array_count;

    for ( sizet i = 0; i < array_count; ++i ) {
        load_mesh( array[ i ], gltf_data.meshes[ i ], allocator );
    }
}

static void load_accessor( json& json_data, glTF::Accessor& accessor, Allocator* allocator ) {
    try_load_int( json_data, "bufferView", accessor.buffer_view );
    try_load_int( json_data, "byteOffset", accessor.byte_offset );
    try_load_int( json_data, "componentType", accessor.component_type );
    try_load_int( json_data, "count", accessor.count );
    try_load_int( json_data, "sparse", accessor.sparse );
    try_load_float_array( json_data, "max", accessor.max_count, &accessor.max, allocator );
    try_load_float_array( json_data, "min", accessor.min_count, &accessor.min, allocator );
    try_load_bool( json_data, "normalized", accessor.normalized );
    try_load_type( json_data, "type", accessor.type );
}

static void load_accessors( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json array = json_data[ "accessors" ];

    sizet array_count = array.size();
    gltf_data.accessors = ( glTF::Accessor* )allocate_and_zero( allocator, sizeof( glTF::Accessor ) * array_count );
    gltf_data.accessors_count = array_count;

    for ( sizet i = 0; i < array_count; ++i ) {
        load_accessor( array[ i ], gltf_data.accessors[ i ], allocator );
    }
}

static void try_load_TextureInfo( json& json_data, cstring key, glTF::TextureInfo** texture_info, Allocator* allocator ) {
    auto it = json_data.find( key );
    if ( it == json_data.end() ) {
        *texture_info = nullptr;
        return;
    }

    glTF::TextureInfo* ti = ( glTF::TextureInfo* ) allocator->allocate( sizeof( glTF::TextureInfo ), 64 );

    try_load_int( *it, "index", ti->index );
    try_load_int( *it, "texCoord", ti->texCoord );

    *texture_info = ti;
}

static void try_load_MaterialNormalTextureInfo( json& json_data, cstring key, glTF::MaterialNormalTextureInfo** texture_info, Allocator* allocator ) {
    auto it = json_data.find( key );
    if ( it == json_data.end() ) {
        *texture_info = nullptr;
        return;
    }

    glTF::MaterialNormalTextureInfo* ti = ( glTF::MaterialNormalTextureInfo* ) allocator->allocate( sizeof( glTF::MaterialNormalTextureInfo ), 64 );

    try_load_int( *it, "index", ti->index );
    try_load_int( *it, "texCoord", ti->tex_coord );
    try_load_float( *it, "scale", ti->scale );

    *texture_info = ti;
}

static void try_load_MaterialOcclusionTextureInfo( json& json_data, cstring key, glTF::MaterialOcclusionTextureInfo** texture_info, Allocator* allocator ) {
    auto it = json_data.find( key );
    if ( it == json_data.end() ) {
        *texture_info = nullptr;
        return;
    }

    glTF::MaterialOcclusionTextureInfo* ti = ( glTF::MaterialOcclusionTextureInfo* ) allocator->allocate( sizeof( glTF::MaterialOcclusionTextureInfo ), 64 );

    try_load_int( *it, "index", ti->index );
    try_load_int( *it, "texCoord", ti->texCoord );
    try_load_float( *it, "strength", ti->strength );

    *texture_info = ti;
}

static void try_load_MaterialPBRMetallicRoughness( json& json_data, cstring key, glTF::MaterialPBRMetallicRoughness** texture_info, Allocator* allocator ) {
    auto it = json_data.find( key );
    if ( it == json_data.end() )
    {
        *texture_info = nullptr;
        return;
    }

    glTF::MaterialPBRMetallicRoughness* ti = ( glTF::MaterialPBRMetallicRoughness* ) allocator->allocate( sizeof( glTF::MaterialPBRMetallicRoughness ), 64 );

    try_load_float_array( *it, "baseColorFactor", ti->base_color_factor_count, &ti->base_color_factor, allocator );
    try_load_TextureInfo( *it, "baseColorTexture", &ti->base_color_texture, allocator );
    try_load_float( *it, "metallicFactor", ti->metallic_factor );
    try_load_TextureInfo( *it, "metallicRoughnessTexture", &ti->metallic_roughness_texture, allocator );
    try_load_float( *it, "roughnessFactor", ti->roughness_factor );

    *texture_info = ti;
}

static void load_material( json& json_data, glTF::Material& material, Allocator* allocator ) {
    try_load_float_array( json_data, "emissiveFactor", material.emissive_factor_count, &material.emissive_factor, allocator );
    try_load_float( json_data, "alphaCutoff", material.alpha_cutoff );
    try_load_string( json_data, "alphaMode", material.alpha_mode, allocator );
    try_load_bool( json_data, "doubleSided", material.double_sided );

    try_load_TextureInfo( json_data, "emissiveTexture", &material.emissive_texture, allocator );
    try_load_MaterialNormalTextureInfo( json_data, "normalTexture", &material.normal_texture, allocator );
    try_load_MaterialOcclusionTextureInfo( json_data, "occlusionTexture", &material.occlusion_texture, allocator );
    try_load_MaterialPBRMetallicRoughness( json_data, "pbrMetallicRoughness", &material.pbr_metallic_roughness, allocator );

    try_load_string( json_data, "name", material.name, allocator );
}

static void load_materials( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json array = json_data[ "materials" ];

    sizet array_count = array.size();
    gltf_data.materials = ( glTF::Material* )allocate_and_zero( allocator, sizeof( glTF::Material ) * array_count );
    gltf_data.materials_count = array_count;

    for ( sizet i = 0; i < array_count; ++i ) {
        load_material( array[ i ], gltf_data.materials[ i ], allocator );
    }
}

static void load_texture( json& json_data, glTF::Texture& texture, Allocator* allocator ) {
    try_load_int( json_data, "sampler", texture.sampler );
    try_load_int( json_data, "source", texture.source );
    try_load_string( json_data, "name", texture.name, allocator );
}

static void load_textures( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json array = json_data[ "textures" ];

    sizet array_count = array.size();
    gltf_data.textures = ( glTF::Texture* )allocate_and_zero( allocator, sizeof( glTF::Texture ) * array_count );
    gltf_data.textures_count = array_count;

    for ( sizet i = 0; i < array_count; ++i ) {
        load_texture( array[ i ], gltf_data.textures[ i ], allocator );
    }
}

static void load_image( json& json_data, glTF::Image& image, Allocator* allocator ) {
    try_load_int( json_data, "bufferView", image.buffer_view );
    try_load_string( json_data, "mimeType", image.mime_type, allocator );
    try_load_string( json_data, "uri", image.uri, allocator );
}

static void load_images( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json array = json_data[ "images" ];

    sizet array_count = array.size();
    gltf_data.images = ( glTF::Image* )allocate_and_zero( allocator, sizeof( glTF::Image ) * array_count );
    gltf_data.images_count = array_count;

    for ( sizet i = 0; i < array_count; ++i ) {
        load_image( array[ i ], gltf_data.images[ i ], allocator );
    }
}

static void load_sampler( json& json_data, glTF::Sampler& sampler, Allocator* allocator ) {
    try_load_int( json_data, "magFilter", sampler.mag_filter );
    try_load_int( json_data, "minFilter", sampler.min_filter );
    try_load_int( json_data, "wrapS", sampler.wrap_s );
    try_load_int( json_data, "wrapT", sampler.wrap_t );
}

static void load_samplers( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json array = json_data[ "samplers" ];

    sizet array_count = array.size();
    gltf_data.samplers = ( glTF::Sampler* )allocate_and_zero( allocator, sizeof( glTF::Sampler ) * array_count );
    gltf_data.samplers_count = array_count;

    for ( sizet i = 0; i < array_count; ++i ) {
        load_sampler( array[ i ], gltf_data.samplers[ i ], allocator );
    }
}

static void load_skin( json& json_data, glTF::Skin& skin, Allocator* allocator ) {
    try_load_int( json_data, "skeleton", skin.skeleton_root_node_index );
    try_load_int( json_data, "inverseBindMatrices", skin.inverse_bind_matrices_buffer_index );
    try_load_int_array( json_data, "joints", skin.joints_count, &skin.joints, allocator );
}

static void load_skins( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json array = json_data[ "skins" ];

    sizet array_count = array.size();
    gltf_data.skins = ( glTF::Skin* )allocate_and_zero( allocator, sizeof( glTF::Skin ) * array_count );
    gltf_data.skins_count = array_count;

    for ( sizet i = 0; i < array_count; ++i ) {
        load_skin( array[ i ], gltf_data.skins[ i ], allocator );
    }
}

static void load_animation( json& json_data, glTF::Animation& animation, Allocator* allocator ) {

    json json_array = json_data.at( "samplers" );
    if ( json_array.is_array() ) {
        sizet count = json_array.size();

        glTF::AnimationSampler* values = ( glTF::AnimationSampler* )allocate_and_zero( allocator, sizeof( glTF::AnimationSampler ) * count );

        for ( sizet i = 0; i < count; ++i ) {
            json element = json_array.at( i );
            glTF::AnimationSampler& sampler = values[ i ];

            try_load_int( element, "input", sampler.input_keyframe_buffer_index );
            try_load_int( element, "output", sampler.output_keyframe_buffer_index );

            std::string value = element.value( "interpolation", "");
            if ( value == "LINEAR" ) {
                sampler.interpolation = glTF::AnimationSampler::Linear;
            }
            else if ( value == "STEP" ) {
                sampler.interpolation = glTF::AnimationSampler::Step;
            }
            else if ( value == "CUBICSPLINE" ) {
                sampler.interpolation = glTF::AnimationSampler::CubicSpline;
            }
            else {
                sampler.interpolation = glTF::AnimationSampler::Linear;
            }
        }

        animation.samplers = values;
        animation.samplers_count = count;
    }

    json_array = json_data.at( "channels" );
    if ( json_array.is_array() ) {
        sizet count = json_array.size();

        glTF::AnimationChannel* values = ( glTF::AnimationChannel* )allocate_and_zero( allocator, sizeof( glTF::AnimationChannel ) * count );

        for ( sizet i = 0; i < count; ++i ) {
            json element = json_array.at( i );
            glTF::AnimationChannel& channel = values[ i ];

            try_load_int( element, "sampler", channel.sampler );
            json target = element.at( "target" );
            try_load_int( target, "node", channel.target_node );

            std::string target_path = target.value( "path", "");
            if ( target_path == "scale" ) {
                channel.target_type = glTF::AnimationChannel::Scale;
            }
            else if ( target_path == "rotation" ) {
                channel.target_type = glTF::AnimationChannel::Rotation;
            }
            else if ( target_path == "translation" ) {
                channel.target_type = glTF::AnimationChannel::Translation;
            }
            else if ( target_path == "weights" ) {
                channel.target_type = glTF::AnimationChannel::Weights;
            }
            else {
                RASSERTM( false, "Error parsing target path %s\n", target_path.c_str() );
                channel.target_type = glTF::AnimationChannel::Count;
            }
        }

        animation.channels = values;
        animation.channels_count = count;
    }
}

static void load_animations( json& json_data, glTF::glTF& gltf_data, Allocator* allocator ) {
    json array = json_data[ "animations" ];

    sizet array_count = array.size();
    gltf_data.animations = ( glTF::Animation* )allocate_and_zero( allocator, sizeof( glTF::Animation ) * array_count );
    gltf_data.animations_count = array_count;

    for ( sizet i = 0; i < array_count; ++i ) {
        load_animation( array[ i ], gltf_data.animations[ i ], allocator );
    }
}

glTF::glTF gltf_load_file( cstring file_path ) {
    glTF::glTF result{ };

    if ( !file_exists( file_path ) ) {
        rprint( "Error: file %s does not exists.\n", file_path );
        return result;
    }

    Allocator* heap_allocator = &MemoryService::instance()->system_allocator;

    FileReadResult read_result = file_read_text( file_path, heap_allocator );

    json gltf_data = json::parse( read_result.data );

    result.allocator.init( rmega(2) );
    Allocator* allocator = &result.allocator;

    for ( auto properties : gltf_data.items() ) {
        if ( properties.key() == "asset" ) {
            load_asset( gltf_data, result.asset, allocator );
        } else if ( properties.key() == "scene" ) {
            try_load_int( gltf_data, "scene", result.scene );
        } else if ( properties.key() == "scenes" ) {
            load_scenes( gltf_data, result, allocator );
        } else if ( properties.key() == "buffers" ) {
            load_buffers( gltf_data, result, allocator );
        } else if ( properties.key() == "bufferViews" ) {
            load_buffer_views( gltf_data, result, allocator );
        } else if ( properties.key() == "nodes" ) {
            load_nodes( gltf_data, result, allocator );
        } else if ( properties.key() == "meshes" ) {
            load_meshes( gltf_data, result, allocator );
        } else if ( properties.key() == "accessors" ) {
            load_accessors( gltf_data, result, allocator );
        } else if ( properties.key() == "materials" ) {
            load_materials( gltf_data, result, allocator );
        } else if ( properties.key() == "textures" ) {
            load_textures( gltf_data, result, allocator );
        } else if ( properties.key() == "images" ) {
            load_images( gltf_data, result, allocator );
        } else if ( properties.key() == "samplers" ) {
            load_samplers( gltf_data, result, allocator );
        } else if ( properties.key() == "skins" ) {
            load_skins( gltf_data, result, allocator );
        } else if ( properties.key() == "animations" ) {
            load_animations( gltf_data, result, allocator );
        }
    }

    heap_allocator->deallocate( read_result.data );

    return result;
}

void gltf_free( glTF::glTF& scene ) {
    scene.allocator.shutdown();
}

i32 gltf_get_attribute_accessor_index( glTF::MeshPrimitive::Attribute* attributes, u32 attribute_count, cstring attribute_name ) {
    for ( u32 index = 0; index < attribute_count; ++index) {
        glTF::MeshPrimitive::Attribute& attribute = attributes[ index ];
        if ( strcmp( attribute.key.data, attribute_name ) == 0 ) {
            return attribute.accessor_index;
        }
    }

    return -1;
}

} // namespace raptor

i32 raptor::glTF::get_data_offset( i32 accessor_offset, i32 buffer_view_offset ) {

    i32 byte_offset = buffer_view_offset == INVALID_INT_VALUE ? 0 : buffer_view_offset;
    byte_offset += accessor_offset == INVALID_INT_VALUE ? 0 : accessor_offset;
    return byte_offset;
}
