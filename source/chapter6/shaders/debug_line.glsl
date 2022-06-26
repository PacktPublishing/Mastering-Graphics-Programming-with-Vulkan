
#if defined(VERTEX_DEBUG_LINE) || defined (FRAGMENT_DEBUG_LINE)
struct PhysicsVertex {
    vec3 position;
    vec3 start_position;
    vec3 previous_position;
    vec3 normal;
    uint joint_count;
    vec3 velocity;
    float mass;
    vec3 force; // TODO(marco): maybe we can remove this
    uint joints[ 12 ];
};

layout ( set = MATERIAL_SET, binding = 1 ) buffer PhysicsMesh {
    uint index_count;
    uint vertex_count;

    PhysicsVertex physics_vertices[];
};

#endif

#if defined(VERTEX_DEBUG_LINE)

void main() {
    uint vertex_index = 0;
    if ( gl_VertexIndex == 0 ) {
        vertex_index = gl_DrawIDARB;
    } else {
        vertex_index = physics_vertices[ gl_DrawIDARB ].joints[ gl_InstanceIndex ];
    }
    vec3 position = physics_vertices[ vertex_index ].position;
    gl_Position = view_projection * vec4(position, 1.0);
}

#endif // VERTEX


#if defined (FRAGMENT_DEBUG_LINE)

layout (location = 0) out vec4 colour;

void main() {

    colour = vec4( 0.7, 0.2, 0.2, 1.0 );
}

#endif // FRAGMENT

#if defined (COMPUTE_COMMANDS_FINALIZE)

layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main() {

    // Calculate instance count for indirect drawing.
    debug_draw_commands.vertex_count = 6;
    debug_draw_commands.instance_count = debug_line_3d_count / 2;
    debug_draw_commands.first_vertex = 0;
    debug_draw_commands.first_instance = 0;

    debug_draw_commands_2d.vertex_count = 6;
    debug_draw_commands_2d.instance_count = debug_line_2d_count / 2;
    debug_draw_commands_2d.first_vertex = 0;

    uint frame_offset = line_2d_offset;// * 2 * frame_index;
    debug_draw_commands_2d.first_instance = frame_offset;
}

#endif // COMPUTE_DEBUG_COMMANDS_FINALIZE

#if defined (VERTEX_DEBUG_LINE_GPU)

layout (location = 0) out vec4 Frag_Color;

// X and Y guide the expansion in clip space, Z guides the part of the segment the final vertex will be pointing to.
vec3 segment_quad[6] = { vec3(-0.5, -0.5, 0), vec3(0.5, -0.5, 1), vec3(0.5, 0.5, 1), vec3(-0.5, -0.5, 0), vec3(0.5, 0.5, 1), vec3(-0.5, 0.5, 0)};
vec2 uv[6] = { vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1)};

float expansion_direction[6] = { 1, -1, -1, 1, -1, 1 };

void main()
{
    // Based on "Antialised volumetric lines using shader based extrusion" by Sebastien Hillaire, OpenGL Insights Chapter 11.
    vec3 position = segment_quad[gl_VertexIndex % 6];
    const float width = 0.001;

    vec3 point_a = debug_line_vertices[gl_InstanceIndex * 2].position;
    vec3 point_b = debug_line_vertices[gl_InstanceIndex * 2 + 1].position;

    vec4 clip0 = view_projection * vec4(point_a, 1.0);
    vec4 clip1 = view_projection * vec4(point_b, 1.0);

    vec2 line_direction = width * normalize( (clip1.xy / clip1.w) - (clip0.xy / clip0.w) );
    if ( clip1.w * clip0.w  < 0 )
        line_direction = -line_direction;

    float segment_length = length(clip1.xy + clip0.xy);
    vec2 aspect_ratio = vec2( 1.0, resolution.x / resolution.y );
    gl_Position = mix(clip0, clip1, position.z);
    gl_Position.xy += line_direction.xy * position.xx * aspect_ratio;
    gl_Position.xy += line_direction.yx * position.yy * vec2(1, -1) * aspect_ratio;

    uint color_a = debug_line_vertices[gl_InstanceIndex * 2].color;
    uint color_b = debug_line_vertices[gl_InstanceIndex * 2 + 1].color;
    Frag_Color = mix(unpack_color_rgba(color_a), unpack_color_rgba(color_b), position.z);
}

#endif // VERTEX_DEBUG_LINE_GPU


#if defined (VERTEX_DEBUG_LINE_2D_GPU)

layout (location = 0) out vec4 Frag_Color;

// X and Y guide the expansion in clip space, Z guides the part of the segment the final vertex will be pointing to.
vec3 segment_quad[6] = { vec3(-0.5, -0.5, 0), vec3(0.5, -0.5, 1), vec3(0.5, 0.5, 1), vec3(-0.5, -0.5, 0), vec3(0.5, 0.5, 1), vec3(-0.5, 0.5, 0)};
vec2 uv[6] = { vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1)};

float expansion_direction[6] = { 1, -1, -1, 1, -1, 1 };

void main()
{
    // Based on "Antialised volumetric lines using shader based extrusion" by Sebastien Hillaire, OpenGL Insights Chapter 11.
    vec3 position = segment_quad[gl_VertexIndex % 6];
    const float width = 0.001;

    uint instance_index = gl_InstanceIndex;

    vec3 point_a = debug_line_vertices[instance_index * 2].position;
    vec3 point_b = debug_line_vertices[instance_index * 2 + 1].position;

    vec4 clip0 = vec4(point_a, 1.0);
    vec4 clip1 = vec4(point_b, 1.0);

    vec2 line_direction = width * normalize( (clip1.xy / clip1.w) - (clip0.xy / clip0.w) );
    if ( clip1.w * clip0.w  < 0 )
        line_direction = -line_direction;

    float segment_length = length(clip1.xy + clip0.xy);
    vec2 aspect_ratio = vec2( 1.0, resolution.x / resolution.y );
    gl_Position = mix(clip0, clip1, position.z);
    gl_Position.xy += line_direction.xy * position.xx * aspect_ratio;
    gl_Position.xy += line_direction.yx * position.yy * vec2(1, -1) * aspect_ratio;

    uint color_a = debug_line_vertices[instance_index * 2].color;
    uint color_b = debug_line_vertices[instance_index * 2 + 1].color;
    Frag_Color = mix(unpack_color_rgba(color_a), unpack_color_rgba(color_b), position.z);
}

#endif // VERTEX_DEBUG_LINE_GPU

#if defined (FRAGMENT_DEBUG_LINE_GPU) || defined (FRAGMENT_DEBUG_LINE_2D_GPU)

layout (location = 0) in vec4 Frag_Color;
        
layout (location = 0) out vec4 Out_Color;

void main()
{
    vec4 col = Frag_Color;
    
    Out_Color = col;
}

#endif // FRAGMENT_DEBUG_LINE_GPU
