#include "foundation/camera.hpp"

#include "external/cglm/struct/cam.h"
#include "external/cglm/struct/affine.h"
#include "external/cglm/struct/quat.h"
#include "external/cglm/struct/project.h"

namespace raptor {


// Camera ///////////////////////////////////////////////////////////////////////

void Camera::init_perpective( f32 near_plane_, f32 far_plane_, f32 fov_y, f32 aspect_ratio_ ) {
    perspective = true;

    near_plane = near_plane_;
    far_plane = far_plane_;
    field_of_view_y = fov_y;
    aspect_ratio = aspect_ratio_;

    reset();
}

void Camera::init_orthographic( f32 near_plane_, f32 far_plane_, f32 viewport_width_, f32 viewport_height_, f32 zoom_ ) {
    perspective = false;

    near_plane = near_plane_;
    far_plane = far_plane_;

    viewport_width = viewport_width_;
    viewport_height = viewport_height_;
    zoom = zoom_;

    reset();
}

void Camera::reset() {
    position = glms_vec3_zero();
    yaw = 0;
    pitch = 0;
    view = glms_mat4_identity();
    projection = glms_mat4_identity();

    update_projection = true;
}

void Camera::set_viewport_size( f32 width_, f32 height_ ) {
    viewport_width = width_;
    viewport_height = height_;

    update_projection = true;
}

void Camera::set_zoom( f32 zoom_ ) {
    zoom = zoom_;

    update_projection = true;
}

void Camera::set_aspect_ratio( f32 aspect_ratio_ ) {
    aspect_ratio = aspect_ratio_;

    update_projection = true;
}

void Camera::set_fov_y( f32 fov_y_ ) {
    field_of_view_y = fov_y_;

    update_projection = true;
}

void Camera::update() {

    // Left for reference.
    // Calculate rotation from yaw and pitch
    /*direction.x = sinf( ( yaw ) ) * cosf( ( pitch ) );
    direction.y = sinf( ( pitch ) );
    direction.z = cosf( ( yaw ) ) * cosf( ( pitch ) );
    direction = glms_vec3_normalize( direction );

    vec3s center = glms_vec3_sub( position, direction );
    vec3s cup{ 0,1,0 };

    right = glms_cross( cup, direction );
    up = glms_cross( direction, right );

    // Calculate view matrix
    view = glms_lookat( position, center, up );
    */

    // Quaternion based rotation.
    // https://stackoverflow.com/questions/49609654/quaternion-based-first-person-view-camera
    const versors pitch_rotation = glms_quat( pitch, 1, 0, 0 );
    const versors yaw_rotation = glms_quat( yaw, 0, 1, 0 );
    const versors rotation = glms_quat_normalize( glms_quat_mul( pitch_rotation, yaw_rotation ) );

    const mat4s translation = glms_translate_make( glms_vec3_scale( position, -1.f ) );
    view = glms_mat4_mul( glms_quat_mat4( rotation ), translation );

    // Update the vectors used for movement
    right = { view.m00, view.m10, view.m20 };
    up = { view.m01, view.m11, view.m21 };
    direction = { view.m02, view.m12, view.m22 };

    if ( update_projection ) {
        update_projection = false;

        calculate_projection_matrix();
    }

    // Calculate final view projection matrix
    calculate_view_projection();
}

void Camera::rotate( f32 delta_pitch, f32 delta_yaw ) {

    pitch += delta_pitch;
    yaw += delta_yaw;
}

void Camera::calculate_projection_matrix() {
    if ( perspective ) {
        projection = glms_perspective( glm_rad( field_of_view_y ), aspect_ratio, near_plane, far_plane );
    } else {
        projection = glms_ortho( zoom * -viewport_width / 2.f, zoom * viewport_width / 2.f, zoom * -viewport_height / 2.f, zoom * viewport_height / 2.f, near_plane, far_plane );
    }
}

void Camera::calculate_view_projection() {
    view_projection = glms_mat4_mul( projection, view );
}

vec3s Camera::unproject( const vec3s& screen_coordinates ) {
    return glms_unproject( screen_coordinates, view_projection, { 0, 0, viewport_width, viewport_height } );
}

vec3s Camera::unproject_inverted_y( const vec3s& screen_coordinates ) {
    const vec3s screen_coordinates_y_inv{ screen_coordinates.x, viewport_height - screen_coordinates.y, screen_coordinates.z };
    return unproject( screen_coordinates_y_inv );
}

void Camera::get_projection_ortho_2d( mat4& out_matrix ) {
    glm_ortho( 0, viewport_width * zoom, 0, viewport_height * zoom, -1.f, 1.f, out_matrix );
}

void Camera::yaw_pitch_from_direction( const vec3s& direction, f32& yaw, f32& pitch ) {

    yaw = glm_deg( atan2f( direction.z, direction.x ) );
    pitch = glm_deg( asinf( direction.y ) );
}

} // namespace raptor

