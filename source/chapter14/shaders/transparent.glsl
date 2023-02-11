
#if defined(VERTEX_TRANSPARENT_NO_CULL) || defined(VERTEX_TRANSPARENT_CULL)

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

    vec4 worldPosition = mesh_draw.model * vec4(position, 1.0);
    gl_Position = view_projection * worldPosition;
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

#if defined (FRAGMENT_TRANSPARENT_NO_CULL) || defined(FRAGMENT_TRANSPARENT_CULL) || defined(FRAGMENT_TRANSPARENT_SKINNING_NO_CULL)

layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vTangent;
layout (location = 3) in vec3 vBiTangent;
layout (location = 4) in vec3 vPosition;
layout (location = 5) in flat uint mesh_draw_index;

layout (location = 0) out vec4 frag_color;

void main() {
    MeshDraw mesh_draw = mesh_draws[mesh_draw_index];

    // Diffuse color
    vec4 base_colour = compute_diffuse_color( mesh_draw.base_color_factor, mesh_draw.textures.x, vTexcoord0 );

    const uint flags = mesh_draw.flags;

    apply_alpha_discards( flags, base_colour.a, mesh_draw.alpha_cutoff );

    // Geometric Normals
    vec3 world_position = vPosition.xyz;

    vec3 normal = normalize(vNormal);
    vec3 tangent = normalize(vTangent);
    vec3 bitangent = normalize(vBiTangent);

    calculate_geometric_TBN( normal, tangent, bitangent, vTexcoord0.xy, world_position, flags );

    // Pixel normals
    normal = apply_pixel_normal( mesh_draw.textures.z, vTexcoord0.xy, normal, tangent, bitangent );

    vec3 pbr_parameters = calculate_pbr_parameters( mesh_draw.metallic_roughness_occlusion_factor.x, mesh_draw.metallic_roughness_occlusion_factor.y,
                                                                      mesh_draw.textures.y, mesh_draw.metallic_roughness_occlusion_factor.z, mesh_draw.textures.w, vTexcoord0.xy );

    vec3 emissive_colour = calculate_emissive(mesh_draw.emissive.rgb, uint(mesh_draw.emissive.w), vTexcoord0.xy );

    uvec2 position = uvec2(gl_FragCoord.x - 0.5, gl_FragCoord.y - 0.5);
    position.y = uint( resolution.y ) - position.y;

    frag_color = calculate_lighting( base_colour, pbr_parameters, normal, emissive_colour, world_position, position, vec2(0,0), true );
}

#endif // FRAGMENT
