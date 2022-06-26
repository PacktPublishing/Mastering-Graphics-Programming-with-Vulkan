#include "graphics/render_resources_loader.hpp"
#include "graphics/frame_graph.hpp"

#include "foundation/file.hpp"

#include "external/json.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

namespace raptor {

// Utility methods ////////////////////////////////////////////////////////
static void             shader_concatenate( cstring filename, raptor::StringBuffer& path_buffer, raptor::StringBuffer& shader_buffer, raptor::Allocator* temp_allocator );
static VkBlendFactor    get_blend_factor( const std::string factor );
static VkBlendOp        get_blend_op( const std::string op );
static void             parse_gpu_pipeline( nlohmann::json& pipeline, raptor::PipelineCreation& pc, raptor::StringBuffer& path_buffer,
                                            raptor::StringBuffer& shader_buffer, raptor::Allocator* temp_allocator, raptor::Renderer* renderer, raptor::FrameGraph* frame_graph );

// RenderResourcesLoader //////////////////////////////////////////////////
void RenderResourcesLoader::init( raptor::Renderer* renderer_, raptor::StackAllocator* temp_allocator_, raptor::FrameGraph* frame_graph_ ) {
    renderer = renderer_;
    temp_allocator = temp_allocator_;
    frame_graph = frame_graph_;
}

void RenderResourcesLoader::shutdown() {
}

void RenderResourcesLoader::load_gpu_technique( cstring json_path ) {

    using namespace raptor;
    sizet allocated_marker = temp_allocator->get_marker();

    FileReadResult read_result = file_read_text( json_path, temp_allocator );

    StringBuffer path_buffer;
    path_buffer.init( 1024, temp_allocator );

    StringBuffer shader_code_buffer;
    shader_code_buffer.init( rkilo( 64 ), temp_allocator );

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

    json pipelines = json_data[ "pipelines" ];
    if ( pipelines.is_array() ) {
        u32 size = u32( pipelines.size() );
        for ( u32 i = 0; i < size; ++i ) {
            json pipeline = pipelines[ i ];
            PipelineCreation pc{};
            pc.shaders.reset();

            json inherit_from = pipeline[ "inherit_from" ];
            if ( inherit_from.is_string() ) {
                std::string inherited_name;
                inherit_from.get_to( inherited_name );

                for ( u32 ii = 0; ii < size; ++ii ) {
                    json pipeline_i = pipelines[ ii ];
                    std::string name;
                    pipeline_i[ "name" ].get_to( name );

                    if ( name == inherited_name ) {
                        parse_gpu_pipeline( pipeline_i, pc, path_buffer, shader_code_buffer, temp_allocator, renderer, frame_graph );
                        break;
                    }
                }
            }

            parse_gpu_pipeline( pipeline, pc, path_buffer, shader_code_buffer, temp_allocator, renderer, frame_graph );

            technique_creation.creations[ technique_creation.num_creations++ ] = pc;
        }
    }

    // Create technique and cache it.
    GpuTechnique* technique = renderer->create_technique( technique_creation );

    temp_allocator->free_marker( allocated_marker );
}

void RenderResourcesLoader::load_texture( cstring path ) {
    int comp, width, height;
    uint8_t* image_data = stbi_load( path, &width, &height, &comp, 4 );
    if ( !image_data ) {
        rprint( "Error loading texture %s", path );
        return ;
    }

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

    sizet allocated_marker = temp_allocator->get_marker();
    StringBuffer path_buffer;
    path_buffer.init( 1024, temp_allocator );
    char* copied_path = path_buffer.append_use_f( "%s", path );
    file_name_from_path( copied_path );

    TextureCreation creation;
    creation.set_data( image_data ).set_format_type( VK_FORMAT_R8G8B8A8_UNORM, TextureType::Texture2D ).set_flags( mip_levels, 0 ).set_size( ( u16 )width, ( u16 )height, 1 ).set_name( copied_path );

    renderer->create_texture( creation );

    // IMPORTANT:
    // Free memory loaded from file, it should not matter!
    free( image_data );

    temp_allocator->free_marker( allocated_marker );
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

void shader_concatenate( cstring filename, raptor::StringBuffer& path_buffer, raptor::StringBuffer& shader_buffer, raptor::Allocator* temp_allocator ) {
    using namespace raptor;

    // Read file and concatenate it
    path_buffer.clear();
    cstring shader_path = path_buffer.append_use_f( "%s%s", RAPTOR_SHADER_FOLDER, filename );
    FileReadResult shader_read_result = file_read_text( shader_path, temp_allocator );
    if ( shader_read_result.data ) {
        // Append without null termination and add termination later.
        shader_buffer.append_m( shader_read_result.data, strlen( shader_read_result.data ) );
    } else {
        rprint( "Cannot read file %s\n", shader_path );
    }
}

void parse_gpu_pipeline( nlohmann::json& pipeline, raptor::PipelineCreation& pc, raptor::StringBuffer& path_buffer,
                                raptor::StringBuffer& shader_buffer, raptor::Allocator* temp_allocator, raptor::Renderer* renderer, raptor::FrameGraph* frame_graph ) {
    using json = nlohmann::json;
    using namespace raptor;

    json shaders = pipeline[ "shaders" ];
    if ( !shaders.is_null() ) {

        for ( sizet s = 0; s < shaders.size(); ++s ) {
            json shader_stage = shaders[ s ];

            std::string name;

            path_buffer.clear();
            // Read file and concatenate it

            cstring code = shader_buffer.current();

            json includes = shader_stage[ "includes" ];
            if ( includes.is_array() ) {

                for ( sizet in = 0; in < includes.size(); ++in ) {
                    includes[ in ].get_to( name );
                    shader_concatenate( name.c_str(), path_buffer, shader_buffer, temp_allocator );
                }
            }

            shader_stage[ "shader" ].get_to( name );
            // Concatenate main shader code
            shader_concatenate( name.c_str(), path_buffer, shader_buffer, temp_allocator );
            // Add terminator for final string.
            shader_buffer.close_current_string();

            shader_stage[ "stage" ].get_to( name );

            // Debug print of final code if needed.
            //rprint( "\n\n%s\n\n\n", code );

            if ( name == "vertex" ) {
                pc.shaders.add_stage( code, strlen( code ), VK_SHADER_STAGE_VERTEX_BIT );
            } else if ( name == "fragment" ) {
                pc.shaders.add_stage( code, strlen( code ), VK_SHADER_STAGE_FRAGMENT_BIT );
            } else if ( name == "compute" ) {
                pc.shaders.add_stage( code, strlen( code ), VK_SHADER_STAGE_COMPUTE_BIT );
            }
        }
    }

    json vertex_inputs = pipeline[ "vertex_input" ];
    if ( vertex_inputs.is_array() ) {

        pc.vertex_input.num_vertex_attributes = 0;
        pc.vertex_input.num_vertex_streams = 0;

        for ( sizet v = 0; v < vertex_inputs.size(); ++v ) {
            VertexAttribute vertex_attribute{};

            json vertex_input = vertex_inputs[ v ];

            vertex_attribute.location = ( u16 )vertex_input.value( "attribute_location", 0u );
            vertex_attribute.binding = ( u16 )vertex_input.value( "attribute_binding", 0u );
            vertex_attribute.offset = vertex_input.value( "attribute_offset", 0u );

            json attribute_format = vertex_input[ "attribute_format" ];
            if ( attribute_format.is_string() ) {
                std::string name;
                attribute_format.get_to( name );

                for ( u32 e = 0; e < VertexComponentFormat::Count; ++e ) {
                    VertexComponentFormat::Enum enum_value = ( VertexComponentFormat::Enum )e;
                    if ( name == VertexComponentFormat::ToString( enum_value ) ) {
                        vertex_attribute.format = enum_value;
                        break;
                    }
                }
            }
            pc.vertex_input.add_vertex_attribute( vertex_attribute );

            VertexStream vertex_stream{};

            vertex_stream.binding = ( u16 )vertex_input.value( "stream_binding", 0u );
            vertex_stream.stride = ( u16 )vertex_input.value( "stream_stride", 0u );

            json stream_rate = vertex_input[ "stream_rate" ];
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

            pc.vertex_input.add_vertex_stream( vertex_stream );
        }
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
            } else {
                const RenderPass* render_pass = renderer->gpu->access_render_pass( node->render_pass );

                pc.render_pass = render_pass->output;
            }
        } else {
            rprint( "Cannot find render pass %s. Defaulting to swapchain\n", name.c_str() );
            pc.render_pass = renderer->gpu->get_swapchain_output();
        }
    }
}


} // namespace raptor