
#ifndef RAPTOR_GLSL_MESHLET_H
#define RAPTOR_GLSL_MESHLET_H

#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require

#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot: require

#if defined (TASK) || defined(MESH)

// Needed extension for mesh and task shaders.
#extension GL_NV_mesh_shader: require

#endif // TASK || MESH

#define DEBUG 0
//#define PER_MESHLET_INDEX_WRITE 1

// Common data
struct VertexExtraData
{
    uint8_t     nx, ny, nz, nw; // normal
    uint8_t     tx, ty, tz, tw; // tangent
    float16_t   tu, tv;         // tex coords
    float       padding;
};

struct VertexPosition
{
    vec3    v;
    float   padding;
};

struct Meshlet
{
    vec3    center;
    float   radius;

    int8_t  cone_axis[3];
    int8_t  cone_cutoff;

    uint    data_offset;
    uint    mesh_index;
    uint8_t vertex_count;
    uint8_t triangle_count;
};

#endif // RAPTOR_GLSL_MESHLET_H
