#pragma once

#include "foundation/platform.hpp"

#include "external/cglm/struct/mat4.h"

namespace raptor {

//
// Camera struct - can be both perspective and orthographic.
//
struct Camera {

    void                        init_perpective( f32 near_plane, f32 far_plane, f32 fov_y, f32 aspect_ratio );
    void                        init_orthographic( f32 near_plane, f32 far_plane, f32 viewport_width, f32 viewport_height, f32 zoom );

    void                        reset();

    void                        set_viewport_size( f32 width, f32 height );
    void                        set_zoom( f32 zoom );
    void                        set_aspect_ratio( f32 aspect_ratio );
    void                        set_fov_y( f32 fov_y );

    void                        update();
    void                        rotate( f32 delta_pitch, f32 delta_yaw );

    void                        calculate_projection_matrix();
    void                        calculate_view_projection();

    // Project/unproject
    vec3s                       unproject( const vec3s& screen_coordinates );

    // Unproject by inverting the y of the screen coordinate.
    vec3s                       unproject_inverted_y( const vec3s& screen_coordinates );

    void                        get_projection_ortho_2d( mat4& out_matrix );

    static void                 yaw_pitch_from_direction( const vec3s& direction, f32 & yaw, f32& pitch );

    mat4s                       view;
    mat4s                       projection;
    mat4s                       view_projection;

    vec3s                       position;
    vec3s                       right;
    vec3s                       direction;
    vec3s                       up;

    f32                         yaw;
    f32                         pitch;

    f32                         near_plane;
    f32                         far_plane;

    f32                         field_of_view_y;
    f32                         aspect_ratio;

    f32                         zoom;
    f32                         viewport_width;
    f32                         viewport_height;

    bool                        perspective;
    bool                        update_projection;

}; // struct Camera

} // namespace raptor
