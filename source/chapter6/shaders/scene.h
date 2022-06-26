
#ifndef RAPTOR_GLSL_SCENE_H
#define RAPTOR_GLSL_SCENE_H

// Scene common code

layout ( std140, set = MATERIAL_SET, binding = 0 ) uniform SceneConstants {
    mat4        view_projection;
    mat4        view_projection_debug;
    mat4  		inverse_view_projection;
    mat4        world_to_camera;
    mat4        world_to_camera_debug;
    mat4        previous_view_projection;

    vec4        eye;
    vec4        eye_debug;
    vec4        light;

    float       light_range;
    float       light_intensity;
    uint        dither_texture_index;
    float       z_near;

    float       z_far;
    float       projection_00;
    float       projection_11;
    uint        frustum_cull_meshes;

    uint        frustum_cull_meshlets;
    uint        occlusion_cull_meshes;
    uint        occlusion_cull_meshlets;
    uint        freeze_occlusion_camera;

    vec2        resolution;
    float       aspect_ratio;
    float       pad0001;

    vec4        frustum_planes[6];
};


float dither(vec2 screen_pixel_position, float value)
{
    float dither_value = texelFetch(global_textures[nonuniformEXT(dither_texture_index)], ivec2(int(screen_pixel_position.x) % 4, int(screen_pixel_position.y) % 4), 0).r;
    return value - dither_value;
}

float linearize_depth(float depth) {
    // NOTE(marco): Vulkan depth is [0, 1]
    return z_near * z_far / (z_far + depth * (z_near - z_far));
}

#endif // RAPTOR_GLSL_SCENE_H
