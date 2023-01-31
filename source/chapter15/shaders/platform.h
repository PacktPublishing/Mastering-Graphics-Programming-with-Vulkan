// Header file for common defines

// Global glsl version ///////////////////////////////////////////////////
#version 460

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
layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform usampler2D global_utextures[];

layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform sampler3D global_textures_3d[];

layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform usampler3D global_utextures_3d[];

layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform samplerCube global_textures_cubemaps[];

layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform samplerCubeArray global_textures_cubemaps_array[];

// Writeonly images do not need format in layout
layout( set = GLOBAL_SET, binding = BINDLESS_IMAGES ) writeonly uniform image2D global_images_2d[];

layout( set = GLOBAL_SET, binding = BINDLESS_IMAGES ) writeonly uniform image3D global_images_3d[];

layout( set = GLOBAL_SET, binding = BINDLESS_IMAGES ) writeonly uniform uimage2D global_uimages_2d[];

layout( set = GLOBAL_SET, binding = BINDLESS_IMAGES ) writeonly uniform uimage3D global_uimages_3d[];

// Common constants //////////////////////////////////////////////////////
#define PI 3.1415926538
#define INVALID_TEXTURE_INDEX 65535

// Utility ///////////////////////////////////////////////////////////////
float saturate( float a ) {
    return clamp(a, 0.0f, 1.0f);
}

vec3 saturate(vec3 v) {
    return vec3(saturate(v.x), saturate(v.y), saturate(v.z));
}

uint vec4_to_rgba( vec4 color ) {
    return (uint(color.r * 255.f) | (uint(color.g * 255.f) << 8) |
           (uint(color.b * 255.f) << 16) | ((uint(color.a * 255.f) << 24)));
}

vec4 unpack_color_rgba( uint color ) {
    return vec4( ( color & 0xffu ) / 255.f,
                 ( ( color >> 8u ) & 0xffu ) / 255.f,
                 ( ( color >> 16u ) & 0xffu ) / 255.f,
                 ( ( color >> 24u ) & 0xffu ) / 255.f );
}

