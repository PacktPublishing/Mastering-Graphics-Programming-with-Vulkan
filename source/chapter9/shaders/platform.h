// Header file for common defines

// Global glsl version ///////////////////////////////////////////////////
#version 450

#define GLOBAL_SET 0
#define MATERIAL_SET 1

#define BINDLESS_BINDING 10
#define BINDLESS_IMAGES 11

// Lighting defines //////////////////////////////////////////////////////

// NOTE(marco): needs to be kept in sync with k_light_z_bins
// TODO(marco): use push constant
#define NUM_BINS 16.0
#define BIN_WIDTH ( 1.0 / NUM_BINS )
#define TILE_SIZE 8
#define NUM_LIGHTS 256
#define NUM_WORDS ( ( NUM_LIGHTS + 31 ) / 32 )


// Cubemap defines ///////////////////////////////////////////////////////
#define CUBE_MAP_POSITIVE_X 0
#define CUBE_MAP_NEGATIVE_X 1
#define CUBE_MAP_POSITIVE_Y 2
#define CUBE_MAP_NEGATIVE_Y 3
#define CUBE_MAP_POSITIVE_Z 4
#define CUBE_MAP_NEGATIVE_Z 5
#define CUBE_MAP_COUNT 6

#extension GL_ARB_shader_draw_parameters : enable

// Bindless support //////////////////////////////////////////////////////
// Enable non uniform qualifier extension
#extension GL_EXT_nonuniform_qualifier : enable
// Global bindless support. This should go in a common file.

layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform sampler2D global_textures[];
// Alias textures to use the same binding point, as bindless texture is shared
// between all kind of textures: 1d, 2d, 3d.
layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform sampler3D global_textures_3d[];

layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform samplerCube global_textures_cubemaps[];

layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform samplerCubeArray global_textures_cubemaps_array[];

// Writeonly images do not need format in layout
layout( set = GLOBAL_SET, binding = BINDLESS_IMAGES ) writeonly uniform image2D global_images_2d[];

layout( set = GLOBAL_SET, binding = BINDLESS_IMAGES ) writeonly uniform uimage2D global_uimages_2d[];


// Common constants //////////////////////////////////////////////////////
#define PI 3.1415926538
#define INVALID_TEXTURE_INDEX 65535

// Utility ///////////////////////////////////////////////////////////////
float saturate( float a ) {
    return clamp(a, 0.0f, 1.0f);
}

#if defined (COMPUTE)

// Wave size as a specialization constant.
layout (constant_id = 0) const uint SUBGROUP_SIZE = 32;

// Utility methods for barriers
void global_shader_barrier() {
    memoryBarrierBuffer();
    memoryBarrierShared();
    memoryBarrier();
    barrier();
}

void group_barrier() {
    groupMemoryBarrier();
    memoryBarrierShared();
    barrier();
}
#endif // COMPUTE

// Encoding/Decoding SRGB ////////////////////////////////////////////////
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

// Utility methods ///////////////////////////////////////////////////////
float heaviside( float v ) {
    if ( v > 0.0 )
        return 1.0;
    else
        return 0.0;
}

// Normals Encoding/Decoding /////////////////////////////////////////////
// https://jcgt.org/published/0003/02/01/
vec2 sign_not_zero(vec2 v) {
    return vec2((v.x >= 0.0) ? 1.0 : -1.0, (v.y >= 0.0) ? 1.0 : -1.0);
}

// Taken from https://johnwhite3d.blogspot.com/2017/10/signed-octahedron-normal-encoding.html
vec3 octahedral_decode_old(vec2 f) {
    vec3 n = vec3( f.x, f.y, 1.0 - abs( f.x ) - abs( f.y ) );
    n.xy = n.z >= 0.0 ? n.xy : ( 1.0 - abs( n.xy ) ) * sign_not_zero( n.xy );

    return normalize(n);
}

// This version proposed in a tweet: https://twitter.com/Stubbesaurus/status/937994790553227264?s=20&t=U36PKMj7v2BFeQwDX6gEGQ
vec3 octahedral_decode(vec2 f) {
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0); // Also saturate
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;

    return normalize(n);
}

// float32x3_to_oct
vec2 octahedral_encode(vec3 n) {
    // Project the sphere onto the octahedron, and then onto the xy plane
    vec2 p = n.xy * (1.0f / (abs(n.x) + abs(n.y) + abs(n.z)));
    // Reflect the folds of the lower hemisphere over the diagonals
    return (n.z < 0.0f) ? ((1.0 - abs(p.yx)) * sign_not_zero(p)) : p;
}

//
// Utility method to get world position from raw depth. //////////////////
vec3 world_position_from_depth( vec2 uv, float raw_depth, mat4 inverse_view_projection ) {

    vec4 H = vec4( uv.x * 2 - 1, (1 - uv.y) * 2 - 1, raw_depth, 1.0 );
    vec4 D = inverse_view_projection * H;

    return D.xyz / D.w;
}

vec3 view_position_from_depth( vec2 uv, float raw_depth, mat4 inverse_projection ) {

    vec4 H = vec4( uv.x * 2 - 1, (1 - uv.y) * 2 - 1, raw_depth, 1.0 );
    vec4 D = inverse_projection * H;

    return D.xyz / D.w;
}

vec2 uv_from_pixels( ivec2 pixel_position, uint width, uint height ) {
    return pixel_position / vec2((width - 1) * 1.f, (height - 1) * 1.f);
}
