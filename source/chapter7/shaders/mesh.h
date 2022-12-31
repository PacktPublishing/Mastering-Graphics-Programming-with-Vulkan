
#ifndef RAPTOR_GLSL_MESH_H
#define RAPTOR_GLSL_MESH_H

uint DrawFlags_AlphaMask    = 1 << 0;
uint DrawFlags_DoubleSided  = 1 << 1;
uint DrawFlags_Transparent  = 1 << 2;
uint DrawFlags_Phong        = 1 << 3;
uint DrawFlags_HasNormals   = 1 << 4;
uint DrawFlags_TexCoords    = 1 << 5;
uint DrawFlags_HasTangents  = 1 << 6;
uint DrawFlags_HasJoints    = 1 << 7;
uint DrawFlags_HasWeights   = 1 << 8;
uint DrawFlags_AlphaDither  = 1 << 9;

struct MeshDraw {

    // x = diffuse index, y = roughness index, z = normal index, w = occlusion index.
    // Occlusion and roughness are encoded in the same texture
    uvec4       textures;
    vec4        emissive;
    vec4        base_color_factor;
    vec4        metallic_roughness_occlusion_factor;

    uint        flags;
    float       alpha_cutoff;
    uint        vertexOffset; // == meshes[meshIndex].vertexOffset, helps data locality in mesh shader
    uint        meshIndex;

    uint        meshlet_offset;
    uint        meshlet_count;
    uint        meshlet_index_count;
    uint        pad001;
};

struct MeshInstanceDraw {
    mat4        model;
    mat4        model_inverse;

    uint        mesh_draw_index;
    uint        pad000;
    uint        pad001;
    uint        pad002;
};

struct MeshDrawCommand
{
    uint        drawId;

    // VkDrawIndexedIndirectCommand
    uint        indexCount;
    uint        instanceCount;
    uint        firstIndex;
    uint        vertexOffset;
    uint        firstInstance;

    // VkDrawMeshTasksIndirectCommandNV
    uint        taskCount;
    uint        firstTask;
};

layout ( std430, set = MATERIAL_SET, binding = 2 ) readonly buffer MeshDraws {

    MeshDraw    mesh_draws[];
};

layout ( std430, set = MATERIAL_SET, binding = 10 ) readonly buffer MeshInstanceDraws {

    MeshInstanceDraw mesh_instance_draws[];
};

layout ( std430, set = MATERIAL_SET, binding = 12 ) readonly buffer MeshBounds {

    vec4        mesh_bounds[];
};

// Material calculations /////////////////////////////////////////////////
vec4 compute_diffuse_color(inout vec4 base_color, uint albedo_texture, vec2 uv) {
    if (albedo_texture != INVALID_TEXTURE_INDEX) {
        vec3 texture_colour = decode_srgb( texture(global_textures[nonuniformEXT(albedo_texture)], uv).rgb );
        base_color *= vec4( texture_colour, 1.0 );
    }

    return base_color;
}

vec4 compute_diffuse_color_alpha(inout vec4 base_color, uint albedo_texture, vec2 uv) {
    if (albedo_texture != INVALID_TEXTURE_INDEX) {
        vec4 texture_color = texture(global_textures[nonuniformEXT(albedo_texture)], uv);
        base_color *= vec4( decode_srgb( texture_color.rgb ), texture_color.a );
    }

    return base_color;
}

vec3 apply_pixel_normal( uint normal_texture, vec2 uv, vec3 normal, vec3 tangent, vec3 bitangent ) {

    if (normal_texture != INVALID_TEXTURE_INDEX) {
        // NOTE(marco): normal textures are encoded to [0, 1] but need to be mapped to [-1, 1] value
        const vec3 bump_normal = normalize( texture(global_textures[nonuniformEXT(normal_texture)], uv).rgb * 2.0 - 1.0 );
        const mat3 TBN = mat3(
            tangent,
            bitangent,
            normal
        );

        normal = normalize(TBN * normalize(bump_normal));
    }

    return normal;
}

vec3 calculate_pbr_parameters( float metalness, float roughness, uint rm_texture, float occlusion, uint occlusion_texture, vec2 uv ) {
    if (rm_texture != INVALID_TEXTURE_INDEX) {
        vec4 rm = texture(global_textures[nonuniformEXT(rm_texture)], uv);

        // Green channel contains roughness values (read as first element in rm)
        roughness *= rm.g;

        // Blue channel contains metalness (read as second element in rm)
        metalness *= rm.b;
    }

    if (occlusion_texture != INVALID_TEXTURE_INDEX) {
        vec4 o = texture(global_textures[nonuniformEXT(occlusion_texture)], uv);
        // Red channel for occlusion value
        occlusion *= o.r;
    }

    return vec3(occlusion, roughness, metalness);
}

vec3 calculate_emissive( vec3 emissive_color, uint emissive_texture, vec2 uv ) {

    if ( emissive_texture != INVALID_TEXTURE_INDEX ) {
        emissive_color *= decode_srgb( texture(global_textures[nonuniformEXT(emissive_texture)], uv).rgb );
    }

    return emissive_color;
}

#if defined (FRAGMENT)

// Apply both alpha mask and/or dithering based on flags
void apply_alpha_discards( uint flags, float alpha, float alpha_cutoff ) {

    bool use_alpha_mask = (flags & DrawFlags_AlphaMask) != 0;
    if (use_alpha_mask && alpha < alpha_cutoff) {
        discard;
    }

    bool use_alpha_dither = (flags & DrawFlags_AlphaDither) != 0;
    if ( use_alpha_dither ) {
        float dithered_alpha = dither(gl_FragCoord.xy, alpha);
        if (dithered_alpha < 0.001f) {
            discard;
        }
    }
}

// Calculate geometric normal/tangent/bitangent
void calculate_geometric_TBN( inout vec3 vertex_normal, inout vec3 vertex_tangent, inout vec3 vertex_bitangent,
                              vec2 uv, vec3 vertex_world_position, uint flags ) {

    vec3 normal = normalize(vertex_normal);
    if ( (flags & DrawFlags_HasNormals) == 0 ) {
        normal = normalize(cross(dFdx(vertex_world_position), dFdy(vertex_world_position)));
    }

    vec3 tangent = normalize(vertex_tangent);
    vec3 bitangent = normalize(vertex_bitangent);
    if ( (flags & DrawFlags_HasTangents) == 0 ) {
        vec3 uv_dx = dFdx(vec3(uv, 0.0));
        vec3 uv_dy = dFdy(vec3(uv, 0.0));

        // NOTE(marco): code taken from https://github.com/KhronosGroup/glTF-Sample-Viewer
        vec3 t_ = (uv_dy.t * dFdx(vertex_world_position) - uv_dx.t * dFdy(vertex_world_position)) /
                  (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);
        tangent = normalize(t_ - normal * dot(normal, t_));

        bitangent = cross( normal, tangent );
    }

    if (gl_FrontFacing == false)
    {
        tangent *= -1.0;
        bitangent *= -1.0;
        normal *= -1.0;
    }

    vertex_normal = normal;
    vertex_tangent = tangent;
    vertex_bitangent = bitangent;
}

#endif // FRAGMENT

#endif // RAPTOR_GLSL_MESH_H
