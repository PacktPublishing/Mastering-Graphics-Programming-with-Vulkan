
uint DrawFlags_AlphaMask    = 1 << 0;
uint DrawFlags_Phong        = 1 << 3;
uint DrawFlags_HasNormals   = 1 << 4;
uint DrawFlags_TexCoords    = 1 << 5;
uint DrawFlags_HasTangents  = 1 << 6;
uint DrawFlags_HasJoints    = 1 << 7;
uint DrawFlags_HasWeights   = 1 << 8;
uint DrawFlags_AlphaDither  = 1 << 9;

layout ( std140, set = MATERIAL_SET, binding = 2 ) uniform Mesh {

    mat4        model;
    mat4        model_inverse;

    // x = diffuse index, y = roughness index, z = normal index, w = occlusion index.
    // Occlusion and roughness are encoded in the same texture
    uvec4       textures;
    vec4        emissive;
    vec4        base_color_factor;
    vec4        metallic_roughness_occlusion_factor;

    uint        flags;
    float       alpha_cutoff;
    float       mesh_padding00;
    float       mesh_padding01;

    vec4        diffuse;
    vec3        specular_colour;
    float       specular_exp;
    vec4        ambient_colour;
};
