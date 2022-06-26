#version 450

layout ( std140, binding = 0 ) uniform LocalConstants {
    mat4        view_projection;
    vec4        eye;
    vec4        light;
    float       light_range;
    float       light_intensity;
};

uint DrawFlags_AlphaMask = 1 << 0;

layout ( std140, binding = 1 ) uniform Mesh {

    mat4        model;
    mat4        model_inverse;

    // x = diffuse index, y = roughness index, z = normal index, w = occlusion index.
    // Occlusion and roughness are encoded in the same texture
    uvec4       textures;
    vec4        base_color_factor;
    vec4        metallic_roughness_occlusion_factor;
    float       alpha_cutoff;
    uint        flags;
};

// Bindless support
// Enable non uniform qualifier extension
#extension GL_EXT_nonuniform_qualifier : enable
// Global bindless support. This should go in a common file.

layout ( set = 1, binding = 10 ) uniform sampler2D global_textures[];
// Alias textures to use the same binding point, as bindless texture is shared
// between all kind of textures: 1d, 2d, 3d.
layout ( set = 1, binding = 10 ) uniform sampler3D global_textures_3d[];


layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vTangent;
layout (location = 3) in vec3 vBiTangent;
layout (location = 4) in vec3 vPosition;

layout (location = 0) out vec4 frag_color;

#define PI 3.1415926538
#define INVALID_TEXTURE_INDEX 65535

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
    vec4 base_colour = texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0) * base_color_factor;

    bool useAlphaMask = (flags & DrawFlags_AlphaMask) != 0;
    if (useAlphaMask && base_colour.a < alpha_cutoff) {
        base_colour.a = 0.0;
    }

    vec3 normal = normalize( vNormal );
    vec3 tangent = normalize( vTangent );
    vec3 bitangent = normalize( vBiTangent );

    if (gl_FrontFacing == false)
    {
        tangent *= -1.0;
        bitangent *= -1.0;
        normal *= -1.0;
    }

    if (textures.z != INVALID_TEXTURE_INDEX) {
        // NOTE(marco): normal textures are encoded to [0, 1] but need to be mapped to [-1, 1] value
        vec3 bump_normal = normalize( texture(global_textures[nonuniformEXT(textures.z)], vTexcoord0).rgb * 2.0 - 1.0 );
        mat3 TBN = mat3(
            tangent,
            bitangent,
            normal
        );

        normal = normalize(TBN * normalize(bump_normal));
    }

    vec3 V = normalize( eye.xyz - vPosition );
    vec3 L = normalize( light.xyz - vPosition );
    vec3 N = normal;
    vec3 H = normalize( L + V );

    float metalness = metallic_roughness_occlusion_factor.x;
    float roughness = metallic_roughness_occlusion_factor.y;

    if (textures.w != INVALID_TEXTURE_INDEX) {
        vec4 rm = texture(global_textures[nonuniformEXT(textures.y)], vTexcoord0);

        // Green channel contains roughness values
        roughness *= rm.g;

        // Blue channel contains metalness
        metalness *= rm.b;
    }

    float alpha = pow(roughness, 2.0);

    float occlusion = metallic_roughness_occlusion_factor.z;
    if (textures.w != INVALID_TEXTURE_INDEX) {
        vec4 o = texture(global_textures[nonuniformEXT(textures.w)], vTexcoord0);
        // Red channel for occlusion value
        occlusion *= o.r;
    }

    base_colour.rgb = decode_srgb( base_colour.rgb );

    // https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#specular-brdf
    float NdotH = clamp(dot(N, H), 0, 1);
    float alpha_squared = alpha * alpha;
    float d_denom = ( NdotH * NdotH ) * ( alpha_squared - 1.0 ) + 1.0;
    float distribution = ( alpha_squared * heaviside( NdotH ) ) / ( PI * d_denom * d_denom );

    float NdotL = clamp(dot(N, L), 0, 1);
    float NdotV = clamp(dot(N, V), 0, 1);
    float HdotL = clamp(dot(H, L), 0, 1);
    float HdotV = clamp(dot(H, V), 0, 1);

    float distance = length(light.xyz - vPosition);
    float intensity = light_intensity * max(min(1.0 - pow(distance / light_range, 4.0), 1.0), 0.0) / pow(distance, 2.0);

    vec3 material_colour = vec3(0, 0, 0);
    if (NdotL > 0.0 || NdotV > 0.0)
    {
        float visibility = ( heaviside( HdotL ) / ( abs( NdotL ) + sqrt( alpha_squared + ( 1.0 - alpha_squared ) * ( NdotL * NdotL ) ) ) ) * ( heaviside( HdotV ) / ( abs( NdotV ) + sqrt( alpha_squared + ( 1.0 - alpha_squared ) * ( NdotV * NdotV ) ) ) );

        float specular_brdf = intensity * NdotL * visibility * distribution;

        vec3 diffuse_brdf = intensity * NdotL * (1 / PI) * base_colour.rgb;

        // NOTE(marco): f0 in the formula notation refers to the base colour here
        vec3 conductor_fresnel = specular_brdf * ( base_colour.rgb + ( 1.0 - base_colour.rgb ) * pow( 1.0 - abs( HdotV ), 5 ) );

        // NOTE(marco): f0 in the formula notation refers to the value derived from ior = 1.5
        float f0 = 0.04; // pow( ( 1 - ior ) / ( 1 + ior ), 2 )
        float fr = f0 + ( 1 - f0 ) * pow(1 - abs( HdotV ), 5 );
        vec3 fresnel_mix = mix( diffuse_brdf, vec3( specular_brdf ), fr );

        material_colour = mix( fresnel_mix, conductor_fresnel, metalness );
    }

    frag_color = vec4( encode_srgb( material_colour ), base_colour.a );
}
