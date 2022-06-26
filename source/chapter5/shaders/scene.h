
// Scene common code

layout ( std140, set = MATERIAL_SET, binding = 0 ) uniform SceneConstants {
    mat4        view_projection;
    mat4  		inverse_view_projection;

    vec4        eye;
    vec4        light;

    float       light_range;
    float       light_intensity;
    uint        dither_texture_index;
    float       padding00;
};


float dither(vec2 screen_pixel_position, float value)
{
    float dither_value = texelFetch(global_textures[nonuniformEXT(dither_texture_index)], ivec2(int(screen_pixel_position.x) % 4, int(screen_pixel_position.y) % 4), 0).r;
    return value - dither_value;
}

