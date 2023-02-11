
#include "application/window.hpp"
#include "application/input.hpp"
#include "application/keys.hpp"

#include "graphics/gpu_device.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/renderer.hpp"
#include "graphics/raptor_imgui.hpp"
#include "graphics/gpu_profiler.hpp"

#include "external/cglm/struct/mat3.h"
#include "external/cglm/struct/mat4.h"
#include "external/cglm/struct/cam.h"
#include "external/cglm/struct/affine.h"

#include "external/imgui/imgui.h"

#include "external/tracy/tracy/Tracy.hpp"

#include "foundation/file.hpp"
#include "foundation/gltf.hpp"
#include "foundation/numerics.hpp"
#include "foundation/resource_manager.hpp"
#include "foundation/time.hpp"

#include <stdlib.h> // for exit()

///////////////////////////////////////

// Rotating cube test
raptor::BufferHandle                    cube_vb;
raptor::BufferHandle                    cube_ib;
raptor::PipelineHandle                  cube_pipeline;
raptor::BufferHandle                    cube_cb;
raptor::DescriptorSetHandle             cube_rl;
raptor::DescriptorSetLayoutHandle       cube_dsl;

f32 rx, ry;

struct MaterialData {
    vec4s base_color_factor;
};

struct MeshDraw {
    raptor::BufferHandle index_buffer;
    raptor::BufferHandle position_buffer;
    raptor::BufferHandle tangent_buffer;
    raptor::BufferHandle normal_buffer;
    raptor::BufferHandle texcoord_buffer;

    raptor::BufferHandle material_buffer;
    MaterialData         material_data;

    u32 index_offset;
    u32 position_offset;
    u32 tangent_offset;
    u32 normal_offset;
    u32 texcoord_offset;

    u32 count;

    raptor::DescriptorSetHandle descriptor_set;
};

struct UniformData {
    mat4s m;
    mat4s vp;
    mat4s inverseM;
    vec4s eye;
    vec4s light;
};

static void input_os_messages_callback( void* os_event, void* user_data ) {
    raptor::InputService* input = ( raptor::InputService* )user_data;
    input->on_event( os_event );
}

