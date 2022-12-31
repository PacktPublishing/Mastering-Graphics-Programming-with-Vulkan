#include "graphics/render_resources_loader.hpp"
#include "graphics/frame_graph.hpp"

#include "foundation/file.hpp"
#include "foundation/time.hpp"

#include "external/json.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

namespace raptor {

// Utility methods ////////////////////////////////////////////////////////
static u64              shader_concatenate( cstring filename, raptor::StringBuffer& path_buffer, raptor::StringBuffer& shader_buffer, raptor::Allocator* temp_allocator );
static VkBlendFactor    get_blend_factor( const std::string factor );
static VkBlendOp        get_blend_op( const std::string op );
static bool             parse_gpu_pipeline( nlohmann::json& pipeline, raptor::PipelineCreation& pc, raptor::StringBuffer& path_buffer,
                                            raptor::StringBuffer& shader_buffer, raptor::Allocator* temp_allocator, raptor::Renderer* renderer,
                                            raptor::FrameGraph* frame_graph, raptor::StringBuffer& pass_name_buffer,
                                            const Array<VertexInputCreation>& vertex_input_creations, FlatHashMap<u64, u16>& name_to_vertex_inputs,
                                            cstring technique_name, bool use_cache, bool parent_technique );

// RenderResourcesLoader //////////////////////////////////////////////////
void RenderResourcesLoader::init( raptor::Renderer* renderer_, raptor::StackAllocator* temp_allocator_, raptor::FrameGraph* frame_graph_ ) {
    renderer = renderer_;
    temp_allocator = temp_allocator_;
    frame_graph = frame_graph_;
}

void RenderResourcesLoader::shutdown() {
}

GpuTechnique* RenderResourcesLoader::load_gpu_technique( cstring json_path, bool use_shader_cache ) {

    using namespace raptor;
    i64 begin_time = time_now();
    sizet allocated_marker = temp_allocator->get_marker();

    FileReadResult read_result = file_read_text( json_path, temp_allocator );

    StringBuffer path_buffer;
    path_buffer.init( 1024, temp_allocator );

    StringBuffer shader_code_buffer;
    shader_code_buffer.init( rmega( 2 ), temp_allocator );

    StringBuffer pass_name_buffer;
    pass_name_buffer.init( rkilo( 2 ), temp_allocator );

    using json = nlohmann::json;

    json json_data = json::parse( read_result.data );

    // parse 1 pipeline
    json name = json_data[ "name" ];
    std::string name_string;
    if ( name.is_string() ) {
        name.get_to( name_string );
        rprint( "Parsing GPU Technique %s\n", name_string.c_str() );
    }

    GpuTechniqueCreation technique_creation;
    technique_creation.name = name_string.c_str();

    Array<VertexInputCreation> vertex_input_creations;

    FlatHashMap<u64, u16> name_to_vertex_inputs;
    name_to_vertex_inputs.init( temp_allocator, 8 );

    // Parse vertex inputs
    json vertex_inputs = json_data[ "vertex_inputs" ];
    if ( vertex_inputs.is_array() ) {

        u32 size = u32( vertex_inputs.size() );

        vertex_input_creations.init( temp_allocator, size, size );

        for ( u32 i = 0; i < size; ++i ) {
            json vertex_input = vertex_inputs[ i ];

            std::string name;
            vertex_input[ "name" ].get_to( name );

            name_to_vertex_inputs.insert( hash_calculate( name.c_str() ), ( u16 )i );

            VertexInputCreation& vertex_input_creation = vertex_input_creations[ i ];
            vertex_input_creation.reset();

            json vertex_attributes = vertex_input[ "vertex_attributes" ];
            if ( vertex_attributes.is_array() ) {

                for ( sizet v = 0; v < vertex_attributes.size(); ++v ) {
                    VertexAttribute vertex_attribute{};

                    json json_vertex_attribute = vertex_attributes[ v ];

                    vertex_attribute.location = ( u16 )json_vertex_attribute.value( "attribute_location", 0u );
                    vertex_attribute.binding = ( u16 )json_vertex_attribute.value( "attribute_binding", 0u );
                    vertex_attribute.offset = json_vertex_attribute.value( "attribute_offset", 0u );

                    json attribute_format = json_vertex_attribute[ "attribute_format" ];
                    if ( attribute_format.is_string() ) {
                        std::string name;
                        attribute_format.get_to( name );

                        vertex_attribute.format = VertexComponentFormat::Count;

                        for ( u32 e = 0; e < VertexComponentFormat::Count; ++e ) {
                            VertexComponentFormat::Enum enum_value = ( VertexComponentFormat::Enum )e;
                            if ( name == VertexComponentFormat::ToString( enum_value ) ) {
                                vertex_attribute.format = enum_value;
                                break;
                            }
                        }

                        RASSERT( vertex_attribute.format != VertexComponentFormat::Count );
                    }

                    vertex_input_creation.add_vertex_attribute( vertex_attribute );
                }
            }

            json vertex_streams = vertex_input[ "vertex_streams" ];
            if ( vertex_streams.is_array() ) {

                for ( sizet v = 0; v < vertex_streams.size(); ++v ) {
                    VertexStream vertex_stream{};

                    json json_vertex_stream = vertex_streams[ v ];

                    vertex_stream.binding = ( u16 )json_vertex_stream.value( "stream_binding", 0u );
                    vertex_stream.stride = ( u16 )json_vertex_stream.value( "stream_stride", 0u );

                    json stream_rate = json_vertex_stream[ "stream_rate" ];
                    if ( stream_rate.is_string() ) {
                        std::string name;
                        stream_rate.get_to( name );

                        if ( name == "Vertex" ) {
                            vertex_stream.input_rate = VertexInputRate::PerVertex;
                        } else if ( name == "Instance" ) {
                            vertex_stream.input_rate = VertexInputRate::PerInstance;
                        } else {
                            RASSERT( false );
                        }
                    }

                    vertex_input_creation.add_vertex_stream( vertex_stream );
                }
            }
        }
    }

    // Parse pipelines
    json pipelines = json_data[ "pipelines" ];
    if ( pipelines.is_array() ) {
        u32 size = u32( pipelines.size() );
        for ( u32 i = 0; i < size; ++i ) {
            json pipeline = pipelines[ i ];

            PipelineCreation pc{};
            pc.shaders.reset();

            bool add_pass = true;

            json inherit_from = pipeline[ "inherit_from" ];
            if ( inherit_from.is_string() ) {
                std::string inherited_name;
                inherit_from.get_to( inherited_name );

                for ( u32 ii = 0; ii < size; ++ii ) {
                    json pipeline_i = pipelines[ ii ];
                    std::string name;
                    pipeline_i[ "name" ].get_to( name );

                    if ( name == inherited_name ) {
                        add_pass = parse_gpu_pipeline( pipeline_i, pc, path_buffer, shader_code_buffer, temp_allocator, renderer, frame_graph, pass_name_buffer, vertex_input_creations, name_to_vertex_inputs, technique_creation.name, false, true );
                        break;
                    }
                }
            }

            add_pass = add_pass && parse_gpu_pipeline( pipeline, pc, path_buffer, shader_code_buffer, temp_allocator, renderer, frame_graph, pass_name_buffer, vertex_input_creations, name_to_vertex_inputs, technique_creation.name, use_shader_cache, false );

            if ( add_pass ) {
                technique_creation.creations[ technique_creation.num_creations++ ] = pc;
            }
        }
    }

    // Create technique and cache it.
    GpuTechnique* technique = renderer->create_technique( technique_creation );

    temp_allocator->free_marker( allocated_marker );

    rprint( "Created technique %s in %f seconds\n", technique_creation.name, time_from_seconds( begin_time ) );

    return technique;
}

TextureResource* RenderResourcesLoader::load_texture( cstring path, bool generate_mipmaps ) {
    int comp, width, height;
    uint8_t* image_data = stbi_load( path, &width, &height, &comp, 4 );
    if ( !image_data ) {
        rprint( "Error loading texture %s", path );
        return nullptr;
    }

    u32 mip_levels = 1;
    if ( generate_mipmaps ) {
        u32 w = width;
        u32 h = height;

        while ( w > 1 && h > 1 ) {
            w /= 2;
            h /= 2;

            ++mip_levels;
        }
    }

    sizet allocated_marker = temp_allocator->get_marker();
    StringBuffer path_buffer;
    path_buffer.init( 1024, temp_allocator );
    char* copied_path = path_buffer.append_use_f( "%s", path );
    file_name_from_path( copied_path );

    TextureCreation creation;
    creation.set_data( image_data ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D ).set_mips( mip_levels ).set_size( ( u16 )width, ( u16 )height, 1 ).set_name( copied_path );

    TextureResource* texture = renderer->create_texture( creation );

    // IMPORTANT:
    // Free memory loaded from file, it should not matter!
    free( image_data );

    temp_allocator->free_marker( allocated_marker );

    return texture;
}


VkBlendFactor get_blend_factor( const std::string factor ) {
    if ( factor == "ZERO" ) {
        return VK_BLEND_FACTOR_ZERO;
    }
    if ( factor == "ONE" ) {
        return VK_BLEND_FACTOR_ONE;
    }
    if ( factor == "SRC_COLOR" ) {
        return VK_BLEND_FACTOR_SRC_COLOR;
    }
    if ( factor == "ONE_MINUS_SRC_COLOR" ) {
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    }
    if ( factor == "DST_COLOR" ) {
        return VK_BLEND_FACTOR_DST_COLOR;
    }
    if ( factor == "ONE_MINUS_DST_COLOR" ) {
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    }
    if ( factor == "SRC_ALPHA" ) {
        return VK_BLEND_FACTOR_SRC_ALPHA;
    }
    if ( factor == "ONE_MINUS_SRC_ALPHA" ) {
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    if ( factor == "DST_ALPHA" ) {
        return VK_BLEND_FACTOR_DST_ALPHA;
    }
    if ( factor == "ONE_MINUS_DST_ALPHA" ) {
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    if ( factor == "CONSTANT_COLOR" ) {
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    }
    if ( factor == "ONE_MINUS_CONSTANT_COLOR" ) {
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    }
    if ( factor == "CONSTANT_ALPHA" ) {
        return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    }
    if ( factor == "ONE_MINUS_CONSTANT_ALPHA" ) {
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    }
    if ( factor == "SRC_ALPHA_SATURATE" ) {
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    }
    if ( factor == "SRC1_COLOR" ) {
        return VK_BLEND_FACTOR_SRC1_COLOR;
    }
    if ( factor == "ONE_MINUS_SRC1_COLOR" ) {
        return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    }
    if ( factor == "SRC1_ALPHA" ) {
        return VK_BLEND_FACTOR_SRC1_ALPHA;
    }
    if ( factor == "ONE_MINUS_SRC1_ALPHA" ) {
        return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    }

    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp get_blend_op( const std::string op ) {
    if ( op == "ADD" ) {
        VK_BLEND_OP_ADD;
    }
    if ( op == "SUBTRACT" ) {
        VK_BLEND_OP_SUBTRACT;
    }
    if ( op == "REVERSE_SUBTRACT" ) {
        VK_BLEND_OP_REVERSE_SUBTRACT;
    }
    if ( op == "MIN" ) {
        VK_BLEND_OP_MIN;
    }
    if ( op == "MAX" ) {
        VK_BLEND_OP_MAX;
    }

    return VK_BLEND_OP_ADD;
}

u64 shader_concatenate( cstring filename, raptor::StringBuffer& path_buffer, raptor::StringBuffer& shader_buffer, raptor::Allocator* temp_allocator ) {
    using namespace raptor;

    u64 hashed_memory = 0;
    // Read file and concatenate it
    path_buffer.clear();
    cstring shader_path = path_buffer.append_use_f( "%s%s", RAPTOR_SHADER_FOLDER, filename );
    FileReadResult shader_read_result = file_read_text( shader_path, temp_allocator );
    if ( shader_read_result.data ) {
        // Append without null termination and add termination later.
        shader_buffer.append_m( shader_read_result.data, strlen( shader_read_result.data ) );
        // Using strlne because file can contain impurities after the end, causing different hashes when using shader_read_result.size.
        hashed_memory = raptor::hash_bytes( shader_read_result.data, strlen( shader_read_result.data ) );
    } else {
        rprint( "Cannot read file %s\n", shader_path );
    }

    return hashed_memory;
}

bool parse_gpu_pipeline( nlohmann::json& pipeline, raptor::PipelineCreation& pc, raptor::StringBuffer& path_buffer,
                         raptor::StringBuffer& shader_buffer, raptor::Allocator* temp_allocator, raptor::Renderer* renderer,
                         raptor::FrameGraph* frame_graph, raptor::StringBuffer& pass_name_buffer,
                         const Array<VertexInputCreation>& vertex_input_creations, FlatHashMap<u64, u16>& name_to_vertex_inputs,
                         cstring technique_name, bool use_cache, bool parent_technique ) {
    using json = nlohmann::json;
    using namespace raptor;

    json json_name = pipeline[ "name" ];
    if ( json_name.is_string() ) {
        std::string name;
        json_name.get_to( name );

        pc.name = pass_name_buffer.append_use_f( "%s", name.c_str() );
    }

    pc.shaders.set_name( pc.name );

    bool compute_shader_pass = false;

    json shaders = pipeline[ "shaders" ];
    if ( !shaders.is_null() ) {

        for ( sizet s = 0; s < shaders.size(); ++s ) {
            json parsed_shader_stage = shaders[ s ];

            std::string name;

            path_buffer.clear();

            // Cache file hashes
            u64 shader_file_hashes[ 16 ];
            u32 shader_file_hashes_count = 0;
            // Read file and concatenate it
            // Cache current shader code beginning
            cstring code = shader_buffer.current();

            json includes = parsed_shader_stage[ "includes" ];
            if ( includes.is_array() ) {

                //rprint( "Shader hash " );
                for ( sizet in = 0; in < includes.size(); ++in ) {
                    includes[ in ].get_to( name );
                    u64 shader_file_hash = shader_concatenate( name.c_str(), path_buffer, shader_buffer, temp_allocator );
                    shader_file_hashes[ shader_file_hashes_count++ ] = shader_file_hash;
                    //rprint( " %016llx", shader_file_hash );
                }
            }

            parsed_shader_stage[ "shader" ].get_to( name );
            // Concatenate main shader code
            u64 shader_file_hash = shader_concatenate( name.c_str(), path_buffer, shader_buffer, temp_allocator );
            // Cache main shader code hash
            shader_file_hashes[ shader_file_hashes_count++ ] = shader_file_hash;
            //rprint( " %016llx", shader_file_hash );
            // Add terminator for final string.
            shader_buffer.close_current_string();

            parsed_shader_stage[ "stage" ].get_to( name );

            // Debug print of final code if needed.
            //rprint( "\n\n%s\n\n\n", code );
            u32 code_size = u32( strlen( code ) );

            ShaderStage shader_stage;
            shader_stage.code = code;
            shader_stage.code_size = code_size;

            if ( name == "vertex" ) {
                shader_stage.type = VK_SHADER_STAGE_VERTEX_BIT;
            } else if ( name == "fragment" ) {
                shader_stage.type = VK_SHADER_STAGE_FRAGMENT_BIT;
            } else if ( name == "compute" ) {
                shader_stage.type = VK_SHADER_STAGE_COMPUTE_BIT;

                compute_shader_pass = true;
            } else if ( name == "mesh" ) {
                if ( !renderer->gpu->mesh_shaders_extension_present ) {
                    return false;
                }
                shader_stage.type = VK_SHADER_STAGE_MESH_BIT_NV;
            } else if ( name == "task" ) {
                if ( !renderer->gpu->mesh_shaders_extension_present ) {
                    return false;
                }
                shader_stage.type = VK_SHADER_STAGE_TASK_BIT_NV;
            }

            // Do not compile shaders when parsing the parent technique
            bool compile_shader = true;
            cstring shader_spirv_path = nullptr;
            cstring shader_hash_path = nullptr;

            if ( use_cache ) {
                // Check shader cache and eventually compile the code.
                path_buffer.clear();
                shader_spirv_path = path_buffer.append_use_f( "%s/%s_%s_%s.spv",
                                                              renderer->resource_cache.binary_data_folder, technique_name, pc.shaders.name,
                                                              to_compiler_extension( shader_stage.type ) );
                shader_hash_path = path_buffer.append_use_f( "%s/%s_%s_%s.hash.cache",
                                                             renderer->resource_cache.binary_data_folder, technique_name, pc.shaders.name,
                                                             to_compiler_extension( shader_stage.type ) );
                //FileReadResult shader_read_result = file_read_text( shader_path, temp_allocator );
                bool cache_exists = file_exists( shader_hash_path );

                //rprint( "\nfile %s\n", shader_hash_path );
                // TODO: still not working
                if ( cache_exists ) {

                    FileReadResult frr = file_read_binary( shader_hash_path, temp_allocator );
                    if ( frr.data ) {

                        u32 file_entries = frr.size / sizeof( u64 );
                        if ( file_entries == shader_file_hashes_count ) {
                            u64* cached_file_hashes = ( u64* )frr.data;
                            u32 i = 0;
                            for ( ; i < shader_file_hashes_count; ++i ) {
                                const u64 a = shader_file_hashes[ i ];
                                const u64 b = cached_file_hashes[ i ];
                                if ( a != b ) {
                                    break;
                                }
                            }

                            compile_shader = i != shader_file_hashes_count;
                        }
                    }
                }
            }

            // Cache is not present or shader has changed, compile shaders.
            if ( compile_shader ) {
                VkShaderModuleCreateInfo shader_create_info = renderer->gpu->compile_shader( code, code_size, shader_stage.type, pc.shaders.name );
                if ( shader_create_info.pCode ) {
                    shader_stage.code = reinterpret_cast< cstring >( shader_create_info.pCode );
                    shader_stage.code_size = ( u32 )shader_create_info.codeSize;

                    if ( use_cache ) {
                        // Write hashes
                        file_write_binary( shader_hash_path, &shader_file_hashes[ 0 ], sizeof( u64 ) * shader_file_hashes_count );
                        /*rprint( "Hashes\n%016llx ", shader_file_hashes[0] );
                        for (u32 i = 1; i < shader_file_hashes_count; ++i ) {
                            rprint( "%016llx ", shader_file_hashes[ i ] );
                        }
                        rprint( "\n\n" );*/
                        // Write spirv
                        file_write_binary( shader_spirv_path, ( void* )shader_create_info.pCode, sizeof( u32 ) * shader_create_info.codeSize );
                    }
                } else {
                    rprint( "Error compiling shader %s stage %s", pc.shaders.name, to_compiler_extension( shader_stage.type ) );
                    return false;
                }
            } else {
                // Shader is the same, read cached SpirV
                FileReadResult frr = file_read_binary( shader_spirv_path, temp_allocator );

                shader_stage.code = reinterpret_cast< cstring >( frr.data );
                shader_stage.code_size = ( u32 )frr.size / sizeof( u32 );
            }

            // Finally add the stage
            pc.shaders.add_stage( shader_stage.code, shader_stage.code_size, shader_stage.type );
            // Output always spv compiled shaders
            pc.shaders.set_spv_input( true );
        }
    }

    json vertex_input = pipeline[ "vertex_input" ];
    if ( vertex_input.is_string() ) {
        std::string name;
        vertex_input.get_to( name );

        u16 index = name_to_vertex_inputs.get( hash_calculate( name.c_str() ) );

        pc.vertex_input = vertex_input_creations[ index ];
    }

    json depth = pipeline[ "depth" ];
    if ( !depth.is_null() ) {
        pc.depth_stencil.depth_enable = 1;
        pc.depth_stencil.depth_write_enable = depth.value( "write", false );

        // TODO:
        json comparison = depth[ "test" ];
        if ( comparison.is_string() ) {
            std::string name;
            comparison.get_to( name );

            if ( name == "less_or_equal" ) {
                pc.depth_stencil.depth_comparison = VK_COMPARE_OP_LESS_OR_EQUAL;
            } else if ( name == "equal" ) {
                pc.depth_stencil.depth_comparison = VK_COMPARE_OP_EQUAL;
            } else if ( name == "never" ) {
                pc.depth_stencil.depth_comparison = VK_COMPARE_OP_NEVER;
            } else if ( name == "always" ) {
                pc.depth_stencil.depth_comparison = VK_COMPARE_OP_ALWAYS;
            } else {
                RASSERT( false );
            }
        }
    }

    json blend_states = pipeline[ "blend" ];
    if ( !blend_states.is_null() ) {

        for ( sizet b = 0; b < blend_states.size(); ++b ) {
            json blend = blend_states[ b ];

            std::string enabled = blend.value( "enable", "" );
            std::string src_colour = blend.value( "src_colour", "" );
            std::string dst_colour = blend.value( "dst_colour", "" );
            std::string blend_op = blend.value( "op", "" );

            BlendState& blend_state = pc.blend_state.add_blend_state();
            blend_state.blend_enabled = ( enabled == "true" );
            blend_state.set_color( get_blend_factor( src_colour ), get_blend_factor( dst_colour ), get_blend_op( blend_op ) );
        }
    }

    json cull = pipeline[ "cull" ];
    if ( cull.is_string() ) {
        std::string name;
        cull.get_to( name );

        if ( name == "back" ) {
            pc.rasterization.cull_mode = VK_CULL_MODE_BACK_BIT;
        } else if ( name == "front" ) {
            pc.rasterization.cull_mode = VK_CULL_MODE_FRONT_BIT;
        } else {
            RASSERT( false );
        }
    }
    //pc.rasterization.front = VK_FRONT_FACE_CLOCKWISE;

    pc.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    json topology = pipeline[ "topology" ];
    if ( topology.is_string() ) {
        std::string name;
        topology.get_to( name );

        if ( name == "triangle_list" ) {
            pc.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        } else if ( name == "line_list" ) {
            pc.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        } else {
            RASSERT( false );
        }
    }

    json render_pass = pipeline[ "render_pass" ];
    if ( render_pass.is_string() ) {
        std::string name;
        render_pass.get_to( name );

        FrameGraphNode* node = frame_graph->get_node( name.c_str() );
        if ( node ) {

            // TODO: handle better
            if ( name == "swapchain" ) {
                pc.render_pass = renderer->gpu->get_swapchain_output();
            } else if ( compute_shader_pass ) {
                pc.render_pass = renderer->gpu->get_swapchain_output();
            } else {
                const RenderPass* render_pass = renderer->gpu->access_render_pass( node->render_pass );
                if ( render_pass )
                    pc.render_pass = render_pass->output;
            }
        } else {
            rprint( "Cannot find render pass %s. Defaulting to swapchain\n", name.c_str() );
            pc.render_pass = renderer->gpu->get_swapchain_output();
        }
    }

    return true;
}


} // namespace raptor
