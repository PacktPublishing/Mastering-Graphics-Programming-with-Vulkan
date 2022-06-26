// Header file for common defines

// Global glsl version ///////////////////////////////////////////////////
#version 450

#define GLOBAL_SET 0
#define MATERIAL_SET 1

#define BINDLESS_BINDING 10
#define BINDLESS_IMAGES 11

// Bindless support //////////////////////////////////////////////////////
// Enable non uniform qualifier extension
#extension GL_EXT_nonuniform_qualifier : enable
// Global bindless support. This should go in a common file.

layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform sampler2D global_textures[];
// Alias textures to use the same binding point, as bindless texture is shared
// between all kind of textures: 1d, 2d, 3d.
layout ( set = GLOBAL_SET, binding = BINDLESS_BINDING ) uniform sampler3D global_textures_3d[];

layout( set = GLOBAL_SET, binding = BINDLESS_IMAGES ) writeonly uniform image2D global_images_2d[];


// Common constants //////////////////////////////////////////////////////
#define PI 3.1415926538
#define INVALID_TEXTURE_INDEX 65535

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
    if ( v > 0.0 ) return 1.0;
    else return 0.0;
}