int main( int argc, char** argv ) {

    if ( argc < 2 ) {
        printf( "Usage: chapter1 [path to glTF model]\n");
        InjectDefault3DModel();
    }

    using namespace raptor;
    // Init services
    MemoryService::instance()->init( nullptr );
    time_service_init();

    Allocator* allocator = &MemoryService::instance()->system_allocator;

    StackAllocator scratch_allocator;
    scratch_allocator.init( rmega( 8 ) );

    // window
    WindowConfiguration wconf{ 1280, 800, "Raptor Test", allocator };
    raptor::Window window;
    window.init( &wconf );

    InputService input_handler;
    input_handler.init( allocator );

    // Callback register
    window.register_os_messages_callback( input_os_messages_callback, &input_handler );

    // graphics
    DeviceCreation dc;
    dc.set_window( window.width, window.height, window.platform_handle ).set_allocator( allocator ).set_linear_allocator( &scratch_allocator );
    GpuDevice gpu;
    gpu.init( dc );

    ResourceManager rm;
    rm.init( allocator, nullptr );

    GPUProfiler gpu_profiler;
    gpu_profiler.init( allocator, 100 );

    Renderer renderer;
    renderer.init( { &gpu, allocator } );
    renderer.set_loaders( &rm );

    ImGuiService* imgui = ImGuiService::instance();
    ImGuiServiceConfiguration imgui_config{ &gpu, window.platform_handle };
    imgui->init( &imgui_config );

    Directory cwd{ };
    directory_current(&cwd);

    char gltf_base_path[512]{ };
    memcpy( gltf_base_path, argv[ 1 ], strlen( argv[ 1] ) );
    file_directory_from_path( gltf_base_path );

    directory_change( gltf_base_path );

    char gltf_file[512]{ };
    memcpy( gltf_file, argv[ 1 ], strlen( argv[ 1] ) );
    file_name_from_path( gltf_file );

    glTF::glTF scene = gltf_load_file( gltf_file );

    Array<TextureResource> images;
    images.init( allocator, scene.images_count );

    for ( u32 image_index = 0; image_index < scene.images_count; ++image_index ) {
        glTF::Image& image = scene.images[ image_index ];
        TextureResource* tr = renderer.create_texture( image.uri.data, image.uri.data );
        RASSERT( tr != nullptr );

        images.push( *tr );
    }

    StringBuffer resource_name_buffer;
    resource_name_buffer.init( 4096, allocator );

    Array<SamplerResource> samplers;
    samplers.init( allocator, scene.samplers_count );

    for ( u32 sampler_index = 0; sampler_index < scene.samplers_count; ++sampler_index ) {
        glTF::Sampler& sampler = scene.samplers[ sampler_index ];

        char* sampler_name = resource_name_buffer.append_use_f( "sampler_%u", sampler_index );

        SamplerCreation creation;
        creation.min_filter = sampler.min_filter == glTF::Sampler::Filter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        creation.mag_filter = sampler.mag_filter == glTF::Sampler::Filter::LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        creation.name = sampler_name;

        SamplerResource* sr = renderer.create_sampler( creation );
        RASSERT( sr != nullptr );

        samplers.push( *sr );
    }

    Array<void*> buffers_data;
    buffers_data.init( allocator, scene.buffers_count );

    for ( u32 buffer_index = 0; buffer_index < scene.buffers_count; ++buffer_index ) {
        glTF::Buffer& buffer = scene.buffers[ buffer_index ];

        FileReadResult buffer_data = file_read_binary( buffer.uri.data, allocator );
        buffers_data.push( buffer_data.data );
    }

    Array<BufferResource> buffers;
    buffers.init( allocator, scene.buffer_views_count );

    for ( u32 buffer_index = 0; buffer_index < scene.buffer_views_count; ++buffer_index ) {
        glTF::BufferView& buffer = scene.buffer_views[ buffer_index ];

        i32 offset = buffer.byte_offset;
        if ( offset == glTF::INVALID_INT_VALUE ) {
            offset = 0;
        }

        u8* data = ( u8* )buffers_data[ buffer.buffer ] + offset;

        // NOTE(marco): the target attribute of a BufferView is not mandatory, so we prepare for both uses
        VkBufferUsageFlags flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        char* buffer_name = buffer.name.data;
        if ( buffer_name == nullptr ) {
            buffer_name = resource_name_buffer.append_use_f( "buffer_%u", buffer_index );
        }

        BufferResource* br = renderer.create_buffer( flags, ResourceUsageType::Immutable, buffer.byte_length, data, buffer_name );
        RASSERT( br != nullptr);

        buffers.push( *br );
    }

    for ( u32 buffer_index = 0; buffer_index < scene.buffers_count; ++buffer_index ) {
        void* buffer = buffers_data[ buffer_index ];
        allocator->deallocate( buffer );
    }
    buffers_data.shutdown();

    // NOTE(marco): restore working directory
    directory_change( cwd.path );

    Array<MeshDraw> mesh_draws;
    mesh_draws.init( allocator, scene.meshes_count );

    {
        // Create pipeline state
        PipelineCreation pipeline_creation;

        // Vertex input
        // TODO(marco): component format should be based on buffer view type
        pipeline_creation.vertex_input.add_vertex_attribute( { 0, 0, 0, VertexComponentFormat::Float3 } ); // position
        pipeline_creation.vertex_input.add_vertex_stream( { 0, 12, VertexInputRate::PerVertex } );

        pipeline_creation.vertex_input.add_vertex_attribute( { 1, 1, 0, VertexComponentFormat::Float4 } ); // tangent
        pipeline_creation.vertex_input.add_vertex_stream( { 1, 16, VertexInputRate::PerVertex} );

        pipeline_creation.vertex_input.add_vertex_attribute( { 2, 2, 0, VertexComponentFormat::Float3 } ); // normal
        pipeline_creation.vertex_input.add_vertex_stream( { 2, 12, VertexInputRate::PerVertex } );

        pipeline_creation.vertex_input.add_vertex_attribute( { 3, 3, 0, VertexComponentFormat::Float2 } ); // texcoord
        pipeline_creation.vertex_input.add_vertex_stream( { 3, 8, VertexInputRate::PerVertex} );

        // Render pass
        pipeline_creation.render_pass = gpu.get_swapchain_output();
        // Depth
        pipeline_creation.depth_stencil.set_depth( true, VK_COMPARE_OP_LESS_OR_EQUAL );

        // Shader state
        const char* vs_code = R"FOO(#version 450
layout(std140, binding = 0) uniform LocalConstants {
    mat4 m;
    mat4 vp;
    mat4 mInverse;
    vec4 eye;
    vec4 light;
};

layout(location=0) in vec3 position;
layout(location=1) in vec4 tangent;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 texCoord0;

layout (location = 0) out vec2 vTexcoord0;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec4 vTangent;
layout (location = 3) out vec4 vPosition;

void main() {
    gl_Position = vp * m * vec4(position, 1);
    vPosition = m * vec4(position, 1.0);
    vTexcoord0 = texCoord0;
    vNormal = mat3(mInverse) * normal;
    vTangent = tangent;
}
)FOO";

        const char* fs_code = R"FOO(#version 450
layout(std140, binding = 0) uniform LocalConstants {
    mat4 m;
    mat4 vp;
    mat4 mInverse;
    vec4 eye;
    vec4 light;
};

layout(std140, binding = 4) uniform MaterialConstant {
    vec4 base_color_factor;
};

layout (binding = 1) uniform sampler2D diffuseTexture;
layout (binding = 2) uniform sampler2D occlusionRoughnessMetalnessTexture;
layout (binding = 3) uniform sampler2D normalTexture;

layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec4 vTangent;
layout (location = 3) in vec4 vPosition;

layout (location = 0) out vec4 frag_color;

#define PI 3.1415926538

vec3 decode_srgb( vec3 c ) {
    vec3 result;
    if ( c.r <= 0.04045) {
        result.r = c.r / 12.92;
    } else {
        result.r = pow( ( c.r + 0.055 ) / 1.055, 2.4 );
    }

    if ( c.g <= 0.04045) {
        result.g = c.g / 12.92;
    } else {
        result.g = pow( ( c.g + 0.055 ) / 1.055, 2.4 );
    }

    if ( c.b <= 0.04045) {
        result.b = c.b / 12.92;
    } else {
        result.b = pow( ( c.b + 0.055 ) / 1.055, 2.4 );
    }

    return clamp( result, 0.0, 1.0 );
}

vec3 encode_srgb( vec3 c ) {
    vec3 result;
    if ( c.r <= 0.0031308) {
        result.r = c.r * 12.92;
    } else {
        result.r = 1.055 * pow( c.r, 1.0 / 2.4 ) - 0.055;
    }

    if ( c.g <= 0.0031308) {
        result.g = c.g * 12.92;
    } else {
        result.g = 1.055 * pow( c.g, 1.0 / 2.4 ) - 0.055;
    }

    if ( c.b <= 0.0031308) {
        result.b = c.b * 12.92;
    } else {
        result.b = 1.055 * pow( c.b, 1.0 / 2.4 ) - 0.055;
    }

    return clamp( result, 0.0, 1.0 );
}

float heaviside( float v ) {
    if ( v > 0.0 ) return 1.0;
    else return 0.0;
}

void main() {
    // NOTE(marco): normal textures are encoded to [0, 1] but need to be mapped to [-1, 1] value
    vec3 bump_normal = normalize( texture(normalTexture, vTexcoord0).rgb * 2.0 - 1.0 );
    vec3 tangent = normalize( vTangent.xyz );
    vec3 bitangent = cross( normalize( vNormal ), tangent ) * vTangent.w;

    mat3 TBN = transpose(mat3(
        tangent,
        bitangent,
        normalize( vNormal )
    ));

    // vec3 V = normalize(eye.xyz - vPosition.xyz);
    // vec3 L = normalize(light.xyz - vPosition.xyz);
    // vec3 N = normalize(vNormal);
    // vec3 H = normalize(L + V);

    vec3 V = normalize( TBN * ( eye.xyz - vPosition.xyz ) );
    vec3 L = normalize( TBN * ( light.xyz - vPosition.xyz ) );
    vec3 N = bump_normal;
    vec3 H = normalize( L + V );

    vec4 rmo = texture(occlusionRoughnessMetalnessTexture, vTexcoord0);

    // Green channel contains roughness values
    float roughness = rmo.g;
    float alpha = pow(roughness, 2.0);

    // Blue channel contains metalness
    float metalness = rmo.b;

    // Red channel for occlusion value

    vec4 base_colour = texture(diffuseTexture, vTexcoord0) * base_color_factor;
    base_colour.rgb = decode_srgb( base_colour.rgb );

    // https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#specular-brdf
    float NdotH = dot(N, H);
    float alpha_squared = alpha * alpha;
    float d_denom = ( NdotH * NdotH ) * ( alpha_squared - 1.0 ) + 1.0;
    float distribution = ( alpha_squared * heaviside( NdotH ) ) / ( PI * d_denom * d_denom );

    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    float HdotL = dot(H, L);
    float HdotV = dot(H, V);

    float visibility = ( heaviside( HdotL ) / ( abs( NdotL ) + sqrt( alpha_squared + ( 1.0 - alpha_squared ) * ( NdotL * NdotL ) ) ) ) * ( heaviside( HdotV ) / ( abs( NdotV ) + sqrt( alpha_squared + ( 1.0 - alpha_squared ) * ( NdotV * NdotV ) ) ) );

    float specular_brdf = visibility * distribution;

    vec3 diffuse_brdf = (1 / PI) * base_colour.rgb;

    // NOTE(marco): f0 in the formula notation refers to the base colour here
    vec3 conductor_fresnel = specular_brdf * ( base_colour.rgb + ( 1.0 - base_colour.rgb ) * pow( 1.0 - abs( HdotV ), 5 ) );

    // NOTE(marco): f0 in the formula notation refers to the value derived from ior = 1.5
    float f0 = 0.04; // pow( ( 1 - ior ) / ( 1 + ior ), 2 )
    float fr = f0 + ( 1 - f0 ) * pow(1 - abs( HdotV ), 5 );
    vec3 fresnel_mix = mix( diffuse_brdf, vec3( specular_brdf ), fr );

    vec3 material_colour = mix( fresnel_mix, conductor_fresnel, metalness );

    frag_color = vec4( encode_srgb( material_colour ), base_colour.a );
}
)FOO";

        pipeline_creation.shaders.set_name( "Cube" ).add_stage( vs_code, ( uint32_t )strlen( vs_code ), VK_SHADER_STAGE_VERTEX_BIT ).add_stage( fs_code, ( uint32_t )strlen( fs_code ), VK_SHADER_STAGE_FRAGMENT_BIT );

        // Descriptor set layout
        DescriptorSetLayoutCreation cube_rll_creation{};
        cube_rll_creation.add_binding( { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 1, "LocalConstants" } );
        cube_rll_creation.add_binding( { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, 1, "diffuseTexture" } );
        cube_rll_creation.add_binding( { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, 1, "occlusionRoughnessMetalnessTexture" } );
        cube_rll_creation.add_binding( { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, 1, "normalTexture" } );
        cube_rll_creation.add_binding( { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, 1, "MaterialConstant" } );
        // Setting it into pipeline
        cube_dsl = gpu.create_descriptor_set_layout( cube_rll_creation );
        pipeline_creation.add_descriptor_set_layout( cube_dsl );

        // Constant buffer
        BufferCreation buffer_creation;
        buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( UniformData ) ).set_name( "cube_cb" );
        cube_cb = gpu.create_buffer( buffer_creation );

        cube_pipeline = gpu.create_pipeline( pipeline_creation );

        for ( u32 mesh_index = 0; mesh_index < scene.meshes_count; ++mesh_index ) {
            MeshDraw mesh_draw{ };

            glTF::Mesh& mesh = scene.meshes[ mesh_index ];

            for ( u32 primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index ) {
                glTF::MeshPrimitive& mesh_primitive = mesh.primitives[ primitive_index ];

                i32 position_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "POSITION" );
                i32 tangent_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "TANGENT" );
                i32 normal_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "NORMAL" );
                i32 texcoord_accessor_index = gltf_get_attribute_accessor_index( mesh_primitive.attributes, mesh_primitive.attribute_count, "TEXCOORD_0" );

                if ( position_accessor_index != -1 ) {
                    glTF::Accessor& position_accessor = scene.accessors[ position_accessor_index ];
                    glTF::BufferView& position_buffer_view = scene.buffer_views[ position_accessor.buffer_view ];
                    BufferResource& position_buffer_gpu = buffers[ position_accessor.buffer_view ];

                    mesh_draw.position_buffer = position_buffer_gpu.handle;
                    mesh_draw.position_offset = position_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : position_accessor.byte_offset;
                }

                if ( tangent_accessor_index != -1 ) {
                    glTF::Accessor& tangent_accessor = scene.accessors[ tangent_accessor_index ];
                    glTF::BufferView& tangent_buffer_view = scene.buffer_views[ tangent_accessor.buffer_view ];
                    BufferResource& tangent_buffer_gpu = buffers[ tangent_accessor.buffer_view ];

                    mesh_draw.tangent_buffer = tangent_buffer_gpu.handle;
                    mesh_draw.tangent_offset = tangent_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : tangent_accessor.byte_offset;
                }

                if ( normal_accessor_index != -1 ) {
                    glTF::Accessor& normal_accessor = scene.accessors[ normal_accessor_index ];
                    glTF::BufferView& normal_buffer_view = scene.buffer_views[ normal_accessor.buffer_view ];
                    BufferResource& normal_buffer_gpu = buffers[ normal_accessor.buffer_view ];

                    mesh_draw.normal_buffer = normal_buffer_gpu.handle;
                    mesh_draw.normal_offset = normal_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : normal_accessor.byte_offset;
                }

                if ( texcoord_accessor_index != -1 ) {
                    glTF::Accessor& texcoord_accessor = scene.accessors[ texcoord_accessor_index ];
                    glTF::BufferView& texcoord_buffer_view = scene.buffer_views[ texcoord_accessor.buffer_view ];
                    BufferResource& texcoord_buffer_gpu = buffers[ texcoord_accessor.buffer_view ];

                    mesh_draw.texcoord_buffer = texcoord_buffer_gpu.handle;
                    mesh_draw.texcoord_offset = texcoord_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : texcoord_accessor.byte_offset;
                }

                glTF::Accessor& indices_accessor = scene.accessors[ mesh_primitive.indices ];
                glTF::BufferView& indices_buffer_view = scene.buffer_views[ indices_accessor.buffer_view ];
                BufferResource& indices_buffer_gpu = buffers[ indices_accessor.buffer_view ];
                mesh_draw.index_buffer = indices_buffer_gpu.handle;
                mesh_draw.index_offset = indices_accessor.byte_offset == glTF::INVALID_INT_VALUE ? 0 : indices_accessor.byte_offset;

                glTF::Material& material = scene.materials[ mesh_primitive.material ];

                // Descriptor Set
                DescriptorSetCreation ds_creation{};
                ds_creation.set_layout( cube_dsl ).buffer( cube_cb, 0 );

                // NOTE(marco): for now we expect all three textures to be defined. In the next chapter
                // we'll relax this constraint thanks to bindless rendering!

                if ( material.pbr_metallic_roughness != nullptr ) {
                    if ( material.pbr_metallic_roughness->base_color_factor_count != 0 ) {
                        RASSERT( material.pbr_metallic_roughness->base_color_factor_count == 4 );

                        mesh_draw.material_data.base_color_factor = {
                            material.pbr_metallic_roughness->base_color_factor[0],
                            material.pbr_metallic_roughness->base_color_factor[1],
                            material.pbr_metallic_roughness->base_color_factor[2],
                            material.pbr_metallic_roughness->base_color_factor[3],
                        };
                    } else {
                        mesh_draw.material_data.base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
                    }

                    if ( material.pbr_metallic_roughness->base_color_texture != nullptr ) {
                        glTF::Texture& diffuse_texture = scene.textures[ material.pbr_metallic_roughness->base_color_texture->index ];
                        TextureResource& diffuse_texture_gpu = images[ diffuse_texture.source ];
                        SamplerResource& diffuse_sampler_gpu = samplers[ diffuse_texture.sampler ];

                        ds_creation.texture_sampler( diffuse_texture_gpu.handle, diffuse_sampler_gpu.handle, 1 );
                    } else {
                        continue;
                    }

                    if ( material.pbr_metallic_roughness->metallic_roughness_texture != nullptr ) {
                        glTF::Texture& roughness_texture = scene.textures[ material.pbr_metallic_roughness->metallic_roughness_texture->index ];
                        TextureResource& roughness_texture_gpu = images[ roughness_texture.source ];
                        SamplerResource& roughness_sampler_gpu = samplers[ roughness_texture.sampler ];

                        ds_creation.texture_sampler( roughness_texture_gpu.handle, roughness_sampler_gpu.handle, 2 );
                    } else if ( material.occlusion_texture != nullptr ) {
                        glTF::Texture& occlusion_texture = scene.textures[ material.occlusion_texture->index ];

                        TextureResource& occlusion_texture_gpu = images[ occlusion_texture.source ];
                        SamplerResource& occlusion_sampler_gpu = samplers[ occlusion_texture.sampler ];

                        ds_creation.texture_sampler( occlusion_texture_gpu.handle, occlusion_sampler_gpu.handle, 2 );
                    } else {
                        continue;
                    }
                } else {
                    continue;
                }

                if ( material.normal_texture != nullptr ) {
                    glTF::Texture& normal_texture = scene.textures[ material.normal_texture->index ];
                    TextureResource& normal_texture_gpu = images[ normal_texture.source ];
                    SamplerResource& normal_sampler_gpu = samplers[ normal_texture.sampler ];

                    ds_creation.texture_sampler( normal_texture_gpu.handle, normal_sampler_gpu.handle, 3 );
                } else {
                    continue;
                }

                buffer_creation.reset().set( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ResourceUsageType::Dynamic, sizeof( MaterialData ) ).set_name( "material" );
                mesh_draw.material_buffer = gpu.create_buffer( buffer_creation );
                ds_creation.buffer( mesh_draw.material_buffer, 4 );

                mesh_draw.count = indices_accessor.count;

                mesh_draw.descriptor_set = gpu.create_descriptor_set( ds_creation );

                mesh_draws.push( mesh_draw );
            }
        }

        rx = 0.0f;
        ry = 0.0f;
    }

    i64 begin_frame_tick = time_now();

    vec3s eye = vec3s{ 0.0f, 2.5f, 2.0f };
    vec3s look = vec3s{ 0.0f, 0.0, -1.0f };
    vec3s right = vec3s{ 1.0f, 0.0, 0.0f };

    f32 yaw = 0.0f;
    f32 pitch = 0.0f;

    float model_scale = 0.008f;

    while ( !window.requested_exit ) {
        ZoneScoped;

        // New frame
        if ( !window.minimized ) {
            gpu.new_frame();
        }
        //input->new_frame();

        window.handle_os_messages();

        if ( window.resized ) {
            //renderer->resize_swapchain( window.width, window.height );
            //on_resize( window.width, window.height );
            gpu.resize( window.width, window.height );
            window.resized = false;
        }
        // This MUST be AFTER os messages!
        imgui->new_frame();

        const i64 current_tick = time_now();
        f32 delta_time = ( f32 )time_delta_seconds( begin_frame_tick, current_tick );
        begin_frame_tick = current_tick;

        input_handler.new_frame();
        input_handler.update( delta_time );

        if ( ImGui::Begin( "Raptor ImGui" ) ) {
            ImGui::InputFloat("Model scale", &model_scale, 0.001f);
        }
        ImGui::End();

        if ( ImGui::Begin( "GPU" ) ) {
            gpu_profiler.imgui_draw();
        }
        ImGui::End();

        {
            // Update rotating cube gpu data
            MapBufferParameters cb_map = { cube_cb, 0, 0 };
            float* cb_data = ( float* )gpu.map_buffer( cb_map );
            if ( cb_data ) {
                if ( input_handler.is_mouse_down( MouseButtons::MOUSE_BUTTONS_LEFT ) ) {
                    pitch += ( input_handler.mouse_position.y - input_handler.previous_mouse_position.y ) * 0.1f;
                    yaw += ( input_handler.mouse_position.x - input_handler.previous_mouse_position.x ) * 0.3f;

                    pitch = clamp( pitch, -60.0f, 60.0f );

                    if ( yaw > 360.0f ) {
                        yaw -= 360.0f;
                    }

                    mat3s rxm = glms_mat4_pick3( glms_rotate_make( glm_rad( -pitch ), vec3s{ 1.0f, 0.0f, 0.0f } ) );
                    mat3s rym = glms_mat4_pick3( glms_rotate_make( glm_rad( -yaw ), vec3s{ 0.0f, 1.0f, 0.0f } ) );

                    look = glms_mat3_mulv( rxm, vec3s{ 0.0f, 0.0f, -1.0f } );
                    look = glms_mat3_mulv( rym, look );

                    right = glms_cross( look, vec3s{ 0.0f, 1.0f, 0.0f });
                }

                if ( input_handler.is_key_down( Keys::KEY_W ) ) {
                    eye = glms_vec3_add( eye, glms_vec3_scale( look, 5.0f * delta_time ) );
                } else if ( input_handler.is_key_down( Keys::KEY_S ) ) {
                    eye = glms_vec3_sub( eye, glms_vec3_scale( look, 5.0f * delta_time ) );
                }

                if ( input_handler.is_key_down( Keys::KEY_D ) ) {
                    eye = glms_vec3_add( eye, glms_vec3_scale( right, 5.0f * delta_time ) );
                } else if ( input_handler.is_key_down( Keys::KEY_A ) ) {
                    eye = glms_vec3_sub( eye, glms_vec3_scale( right, 5.0f * delta_time ) );
                }

                mat4s view = glms_lookat( eye, glms_vec3_add( eye, look ), vec3s{ 0.0f, 1.0f, 0.0f } );
                mat4s projection = glms_perspective( glm_rad( 60.0f ), gpu.swapchain_width * 1.0f / gpu.swapchain_height, 0.01f, 1000.0f );

                // Calculate view projection matrix
                mat4s view_projection = glms_mat4_mul( projection, view );

                // Rotate cube:
                rx += 1.0f * delta_time;
                ry += 2.0f * delta_time;

                mat4s rxm = glms_rotate_make( rx, vec3s{ 1.0f, 0.0f, 0.0f } );
                mat4s rym = glms_rotate_make( glm_rad( 45.0f ), vec3s{ 0.0f, 1.0f, 0.0f } );

                mat4s sm = glms_scale_make( vec3s{ model_scale, model_scale, model_scale } );
                mat4s model = glms_mat4_mul( rym, sm );

                UniformData uniform_data{ };
                uniform_data.vp = view_projection, model;
                uniform_data.m = model;
                uniform_data.inverseM = glms_mat4_inv( glms_mat4_transpose( model ) );
                uniform_data.eye = vec4s{ eye.x, eye.y, eye.z, 1.0f };
                uniform_data.light = vec4s{ 2.0f, 2.0f, 0.0f, 1.0f };

                memcpy( cb_data, &uniform_data, sizeof( UniformData ) );

                gpu.unmap_buffer( cb_map );
            }
        }

        if ( !window.minimized ) {
            raptor::CommandBuffer* gpu_commands = gpu.get_command_buffer( QueueType::Graphics, true );
            gpu_commands->push_marker( "Frame" );

            gpu_commands->clear( 0.3f, 0.9f, 0.3f, 1.0f );
            gpu_commands->clear_depth_stencil( 1.0f, 0 );
            gpu_commands->bind_pass( gpu.get_swapchain_pass() );
            gpu_commands->bind_pipeline( cube_pipeline );
            gpu_commands->set_scissor( nullptr );
            gpu_commands->set_viewport( nullptr );

            for ( u32 mesh_index = 0; mesh_index < mesh_draws.size; ++mesh_index ) {
                MeshDraw mesh_draw = mesh_draws[ mesh_index ];

                MapBufferParameters material_map = { mesh_draw.material_buffer, 0, 0 };
                MaterialData* material_buffer_data = ( MaterialData* )gpu.map_buffer( material_map );

                memcpy( material_buffer_data, &mesh_draw.material_data, sizeof( MaterialData ) );

                gpu.unmap_buffer( material_map );

                gpu_commands->bind_vertex_buffer( mesh_draw.position_buffer, 0, mesh_draw.position_offset );
                gpu_commands->bind_vertex_buffer( mesh_draw.tangent_buffer, 1, mesh_draw.tangent_offset );
                gpu_commands->bind_vertex_buffer( mesh_draw.normal_buffer, 2, mesh_draw.normal_offset );
                gpu_commands->bind_vertex_buffer( mesh_draw.texcoord_buffer, 3, mesh_draw.texcoord_offset );
                gpu_commands->bind_index_buffer( mesh_draw.index_buffer, mesh_draw.index_offset );
                gpu_commands->bind_descriptor_set( &mesh_draw.descriptor_set, 1, nullptr, 0 );

                gpu_commands->draw_indexed( TopologyType::Triangle, mesh_draw.count, 1, 0, 0, 0 );
            }


            imgui->render( *gpu_commands );

            gpu_commands->pop_marker();

            gpu_profiler.update( gpu );

            // Send commands to GPU
            gpu.queue_command_buffer( gpu_commands );
            gpu.present();

        } else {
            ImGui::Render();
        }

        FrameMark;
    }

    for ( u32 mesh_index = 0; mesh_index < mesh_draws.size; ++mesh_index ) {
        MeshDraw& mesh_draw = mesh_draws[ mesh_index ];
        gpu.destroy_descriptor_set( mesh_draw.descriptor_set );
        gpu.destroy_buffer( mesh_draw.material_buffer );
    }

    mesh_draws.shutdown();

    gpu.destroy_buffer( cube_cb );
    gpu.destroy_pipeline( cube_pipeline );
    gpu.destroy_descriptor_set_layout( cube_dsl );

    imgui->shutdown();

    gpu_profiler.shutdown();

    rm.shutdown();
    renderer.shutdown();

    samplers.shutdown();
    images.shutdown();
    buffers.shutdown();

    resource_name_buffer.shutdown();

    // NOTE(marco): we can't destroy this sooner as textures and buffers
    // hold a pointer to the names stored here
    gltf_free( scene );

    input_handler.shutdown();
    window.unregister_os_messages_callback( input_os_messages_callback );
    window.shutdown();

    MemoryService::instance()->shutdown();

    return 0;
}
