

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
    
    // Diffuse color
    vec4 base_colour = compute_diffuse_color( mesh_draw.base_color_factor, mesh_draw.textures.x, vTexcoord0 );

    const uint flags = mesh_draw.flags;

    apply_alpha_discards( flags, base_colour.a, mesh_draw.alpha_cutoff );

    color_out = base_colour;

    // Geometric Normals
    vec3 world_position = vPosition.xyz;

    vec3 normal = normalize(vNormal);
    vec3 tangent = normalize(vTangent);
    vec3 bitangent = normalize(vBiTangent);

    calculate_geometric_TBN( normal, tangent, bitangent, vTexcoord0.xy, world_position, flags );

    // Pixel normals
    normal = apply_pixel_normal( mesh_draw.textures.z, vTexcoord0.xy, normal, tangent, bitangent );

    normal_out.rg = octahedral_encode(normal);

    // PBR Parameters
    occlusion_roughness_metalness_out.rgb = calculate_pbr_parameters( mesh_draw.metallic_roughness_occlusion_factor.x, mesh_draw.metallic_roughness_occlusion_factor.y,
                                                                      mesh_draw.textures.y, mesh_draw.metallic_roughness_occlusion_factor.z, mesh_draw.textures.w, vTexcoord0.xy );

    emissive_out = vec4( calculate_emissive(mesh_draw.emissive.rgb, uint(mesh_draw.emissive.w), vTexcoord0.xy ), 1.0 );
}

#endif // FRAGMENT
