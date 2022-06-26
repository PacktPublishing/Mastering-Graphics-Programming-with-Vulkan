

#if defined(VERTEX)

layout (location = 0) out vec2 vTexCoord;
layout (location = 1) flat out uint out_texture_id;

void main() {

    vTexCoord.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vTexCoord.xy * 2.0f - 1.0f, 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;

    out_texture_id = gl_InstanceIndex;
}

#endif // VERTEX

#if defined(FRAGMENT)

layout (location = 0) in vec2 vTexCoord;
layout (location = 1) flat in uint texture_id;

layout (location = 0) out vec4 out_color;


layout ( std140, set = MATERIAL_SET, binding = 0 ) uniform LocalConstants {
    mat4        view_projection;
    vec4        eye;
    vec4        light;
    float       light_range;
    float       light_intensity;
};

void main() {
    vec4 color = texture(global_textures[nonuniformEXT(texture_id)], vTexCoord.xy);
    out_color = color;
}

#endif // FRAGMENT