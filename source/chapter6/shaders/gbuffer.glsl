

#if defined(VERTEX_GBUFFER_NO_CULL) || defined(VERTEX_GBUFFER_CULL)

layout(location=0) in vec3 position;
layout(location=1) in vec4 tangent;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 texCoord0;

layout (location = 0) out vec2 vTexcoord0;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec3 vTangent;
layout (location = 3) out vec3 vBiTangent;
layout (location = 4) out vec3 vPosition;
layout (location = 5) out flat uint mesh_draw_index;

void main() {

    MeshInstanceDraw mesh_draw = mesh_instance_draws[gl_InstanceIndex];
    mesh_draw_index = mesh_draw.mesh_draw_index;

    gl_Position = view_projection * mesh_draw.model * vec4(position, 1.0);
    vec4 worldPosition = mesh_draw.model * vec4(position, 1.0);
    vPosition = worldPosition.xyz / worldPosition.w;

    // NOTE(marco): assume texcoords are always specified for now
    vTexcoord0 = texCoord0;

    uint flags = mesh_draws[mesh_draw_index].flags;
    if ( (flags & DrawFlags_HasNormals) != 0 ) {
        vNormal = normalize( mat3(mesh_draw.model_inverse) * normal );
    }

    if ( (flags & DrawFlags_HasTangents) != 0 ) {
        vTangent = normalize( mat3(mesh_draw.model) * tangent.xyz );

        vBiTangent = cross( vNormal, vTangent ) * tangent.w;
    }
}

#endif // VERTEX

#if defined (FRAGMENT_GBUFFER_NO_CULL) || defined(FRAGMENT_GBUFFER_CULL) || defined(FRAGMENT_GBUFFER_SKINNING)

layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vTangent;
layout (location = 3) in vec3 vBiTangent;
layout (location = 4) in vec3 vPosition;
layout (location = 5) in flat uint mesh_draw_index;

layout (location = 0) out vec4 color_out;
layout (location = 1) out vec2 normal_out;
layout (location = 2) out vec4 occlusion_roughness_metalness_out;
layout (location = 3) out vec4 emissive_out;

void main() {
    MeshDraw mesh_draw = mesh_draws[mesh_draw_index];
    uint flags = mesh_draw.flags;

    vec3 normal = normalize(vNormal);
    if ( (flags & DrawFlags_HasNormals) == 0 ) {
        normal = normalize(cross(dFdx(vPosition), dFdy(vPosition)));
    }

    vec3 tangent = normalize(vTangent);
    vec3 bitangent = normalize(vBiTangent);
    if ( (flags & DrawFlags_HasTangents) == 0 ) {
        vec3 uv_dx = dFdx(vec3(vTexcoord0, 0.0));
        vec3 uv_dy = dFdy(vec3(vTexcoord0, 0.0));

        // NOTE(marco): code taken from https://github.com/KhronosGroup/glTF-Sample-Viewer
        vec3 t_ = (uv_dy.t * dFdx(vPosition) - uv_dx.t * dFdy(vPosition)) /
                  (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);
        tangent = normalize(t_ - normal * dot(normal, t_));

        bitangent = cross( normal, tangent );
    }

    bool phong = ( flags & DrawFlags_Phong ) != 0;

    uvec4 textures = mesh_draw.textures;
    vec4 base_colour = phong ? mesh_draw.diffuse : mesh_draw.base_color_factor;
    if (textures.x != INVALID_TEXTURE_INDEX) {
        vec3 texture_colour = decode_srgb( texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0).rgb );
        base_colour *= vec4( texture_colour, 1.0 );
    }

    bool useAlphaMask = (flags & DrawFlags_AlphaMask) != 0;
    if (useAlphaMask && base_colour.a < mesh_draw.alpha_cutoff) {
        discard;
    }

    bool use_alpha_dither = (flags & DrawFlags_AlphaDither) != 0;
    if ( use_alpha_dither ) {
        float dithered_alpha = dither(gl_FragCoord.xy, base_colour.a);
        if (dithered_alpha < 0.001f) {
            discard;
        }
    }

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

    normal_out.rg = octahedral_encode(normal);

    float metalness = 0.0;
    float roughness = 0.0;
    float occlusion = 0.0;
    if (phong) {
        // TODO(marco): better conversion
        metalness = 0.5;
        roughness = max(pow((1 - mesh_draw.specular_exp), 2), 0.0001);
        emissive_out = vec4( 0, 0, 0, 1 );
    } else {
        roughness = mesh_draw.metallic_roughness_occlusion_factor.x;
        metalness = mesh_draw.metallic_roughness_occlusion_factor.y;

        if (textures.y != INVALID_TEXTURE_INDEX) {
            vec4 rm = texture(global_textures[nonuniformEXT(textures.y)], vTexcoord0);

            // Green channel contains roughness values
            roughness *= rm.g;

            // Blue channel contains metalness
            metalness *= rm.b;
        }

        occlusion = mesh_draw.metallic_roughness_occlusion_factor.z;
        if (textures.w != INVALID_TEXTURE_INDEX) {
            vec4 o = texture(global_textures[nonuniformEXT(textures.w)], vTexcoord0);
            // Red channel for occlusion value
            occlusion *= o.r;
        }

        emissive_out = vec4( mesh_draw.emissive.rgb, 1.0 );
        uint emissive_texture = uint(mesh_draw.emissive.w);
        if ( emissive_texture != INVALID_TEXTURE_INDEX ) {
            emissive_out *= vec4( decode_srgb( texture(global_textures[nonuniformEXT(emissive_texture)], vTexcoord0).rgb ), 1.0 );
        }
    }

    occlusion_roughness_metalness_out.rgb = vec3( occlusion, roughness, metalness );

    color_out = base_colour;
}

#endif // FRAGMENT
