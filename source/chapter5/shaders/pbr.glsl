
// Common code

layout ( std140, set = MATERIAL_SET, binding = 1 ) uniform LightingConstants {

    // x = albedo index, y = roughness index, z = normal index, w = position index.
    // Occlusion and roughness are encoded in the same texture
    uvec4       textures;

    uint        output_index;   // Used by compute
    uint        output_width;
    uint        output_height;
    uint        emissive_index;
};


#if defined(VERTEX)

layout(location=0) in vec3 position;

layout (location = 0) out vec2 vTexcoord0;

void main() {

    vTexcoord0.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vTexcoord0.xy * 2.0f - 1.0f, 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

#endif // VERTEX

#if defined (FRAGMENT)

layout (location = 0) in vec2 vTexcoord0;

layout (location = 0) out vec4 frag_color;

void main() {
    vec4 base_colour = texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0);
    vec3 orm = texture(global_textures[nonuniformEXT(textures.y)], vTexcoord0).rgb;
    vec2 encoded_normal = texture(global_textures[nonuniformEXT(textures.z)], vTexcoord0).rg;
    vec3 normal = octahedral_decode(encoded_normal);
    vec3 vPosition = texture(global_textures[nonuniformEXT(textures.w)], vTexcoord0).rgb;
    vec3 emissive = texture(global_textures[nonuniformEXT(emissive_index)], vTexcoord0).rgb;

    frag_color = calculate_lighting( base_colour, orm, normal, emissive, vPosition );
}

#endif // FRAGMENT

#if defined(COMPUTE)

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
    ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);

    vec4 base_colour = texelFetch(global_textures[nonuniformEXT(textures.x)], pos.xy, 0);
    vec3 orm = texelFetch(global_textures[nonuniformEXT(textures.y)], pos.xy, 0).rgb;
    vec2 encoded_normal = texelFetch(global_textures[nonuniformEXT(textures.z)], pos.xy, 0).rg;
    vec3 normal = octahedral_decode(encoded_normal);
    vec3 emissive = texelFetch(global_textures[nonuniformEXT(emissive_index)], pos.xy, 0).rgb;

    vec4 color = vec4(0);

    const float raw_depth = texelFetch(global_textures[nonuniformEXT(textures.w)], pos.xy, 0).r;
    if ( raw_depth == 1.0f ) {
        color = vec4(base_colour.rgb, 1);
    }
    else {
        const vec2 screen_uv = uv_from_pixels(pos.xy, output_width, output_height);
        const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

        color = calculate_lighting( base_colour, orm, normal, emissive, pixel_world_position );
    }

    imageStore(global_images_2d[output_index], pos.xy, color);
}

#endif // COMPUTE
