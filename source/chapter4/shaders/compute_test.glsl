#version 450

#extension GL_EXT_nonuniform_qualifier : enable

layout ( set = 0, binding = 10 ) uniform sampler2D textures[];
layout ( set = 0, binding = 11 ) writeonly uniform image3D images[];

layout(std140, set = 1, binding = 2) uniform locals {
    uint                albedo_id;
    uint                voxelized_id;
    uint                horizontal;
    uint                pad02;

    vec4                position;
    vec4                albedo_size;
    mat4                view_projection;
};

layout(std430, set = 1, binding = 1) buffer positions {
    vec4                nacifra[];
};

//layout( rgba8, set = 1, binding = 3 ) writeonly uniform image3D voxelized_image;

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
    ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);

    vec4 color = texelFetch( textures[albedo_id], pos.xy, 0 ).rgba;

    //points[pos.y * uint(albedo_size.x) + pos.x] = color;
    color += nacifra[voxelized_id];
    imageStore(images[0], pos, color);
}