vec4 unpack_color_abgr( uint color ) {
    return vec4( ( ( color >> 24u ) & 0xffu ) / 255.f,
                 ( ( color >> 16u ) & 0xffu ) / 255.f,
                 ( ( color >> 8u ) & 0xffu ) / 255.f,
                 ( color & 0xffu ) / 255.f );
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
// sRGB to Linear.
// Assuming using sRGB typed textures this should not be needed.
float ToLinear1(float c) {
    return ( c <= 0.04045 ) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

// Linear to sRGB.
// Assuing using sRGB typed textures this should not be needed.
float ToSrgb1(float c){
    return (c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055);
}

vec3 to_srgb(vec3 c) {
    return vec3( ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b) );
}

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

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// https://software.intel.com/en-us/node/503873
vec3 rgb_to_ycocg(vec3 c)
{
    // Y = R/4 + G/2 + B/4
    // Co = R/2 - B/2
    // Cg = -R/4 + G/2 - B/4
    return vec3(c.x/4.0 + c.y/2.0 + c.z/4.0,
                c.x/2.0 - c.z/2.0,
                -c.x/4.0 + c.y/2.0 - c.z/4.0 );
}

// https://software.intel.com/en-us/node/503873
vec3 ycocg_to_rgb(vec3 c)
{
    // R = Y + Co - Cg
    // G = Y + Cg
    // B = Y - Co - Cg
    return clamp(vec3(c.x + c.y - c.z,
                      c.x + c.z,
                      c.x - c.y - c.z), vec3(0), vec3(1));
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

// vec32x3_to_oct
vec2 octahedral_encode(vec3 n) {
    // Project the sphere onto the octahedron, and then onto the xy plane
    vec2 p = n.xy * (1.0f / (abs(n.x) + abs(n.y) + abs(n.z)));
    // Reflect the folds of the lower hemisphere over the diagonals
    return (n.z < 0.0f) ? ((1.0 - abs(p.yx)) * sign_not_zero(p)) : p;
}

// Coordinate conversions ////////////////////////////////////////////////
vec3 ndc_from_uv_raw_depth( vec2 uv, float raw_depth ) {
    return vec3( uv.x * 2 - 1, (1 - uv.y) * 2 - 1, raw_depth );
}

// Utility method to get world position from raw depth. //////////////////
vec3 world_position_from_depth( vec2 uv, float raw_depth, mat4 inverse_view_projection ) {

    vec4 H = vec4( ndc_from_uv_raw_depth(uv, raw_depth), 1.0 );
    vec4 D = inverse_view_projection * H;

    return D.xyz / D.w;
}

vec3 view_position_from_depth( vec2 uv, float raw_depth, mat4 inverse_projection ) {

    vec4 H = vec4( ndc_from_uv_raw_depth(uv, raw_depth), 1.0 );
    vec4 D = inverse_projection * H;

    return D.xyz / D.w;
}

vec2 uv_from_pixels( ivec2 pixel_position, uint width, uint height ) {
    return pixel_position / vec2(width * 1.f, height * 1.f);
}

vec2 uv_nearest( ivec2 pixel, vec2 texture_size ) {
    vec2 uv = floor(pixel) + .5;

    return uv / texture_size;
}

// Convert raw_depth (0..1) to linear depth (near...far)
float raw_depth_to_linear_depth( float raw_depth, float near, float far ) {
    // NOTE(marco): Vulkan depth is [0, 1]
    return near * far / (far + raw_depth * (near - far));
}

// Convert linear depth (near...far) to raw_depth (0..1)
float linear_depth_to_raw_depth( float linear_depth, float near, float far ) {
    return ( near * far ) / ( linear_depth * ( near - far ) ) - far / ( near - far );
}

// Exponential distribution as in https://advances.realtimerendering.com/s2016/Siggraph2016_idTech6.pdf
// Convert slice index to (near...far) value distributed with exponential function.
float slice_to_exponential_depth( float near, float far, int slice, int num_slices ) {
    return near * pow( far / near, (float(slice) + 0.5f) / float(num_slices) );
}

float slice_to_exponential_depth_jittered( float near, float far, float jitter, int slice, int num_slices ) {
    return near * pow( far / near, (float(slice) + 0.5f + jitter) / float(num_slices) );
}

// http://www.aortiz.me/2018/12/21/CG.html
// Convert linear depth (near...far) to (0...1) value distributed with exponential functions
// like slice_to_exponential_depth.
// This function is performing all calculations, a more optimized one precalculates factors on CPU.
float linear_depth_to_uv( float near, float far, float linear_depth, int num_slices ) {
    const float one_over_log_f_over_n = 1.0f / log2( far / near );
    const float scale = num_slices * one_over_log_f_over_n;
    const float bias = - ( num_slices * log2(near) * one_over_log_f_over_n );

    return max(log2(linear_depth) * scale + bias, 0.0f) / float(num_slices);
}

// Convert linear depth (near...far) to (0...1) value distributed with exponential functions.
// Uses CPU cached values of scale and bias.
float linear_depth_to_uv_optimized( float scale, float bias, float linear_depth, int num_slices ) {
    return max(log2(linear_depth) * scale + bias, 0.0f) / float(num_slices);
}

// Noise helper functions ////////////////////////////////////////////////
float remap_noise_tri( float v ) {
    v = v * 2.0 - 1.0;
    return sign(v) * (1.0 - sqrt(1.0 - abs(v)));
}

// Takes 2 noises in space [0..1] and remaps them in [-1..1]
float triangular_noise( float noise0, float noise1 ) {
    return noise0 + noise1 - 1.0f;
}

float interleaved_gradient_noise(vec2 pixel, int frame) {
    pixel += (float(frame) * 5.588238f);
    return fract(52.9829189f * fract(0.06711056f*float(pixel.x) + 0.00583715f*float(pixel.y)));
}

vec2 interleaved_gradient_noise2(vec2 pixel, int frame) {
    float noise_x = interleaved_gradient_noise( pixel, frame );
    float noise_y = interleaved_gradient_noise( pixel, frame + 1 );

    return vec2( noise_x * 2 - 1, noise_y * 2 - 1 );
}

vec2 blue_noise2(vec2 pixel, int frame, uint blue_noise_texture_index) {
    vec2 uv = vec2(pixel.xy / vec2(128.f));
    // Read blue noise from texture
    vec2 blue_noise = texture(global_textures[nonuniformEXT(blue_noise_texture_index)], uv ).rg;
    const float k_golden_ratio_conjugate = 0.61803398875;
    float blue_noise0 = fract(ToLinear1(blue_noise.r) + float(frame % 256) * k_golden_ratio_conjugate);
    float blue_noise1 = fract(ToLinear1(blue_noise.g) + float(frame % 256) * k_golden_ratio_conjugate);

    return vec2(blue_noise0, blue_noise1);
}

// Custom filtering

// https://gist.github.com/Fewes/59d2c831672040452aa77da6eaab2234
vec4 tricubic_filtering( uint texture_index, vec3 uvw, vec3 texture_size ) {

    // Shift the coordinate from [0,1] to [-0.5, texture_size-0.5]
    vec3 coord_grid = uvw * texture_size - 0.5;
    vec3 index = floor(coord_grid);
    vec3 fraction = coord_grid - index;
    vec3 one_frac = 1.0 - fraction;

    vec3 w0 = 1.0/6.0 * one_frac*one_frac*one_frac;
    vec3 w1 = 2.0/3.0 - 0.5 * fraction*fraction*(2.0-fraction);
    vec3 w2 = 2.0/3.0 - 0.5 * one_frac*one_frac*(2.0-one_frac);
    vec3 w3 = 1.0/6.0 * fraction*fraction*fraction;

    vec3 g0 = w0 + w1;
    vec3 g1 = w2 + w3;
    vec3 mult = 1.0 / texture_size;
    vec3 h0 = mult * ((w1 / g0) - 0.5 + index); //h0 = w1/g0 - 1, move from [-0.5, textureSize-0.5] to [0,1]
    vec3 h1 = mult * ((w3 / g1) + 1.5 + index); //h1 = w3/g1 + 1, move from [-0.5, textureSize-0.5] to [0,1]

    // Fetch the eight linear interpolations
    // Weighting and fetching is interleaved for performance and stability reasons
    vec4 tex000 = textureLod(global_textures_3d[nonuniformEXT(texture_index)], h0, 0);
    vec4 tex100 = textureLod(global_textures_3d[nonuniformEXT(texture_index)], vec3(h1.x, h0.y, h0.z), 0);
    tex000 = mix(tex100, tex000, g0.x); // Weigh along the x-direction

    vec4 tex010 = textureLod(global_textures_3d[nonuniformEXT(texture_index)], vec3(h0.x, h1.y, h0.z), 0);
    vec4 tex110 = textureLod(global_textures_3d[nonuniformEXT(texture_index)], vec3(h1.x, h1.y, h0.z), 0);
    // vec4 tex010 = tex3Dlod(tex, vec4(h0.x, h1.y, h0.z, 0));
    // vec4 tex110 = tex3Dlod(tex, vec4(h1.x, h1.y, h0.z, 0));
    tex010 = mix(tex110, tex010, g0.x); // Weigh along the x-direction
    tex000 = mix(tex010, tex000, g0.y); // Weigh along the y-direction

    vec4 tex001 = textureLod(global_textures_3d[nonuniformEXT(texture_index)], vec3(h0.x, h0.y, h1.z), 0);
    vec4 tex101 = textureLod(global_textures_3d[nonuniformEXT(texture_index)], vec3(h1.x, h0.y, h1.z), 0);
    // vec4 tex001 = tex3Dlod(tex, vec4(h0.x, h0.y, h1.z, 0));
    // vec4 tex101 = tex3Dlod(tex, vec4(h1.x, h0.y, h1.z, 0));
    tex001 = mix(tex101, tex001, g0.x); // Weigh along the x-direction

    vec4 tex011 = textureLod(global_textures_3d[nonuniformEXT(texture_index)], vec3(h0.x, h1.y, h1.z), 0);
    vec4 tex111 = textureLod(global_textures_3d[nonuniformEXT(texture_index)], vec3(h1.x, h1.y, h1.z), 0);
    // vec4 tex011 = tex3Dlod(tex, vec4(h0.x, h1.y, h1.z, 0));
    // vec4 tex111 = tex3Dlod(tex, vec4(h1, 0));
    tex011 = mix(tex111, tex011, g0.x); // Weigh along the x-direction
    tex001 = mix(tex011, tex001, g0.y); // Weigh along the y-direction

    return mix(tex001, tex000, g0.z); // Weigh along the z-direction
}
