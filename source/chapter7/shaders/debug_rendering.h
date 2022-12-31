
#ifndef RAPTOR_GLSL_DEBUG_RENDERING_H
#define RAPTOR_GLSL_DEBUG_RENDERING_H

struct DebugLineVertex {
	vec3            position;
    uint            color;
};

struct DrawCommand {
    uint            vertex_count;
    uint            instance_count;
    uint            first_vertex;
    uint            first_instance;
};

layout(set = MATERIAL_SET, binding = 20) buffer DebugLines
{
    DebugLineVertex debug_line_vertices[];
};

layout(set = MATERIAL_SET, binding = 21) buffer DebugLinesCount
{
    uint            debug_line_3d_count;
    uint            debug_line_2d_count;
    uint            frame_index;
    uint            pad002;
};

layout(set = MATERIAL_SET, binding = 22) buffer DebugLineCommands
{
    DrawCommand     debug_draw_commands;
    DrawCommand     debug_draw_commands_2d;
};

const uint line_2d_offset = 1000;
const uint k_max_lines = 640000;


// Utility methods ///////////////////////////////////////////////////////
uint vec4_to_rgba( vec4 color ) {
    return (uint(color.r * 255.f) | (uint(color.g * 255.f) << 8) | 
           (uint(color.b * 255.f) << 16) | ((uint(color.a * 255.f) << 24)));
}

vec4 unpack_color_rgba( uint color ) {
    return vec4( ( color & 0xffu ) / 255.f,
                 ( ( color >> 8u ) & 0xffu ) / 255.f,
                 ( ( color >> 16u ) & 0xffu ) / 255.f,
                 ( ( color >> 24u ) & 0xffu ) / 255.f );
}

vec4 unpack_color_abgr( uint color ) {
    return vec4( ( ( color >> 24u ) & 0xffu ) / 255.f,
                 ( ( color >> 16u ) & 0xffu ) / 255.f,
                 ( ( color >> 8u ) & 0xffu ) / 255.f,
                 ( color & 0xffu ) / 255.f );
}

// Draw methods //////////////////////////////////////////////////////////
void debug_draw_line_coloru( vec3 start, vec3 end, uint start_color, uint end_color ) {

    if ( debug_line_3d_count >= k_max_lines ) {
        return;
    }

    uint line_offset = atomicAdd( debug_line_3d_count, 2 );

    debug_line_vertices[line_offset].position = start;
    debug_line_vertices[line_offset].color = start_color;

    debug_line_vertices[line_offset + 1].position = end;
    debug_line_vertices[line_offset + 1].color = end_color;
}

void debug_draw_line( vec3 start, vec3 end, vec4 start_color, vec4 end_color ) {

    debug_draw_line_coloru( start, end, vec4_to_rgba( start_color ), vec4_to_rgba( end_color ) );
}

void debug_draw_box( vec3 min, vec3 max, vec4 color ) {

    const float x0 = min.x;
    const float y0 = min.y;
    const float z0 = min.z;
    const float x1 = max.x;
    const float y1 = max.y;
    const float z1 = max.z;

    uint color_uint = vec4_to_rgba(color);

    debug_draw_line_coloru( vec3( x0, y0, z0 ), vec3( x0, y1, z0 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x0, y1, z0 ), vec3( x1, y1, z0 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x1, y1, z0 ), vec3( x1, y0, z0 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x1, y0, z0 ), vec3( x0, y0, z0 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x0, y0, z0 ), vec3( x0, y0, z1 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x0, y1, z0 ), vec3( x0, y1, z1 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x1, y1, z0 ), vec3( x1, y1, z1 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x1, y0, z0 ), vec3( x1, y0, z1 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x0, y0, z1 ), vec3( x0, y1, z1 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x0, y1, z1 ), vec3( x1, y1, z1 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x1, y1, z1 ), vec3( x1, y0, z1 ), color_uint, color_uint );
    debug_draw_line_coloru( vec3( x1, y0, z1 ), vec3( x0, y0, z1 ), color_uint, color_uint );
}

// 2D methods ////////////////////////////////////////////////////////////
void debug_draw_line_2d_coloru( vec2 start, vec2 end, uint start_color, uint end_color ) {

    if ( debug_line_2d_count >= k_max_lines ) {
        return;
    }

    uint frame_offset = line_2d_offset;//line_2d_offset * 2 * frame_index;
    uint line_offset = atomicAdd( debug_line_2d_count, 2 ) + frame_offset;

    debug_line_vertices[line_offset].position = vec3(start.xy, 0);
    debug_line_vertices[line_offset].color = start_color;

    debug_line_vertices[line_offset + 1].position = vec3(end.xy, 0);
    debug_line_vertices[line_offset + 1].color = end_color;
}

void debug_draw_2d_line( vec2 start, vec2 end, vec4 start_color, vec4 end_color ) {

    debug_draw_line_2d_coloru( start, end, vec4_to_rgba( start_color ), vec4_to_rgba( end_color ) );
}

void debug_draw_2d_box( vec2 min, vec2 max, vec4 color ) {

    uint color_uint = vec4_to_rgba(color);

    debug_draw_line_2d_coloru( vec2(min.x, min.y), vec2(max.x, min.y), color_uint, color_uint );
    debug_draw_line_2d_coloru( vec2(max.x, min.y), vec2(max.x, max.y), color_uint, color_uint );
    debug_draw_line_2d_coloru( vec2(max.x, max.y), vec2(min.x, max.y), color_uint, color_uint );
    debug_draw_line_2d_coloru( vec2(min.x, max.y), vec2(min.x, min.y), color_uint, color_uint );
}


#endif // RAPTOR_GLSL_DEBUG_RENDERING_H