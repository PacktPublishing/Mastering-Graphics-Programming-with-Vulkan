
#ifndef RAPTOR_GLSL_MESH_H
#define RAPTOR_GLSL_MESH_H

uint DrawFlags_AlphaMask    = 1 << 0;
uint DrawFlags_DoubleSided  = 1 << 1;
uint DrawFlags_Transparent  = 1 << 2;
uint DrawFlags_Phong        = 1 << 3;
uint DrawFlags_HasNormals   = 1 << 4;
uint DrawFlags_TexCoords    = 1 << 5;
uint DrawFlags_HasTangents  = 1 << 6;
uint DrawFlags_HasJoints    = 1 << 7;
uint DrawFlags_HasWeights   = 1 << 8;
uint DrawFlags_AlphaDither  = 1 << 9;

struct MeshDraw {

    // x = diffuse index, y = roughness index, z = normal index, w = occlusion index.
    // Occlusion and roughness are encoded in the same texture
    uvec4       textures;
    vec4        emissive;
    vec4        base_color_factor;
    vec4        metallic_roughness_occlusion_factor;

    uint        flags;
    float       alpha_cutoff;
    uint        vertexOffset; // == meshes[meshIndex].vertexOffset, helps data locality in mesh shader
    uint        meshIndex;

    uint        meshlet_offset;
    uint        meshlet_count;
    uint        pad000;
    uint        pad001;

    vec4        diffuse;

    vec3        specular_colour;
    float       specular_exp;

    vec4        ambient_colour;
};

struct MeshInstanceDraw {
    mat4        model;
    mat4        model_inverse;

    uint        mesh_draw_index;
    uint        pad000;
    uint        pad001;
    uint        pad002;
};

struct MeshDrawCommand
{
    uint        drawId;

    // VkDrawIndexedIndirectCommand
    uint        indexCount;
    uint        instanceCount;
    uint        firstIndex;
    uint        vertexOffset;
    uint        firstInstance;

    // VkDrawMeshTasksIndirectCommandNV
    uint        taskCount;
    uint        firstTask;
};

layout ( std430, set = MATERIAL_SET, binding = 2 ) readonly buffer MeshDraws {

    MeshDraw    mesh_draws[];
};

layout ( std430, set = MATERIAL_SET, binding = 10 ) readonly buffer MeshInstanceDraws {

    MeshInstanceDraw mesh_instance_draws[];
};

layout ( std430, set = MATERIAL_SET, binding = 12 ) readonly buffer MeshBounds {

    vec4        mesh_bounds[];
};

#endif // RAPTOR_GLSL_MESH_H
