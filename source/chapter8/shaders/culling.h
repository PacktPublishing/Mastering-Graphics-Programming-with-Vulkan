
#ifndef RAPTOR_GLSL_CULLING_H
#define RAPTOR_GLSL_CULLING_H

// NOTE(marco): as described in meshoptimizer.h
bool coneCull(vec3 center, float radius, vec3 cone_axis, float cone_cutoff, vec3 camera_position)
{
    return dot(center - camera_position, cone_axis) >= cone_cutoff * length(center - camera_position) + radius;
}

bool sphere_intersect( vec3 center_a, float radius_a, vec3 center_b, float radius_b ) {
	const vec3 v = center_b - center_a;
	const float total_radius = radius_a + radius_b;

	return dot(v, v) < total_radius;
}

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool project_sphere(vec3 C, float r, float znear, float P00, float P11, out vec4 aabb) {
	if (C.z - r < znear)
		return false;

	vec2 cx = vec2(C.x, C.z);
	vec2 vx = vec2(sqrt(dot(cx, cx) - r * r), r);
	vec2 minx = mat2(vx.x, vx.y, -vx.y, vx.x) * cx;
	vec2 maxx = mat2(vx.x, -vx.y, vx.y, vx.x) * cx;

	vec2 cy = vec2(-C.y, C.z);
	vec2 vy = vec2(sqrt(dot(cy, cy) - r * r), r);
	vec2 miny = mat2(vy.x, vy.y, -vy.y, vy.x) * cy;
	vec2 maxy = mat2(vy.x, -vy.y, vy.y, vy.x) * cy;

	aabb = vec4(minx.x / minx.y * P00, miny.x / miny.y * P11, maxx.x / maxx.y * P00, maxy.x / maxy.y * P11);
	aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) + vec4(0.5f); // clip space -> uv space

	return true;
}

bool occlusion_cull( vec3 view_bounding_center, float radius, float z_near, float projection_00, float projection_11,
	                 uint depth_pyramid_texture_index, vec3 world_bounding_center, vec3 camera_world_position,
	                 mat4 culling_view_projection ) {

	vec4 aabb;
	bool occlusion_visible = true;
	if ( project_sphere(view_bounding_center, radius, z_near, projection_00, projection_11, aabb ) ) {
		// TODO: improve
		ivec2 depth_pyramid_size = textureSize(global_textures[nonuniformEXT(depth_pyramid_texture_index)], 0);
		float width = (aabb.z - aabb.x) * depth_pyramid_size.x;
		float height = (aabb.w - aabb.y) * depth_pyramid_size.y;

		float level = floor(log2(max(width, height)));

		// Sampler is set up to do max reduction, so this computes the minimum depth of a 2x2 texel quad
		vec2 uv = (aabb.xy + aabb.zw) * 0.5;
    	uv.y = 1 - uv.y;
		
		float depth = textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], uv, level).r;
		// Sample also 4 corners
    	depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.x, 1.0f - aabb.y), level).r);
    	depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.z, 1.0f - aabb.w), level).r);
    	depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.x, 1.0f - aabb.w), level).r);
    	depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.z, 1.0f - aabb.y), level).r);

    	//vec3 camera_world_position = freeze_occlusion_camera() ? camera_position.xyz : camera_position_debug.xyz;
		vec3 dir = normalize(camera_world_position - world_bounding_center.xyz);
		//vec4 sceen_space_center_last = early_late_flag == 0 ? previous_view_projection * vec4(world_bounding_center + dir * radius, 1.0) : view_projection * vec4(world_bounding_center + dir * radius, 1.0);
		vec4 sceen_space_center_last = culling_view_projection * vec4(world_bounding_center + dir * radius, 1.0);

		float depth_sphere = sceen_space_center_last.z / sceen_space_center_last.w;

		occlusion_visible = (depth_sphere <= depth);

		// Debug: write world space calculated bounds.
        // vec2 uv_center = (aabb.xy + aabb.zw) * 0.5;
        // vec4 sspos = vec4(uv_center * 2 - 1, depth, 1);
        // vec4 aabb_world = inverse_view_projection * sspos;
        // aabb_world.xyz /= aabb_world.w;

        //debug_draw_box( world_center.xyz - vec3(radius), world_center.xyz + vec3(radius), vec4(1,1,1,0.5));
        //debug_draw_box( aabb_world.xyz - vec3(radius * 1.1), aabb_world.xyz + vec3(radius * 1.1), vec4(0,depth_sphere - depth,1,0.5));

		//debug_draw_2d_box(aabb.xy * 2.0 - 1, aabb.zw * 2 - 1, occlusion_visible ? vec4(0,1,0,1) : vec4(1,0,0,1));
	}

	return occlusion_visible;
}

uint get_cube_face_mask( vec3 cube_map_pos, vec3 aabb_min, vec3 aabb_max ) {

    vec3 plane_normals[] = { vec3(-1, 1, 0), vec3(1, 1, 0), vec3(1, 0, 1), vec3(1, 0, -1), vec3(0, 1, 1), vec3(0, -1, 1) };
    vec3 abs_plane_normals[] = { vec3(1, 1, 0), vec3(1, 1, 0), vec3(1, 0, 1), vec3(1, 0, 1), vec3(0, 1, 1), vec3(0, 1, 1) };

    vec3 aabb_center = (aabb_min + aabb_max) * 0.5f;

    vec3 center = aabb_center - cube_map_pos;
    vec3 extents = (aabb_max - aabb_min) * 0.5f;

    bool rp[ 6 ];
    bool rn[ 6 ];

    for ( uint  i = 0; i < 6; ++i ) {
        float dist = dot( center, plane_normals[ i ] );
        float radius = dot( extents, abs_plane_normals[ i ] );

        rp[ i ] = dist > -radius;
        rn[ i ] = dist < radius;
    }

    uint fpx = (rn[ 0 ] && rp[ 1 ] && rp[ 2 ] && rp[ 3 ] && aabb_max.x > cube_map_pos.x) ? 1 : 0;
    uint fnx = (rp[ 0 ] && rn[ 1 ] && rn[ 2 ] && rn[ 3 ] && aabb_min.x < cube_map_pos.x) ? 1 : 0;
    uint fpy = (rp[ 0 ] && rp[ 1 ] && rp[ 4 ] && rn[ 5 ] && aabb_max.y > cube_map_pos.y) ? 1 : 0;
    uint fny = (rn[ 0 ] && rn[ 1 ] && rn[ 4 ] && rp[ 5 ] && aabb_min.y < cube_map_pos.y) ? 1 : 0;
    uint fpz = (rp[ 2 ] && rn[ 3 ] && rp[ 4 ] && rp[ 5 ] && aabb_max.z > cube_map_pos.z) ? 1 : 0;
    uint fnz = (rn[ 2 ] && rp[ 3 ] && rn[ 4 ] && rn[ 5 ] && aabb_min.z < cube_map_pos.z) ? 1 : 0;

    return fpx | ( fnx << 1 ) | ( fpy << 2 ) | ( fny << 3 ) | ( fpz << 4 ) | ( fnz << 5 );
}

#endif // RAPTOR_GLSL_CULLING_H