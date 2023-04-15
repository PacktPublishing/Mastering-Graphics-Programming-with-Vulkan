#include "gpu_profiler.hpp"

#include "foundation/hash_map.hpp"
#include "foundation/numerics.hpp"
#include "foundation/color.hpp"

#include "graphics/renderer.hpp"

#include "external/imgui/imgui.h"
#include <cmath>
#include <stdio.h>

namespace raptor {

// GPU task names to colors
raptor::FlatHashMap<u64, u32>   name_to_color;

static u32      initial_frames_paused = 3;

void GPUProfiler::init( Allocator* allocator_, u32 max_frames_ ) {

    allocator = allocator_;
    max_frames = max_frames_;
    timestamps = ( GPUTimestamp* )ralloca( sizeof( GPUTimestamp ) * max_frames * 32, allocator );
    per_frame_active = ( u16* )ralloca( sizeof( u16 ) * max_frames, allocator );

    max_duration = 16.666f;
    current_frame = 0;
    min_time = max_time = average_time = 0.f;
    paused = false;

    memset( per_frame_active, 0, 2 * max_frames );

    name_to_color.init( allocator, 16 );
    name_to_color.set_default_value( u32_max );
}

void GPUProfiler::shutdown() {

    name_to_color.shutdown();

    rfree( timestamps, allocator );
    rfree( per_frame_active, allocator );
}

void GPUProfiler::update( GpuDevice& gpu ) {

    gpu.set_gpu_timestamps_enable( !paused );

    if ( initial_frames_paused ) {
        --initial_frames_paused;
        return;
    }

    if ( paused && !gpu.resized )
        return;

    u32 active_timestamps = gpu.get_gpu_timestamps( &timestamps[ 32 * current_frame ] );
    per_frame_active[ current_frame ] = ( u16 )active_timestamps;

    // Get colors
    for ( u32 i = 0; i < active_timestamps; ++i ) {
        GPUTimestamp& timestamp = timestamps[ 32 * current_frame + i ];

        u64 hashed_name = raptor::hash_calculate( timestamp.name );
        u32 color_index = name_to_color.get( hashed_name );
        // No entry found, add new color
        if ( color_index == u32_max ) {

            color_index = ( u32 )name_to_color.size;
            name_to_color.insert( hashed_name, color_index );
        }

        timestamp.color = raptor::Color::get_distinct_color( color_index );
    }

    current_frame = ( current_frame + 1 ) % max_frames;

    // Reset Min/Max/Average after few frames
    if ( current_frame == 0 ) {
        max_time = -FLT_MAX;
        min_time = FLT_MAX;
        average_time = 0.f;
    }
}

void GPUProfiler::imgui_draw() {
    if ( initial_frames_paused ) {
        return;
    }

    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        f32 widget_height = canvas_size.y - 100;

        f32 legend_width = 200;
        f32 graph_width = fabsf( canvas_size.x - legend_width );
        u32 rect_width = ceilu32( graph_width / max_frames );
        i32 rect_x = ceili32( graph_width - rect_width );

        f64 new_average = 0;

        ImGuiIO& io = ImGui::GetIO();

        static char buf[ 128 ];

        const ImVec2 mouse_pos = io.MousePos;

        i32 selected_frame = -1;

        // Draw time reference lines
        sprintf( buf, "%3.4fms", max_duration );
        draw_list->AddText( { cursor_pos.x, cursor_pos.y }, 0xff0000ff, buf );
        draw_list->AddLine( { cursor_pos.x + rect_width, cursor_pos.y }, { cursor_pos.x + graph_width, cursor_pos.y }, 0xff0000ff );

        sprintf( buf, "%3.4fms", max_duration / 2.f );
        draw_list->AddText( { cursor_pos.x, cursor_pos.y + widget_height / 2.f }, 0xff00ffff, buf );
        draw_list->AddLine( { cursor_pos.x + rect_width, cursor_pos.y + widget_height / 2.f }, { cursor_pos.x + graph_width, cursor_pos.y + widget_height / 2.f }, 0xff00ffff );

        // Draw Graph
        for ( u32 i = 0; i < max_frames; ++i ) {
            u32 frame_index = ( current_frame - 1 - i ) % max_frames;

            f32 frame_x = cursor_pos.x + rect_x;
            GPUTimestamp* frame_timestamps = &timestamps[ frame_index * 32 ];
            f32 frame_time = ( f32 )frame_timestamps[ 0 ].elapsed_ms;
            // Clamp values to not destroy the frame data
            frame_time = raptor::clamp( frame_time, 0.00001f, 1000.f );
            // Update timings
            new_average += frame_time;
            min_time = raptor::min( min_time, frame_time );
            max_time = raptor::max( max_time, frame_time );

            f32 rect_height = frame_time / max_duration * widget_height;
            //drawList->AddRectFilled( { frame_x, cursor_pos.y + rect_height }, { frame_x + rect_width, cursor_pos.y }, 0xffffffff );

            for ( u32 j = 0; j < per_frame_active[ frame_index ]; ++j ) {
                const GPUTimestamp& timestamp = frame_timestamps[ j ];

                /*if ( timestamp.depth != 1 ) {
                    continue;
                }*/

                rect_height = ( f32 )timestamp.elapsed_ms / max_duration * widget_height;
                draw_list->AddRectFilled( { frame_x, cursor_pos.y + widget_height - rect_height },
                    { frame_x + rect_width, cursor_pos.y + widget_height }, timestamp.color );
            }

            if ( mouse_pos.x >= frame_x && mouse_pos.x < frame_x + rect_width &&
                mouse_pos.y >= cursor_pos.y && mouse_pos.y < cursor_pos.y + widget_height ) {
                draw_list->AddRectFilled( { frame_x, cursor_pos.y + widget_height },
                    { frame_x + rect_width, cursor_pos.y }, 0x0fffffff );

                ImGui::SetTooltip( "(%u): %f", frame_index, frame_time );

                selected_frame = frame_index;
            }

            draw_list->AddLine( { frame_x, cursor_pos.y + widget_height }, { frame_x, cursor_pos.y }, 0x0fffffff );

            // Debug only
            /*static char buf[ 32 ];
            sprintf( buf, "%u", frame_index );
            draw_list->AddText( { frame_x, cursor_pos.y + widget_height - 64 }, 0xffffffff, buf );

            sprintf( buf, "%u", frame_timestamps[0].frame_index );
            drawList->AddText( { frame_x, cursor_pos.y + widget_height - 32 }, 0xffffffff, buf );*/

            rect_x -= rect_width;
        }

        average_time = ( f32 )new_average / max_frames;

        // Draw legend
        ImGui::SetCursorPosX( cursor_pos.x + graph_width );
        // Default to last frame if nothing is selected.
        selected_frame = selected_frame == -1 ? ( current_frame - 1 ) % max_frames : selected_frame;
        if ( selected_frame >= 0 ) {
            GPUTimestamp* frame_timestamps = &timestamps[ selected_frame * 32 ];

            f32 x = cursor_pos.x + graph_width;
            f32 y = cursor_pos.y;

            for ( u32 j = 0; j < per_frame_active[ selected_frame ]; ++j ) {
                const GPUTimestamp& timestamp = frame_timestamps[ j ];

                /*if ( timestamp.depth != 1 ) {
                    continue;
                }*/

                draw_list->AddRectFilled( { x, y },
                    { x + 8, y + 8 }, timestamp.color );

                sprintf( buf, "(%d)-%s %2.4f", timestamp.depth, timestamp.name, timestamp.elapsed_ms );
                draw_list->AddText( { x + 12, y }, 0xffffffff, buf );

                y += 16;
            }
        }

        ImGui::Dummy( { canvas_size.x, widget_height } );
    }

    ImGui::SetNextItemWidth( 100.f );
    ImGui::LabelText( "", "Max %3.4fms", max_time );
    ImGui::SameLine();
    ImGui::SetNextItemWidth( 100.f );
    ImGui::LabelText( "", "Min %3.4fms", min_time );
    ImGui::SameLine();
    ImGui::LabelText( "", "Ave %3.4fms", average_time );

    ImGui::Separator();
    ImGui::Checkbox( "Pause", &paused );

    static const char* items[] = { "200ms", "100ms", "66ms", "33ms", "16ms", "8ms", "4ms" };
    static const float max_durations[] = { 200.f, 100.f, 66.f, 33.f, 16.f, 8.f, 4.f };

    static int max_duration_index = 4;
    if ( ImGui::Combo( "Graph Max", &max_duration_index, items, IM_ARRAYSIZE( items ) ) ) {
        max_duration = max_durations[ max_duration_index ];
    }
}

} // namespace raptor
