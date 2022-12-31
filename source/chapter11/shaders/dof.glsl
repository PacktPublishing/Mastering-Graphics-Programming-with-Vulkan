

#if defined(VERTEX_MAIN)

layout(location=0) in vec3 position;

layout (location = 0) out vec2 vTexcoord0;

void main() {

    vTexcoord0.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vTexcoord0.xy * 2.0f - 1.0f, 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

#endif // VERTEX

#if defined (FRAGMENT_MAIN)

layout ( std140, set = MATERIAL_SET, binding = 0 ) uniform CameraParameters {

    // x = diffuse index, y = depth index
    uvec4       textures;
    float       znear;
    float       zfar;
    float       focal_length;
    float       plane_in_focus;
    float       aperture;
};

layout (location = 0) in vec2 vTexcoord0;

layout (location = 0) out vec4 frag_color;

void main() {
    float z = texture(global_textures[nonuniformEXT(textures.y)], vTexcoord0).r;

    if (z < 1.0)
    {
        float object_distance = -zfar * znear / (z * (zfar - znear) - zfar);
        // float coc_scale = (aperture * focal_length * plane_in_focus * (zfar - znear)) / ((plane_in_focus - focal_length) * znear * zfar);
        // float coc_bias = (aperture * focal_length * (znear - plane_in_focus)) / ((plane_in_focus * focal_length) * znear);

        // float coc = abs(z * coc_scale + coc_bias);
        float coc = abs(aperture * (focal_length * (object_distance - plane_in_focus)) / (object_distance * (plane_in_focus - focal_length)));
        float max_coc = abs(aperture * (focal_length * (zfar - plane_in_focus)) / (object_distance * (plane_in_focus - focal_length)));
        coc = coc / max_coc;

        vec4 base_colour = textureGrad(global_textures[nonuniformEXT(textures.x)], vTexcoord0, vec2(coc, coc), vec2(coc, coc));

        frag_color = vec4( base_colour.rgb, 1.0 );
    } else {
        frag_color = texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0);
    }
}

#endif // FRAGMENT
