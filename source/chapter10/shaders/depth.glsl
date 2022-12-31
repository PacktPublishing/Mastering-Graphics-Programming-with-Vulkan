

#if defined(VERTEX_DEPTH_PRE)

layout(location=0) in vec3 position;

void main() {
    MeshInstanceDraw mesh_draw = mesh_instance_draws[gl_InstanceIndex];
    gl_Position = view_projection * mesh_draw.model * vec4(position, 1.0);
}

#endif // VERTEX

#if defined (VERTEX_DEPTH_CUBEMAP)

#extension GL_EXT_multiview : require

layout ( std140, set = 2, binding = 0 ) readonly uniform Views {

    mat4    view_projections[6];
    vec4    camera_sphere;
};

layout(location=0) in vec3 position;

void main() {
    MeshInstanceDraw mesh_draw = mesh_instance_draws[gl_InstanceIndex];
    gl_Position = view_projections[gl_ViewIndex] * mesh_draw.model * vec4(position, 1.0);
}

#endif // VERTEX_DEPTH_CUBEMAP


#if defined (FRAGMENT_DEPTH_PRE_SKINNING)

layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in flat uint mesh_draw_index;

void main() {

    MeshDraw mesh_draw = mesh_draws[mesh_draw_index];
    uint flags = mesh_draw.flags;
    uvec4 textures = mesh_draw.textures;

    float texture_alpha = texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0).a;

    bool useAlphaMask = (flags & DrawFlags_AlphaMask) != 0;
    if (useAlphaMask && texture_alpha < mesh_draw.alpha_cutoff) {
        discard;
    }

    bool use_alpha_dither = (flags & DrawFlags_AlphaDither) != 0;
    if ( use_alpha_dither ) {
        float dithered_alpha = dither(gl_FragCoord.xy, texture_alpha);
    	if (dithered_alpha < 0.001f) {
            discard;
        }
    }
}

#endif // FRAGMENT