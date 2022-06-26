
uint DrawFlags_AlphaMask = 1 << 0;

layout ( std140, set = MATERIAL_SET, binding = 1 ) uniform Mesh {

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