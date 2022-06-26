

#if defined(COMPUTE_GPU_CULLING)


layout(set = MATERIAL_SET, binding = 1) writeonly buffer VisibleMeshInstances
{
	MeshDrawCommand draw_commands[];
};

layout(set = MATERIAL_SET, binding = 3) buffer CulledMeshInstances
{
	MeshDrawCommand draw_late_commands[];
};

layout(set = MATERIAL_SET, binding = 11) buffer VisibleMeshCount
{
	uint opaque_mesh_visible_count;
	uint opaque_mesh_culled_count;
	uint transparent_mesh_visible_count;
	uint transparent_mesh_culled_count;

	uint total_count;
	uint depth_pyramid_texture_index;
	uint late_flag;
};

layout(set = MATERIAL_SET, binding = 13) buffer EarlyVisibleMeshCount
{
	uint early_opaque_mesh_visible_count;
	uint early_opaque_mesh_culled_count;
	uint early_transparent_mesh_visible_count;
	uint early_transparent_mesh_culled_count;

	uint early_total_count;
	uint early_depth_pyramid_texture_index;
	uint early_late_flag;
};

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
bool project_sphere(vec3 C, float r, float znear, float P00, float P11, out vec4 aabb) {
	if (-C.z - r < znear)
		return false;

	vec2 cx = vec2(C.x, -C.z);
	vec2 vx = vec2(sqrt(dot(cx, cx) - r * r), r);
	vec2 minx = mat2(vx.x, vx.y, -vx.y, vx.x) * cx;
	vec2 maxx = mat2(vx.x, -vx.y, vx.y, vx.x) * cx;

	vec2 cy = -C.yz;
	vec2 vy = vec2(sqrt(dot(cy, cy) - r * r), r);
	vec2 miny = mat2(vy.x, vy.y, -vy.y, vy.x) * cy;
	vec2 maxy = mat2(vy.x, -vy.y, vy.y, vy.x) * cy;

	aabb = vec4(minx.x / minx.y * P00, miny.x / miny.y * P11, maxx.x / maxx.y * P00, maxy.x / maxy.y * P11);
	aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) + vec4(0.5f); // clip space -> uv space

	return true;
}


layout (local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main() {

	uint mesh_instance_index = gl_GlobalInvocationID.x;
	uint count = total_count;
	if ( late_flag == 1 ) {
		count = early_opaque_mesh_culled_count;
	}

	// TODO:
	if ( mesh_instance_index == 0 && late_flag == 0 ) {
		debug_draw_line( vec3(-1,-1,-1), vec3(1,1,1), vec4(0,0,1,1), vec4(1,0,0,1));
		//debug_draw_box(vec3(-1,-1,-1), vec3(1,1,1), vec4(0,0,1,1));
	}

	if (mesh_instance_index < count) {
		if (late_flag == 1) {
			mesh_instance_index = draw_late_commands[mesh_instance_index].drawId;
		}
		uint mesh_draw_index = mesh_instance_draws[mesh_instance_index].mesh_draw_index;

		MeshDraw mesh_draw = mesh_draws[mesh_draw_index];

		vec4 bounding_sphere = mesh_bounds[mesh_draw_index];
		mat4 model = mesh_instance_draws[mesh_instance_index].model;

		// Transform bounding sphere to view space.
		vec4 world_bounding_center = model * vec4(bounding_sphere.xyz, 1);
		vec4 view_bounding_center = freeze_occlusion_camera == 0 ? world_to_camera * world_bounding_center : world_to_camera_debug * world_bounding_center;

    	float scale = length( model[0] );
    	float radius = bounding_sphere.w * scale * 1.1;	// Artificially inflate bounding sphere.

    	bool frustum_visible = true;
	    for ( uint i = 0; i < 6; ++i ) {
	        frustum_visible = frustum_visible && (dot( frustum_planes[i], view_bounding_center) > -radius);
	    }

	    frustum_visible = frustum_visible || (frustum_cull_meshes == 0);

	    bool occlusion_visible = true;
	    if ( frustum_visible ) {
	    	vec4 aabb;
	    	if ( project_sphere(view_bounding_center.xyz, radius, z_near, projection_00, projection_11, aabb ) ) {
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

				vec3 dir = normalize(eye.xyz - world_bounding_center.xyz);
    			vec4 sceen_space_center_last = late_flag == 0 ? previous_view_projection * vec4(world_bounding_center.xyz + dir * radius, 1.0) : view_projection * vec4(world_bounding_center.xyz + dir * radius, 1.0);

				float depth_sphere = sceen_space_center_last.z / sceen_space_center_last.w;

				occlusion_visible = (depth_sphere <= depth);
	    	}
	    }

		occlusion_visible = occlusion_visible || (occlusion_cull_meshes == 0);

	    uint flags = mesh_draw.flags;
	    if ( frustum_visible && occlusion_visible ) {
	    	// Add opaque draws
			if ( (flags & (DrawFlags_AlphaMask | DrawFlags_Transparent)) == 0 ) {
				uint draw_index = atomicAdd( opaque_mesh_visible_count, 1 );

				draw_commands[draw_index].drawId = mesh_instance_index;
				draw_commands[draw_index].indexCount = 0;
				draw_commands[draw_index].instanceCount = 1;
				draw_commands[draw_index].firstIndex = 0;
				draw_commands[draw_index].vertexOffset = mesh_draw.vertexOffset;
				draw_commands[draw_index].firstInstance = 0;
				draw_commands[draw_index].taskCount = (mesh_draw.meshlet_count + 31) / 32;
				draw_commands[draw_index].firstTask = mesh_draw.meshlet_offset / 32;
			}
			else {
				// Transparent draws are written after total_count commands in the same buffer.
				uint draw_index = atomicAdd( transparent_mesh_visible_count, 1 ) + total_count;

				draw_commands[draw_index].drawId = mesh_instance_index;
				draw_commands[draw_index].indexCount = 0;
				draw_commands[draw_index].instanceCount = 1;
				draw_commands[draw_index].firstIndex = 0;
				draw_commands[draw_index].vertexOffset = mesh_draw.vertexOffset;
				draw_commands[draw_index].firstInstance = 0;
				draw_commands[draw_index].taskCount = (mesh_draw.meshlet_count + 31) / 32;
				draw_commands[draw_index].firstTask = mesh_draw.meshlet_offset / 32;
			}
	    } else if ( late_flag == 0 ) {
			// Add culled object for re-test
			if ( (flags & (DrawFlags_AlphaMask | DrawFlags_Transparent)) == 0 ) {
				uint draw_index = atomicAdd( opaque_mesh_culled_count, 1 );

				draw_late_commands[draw_index].drawId = mesh_instance_index;
				draw_late_commands[draw_index].taskCount = (mesh_draw.meshlet_count + 31) / 32;
				draw_late_commands[draw_index].firstTask = mesh_draw.meshlet_offset / 32;
			}
		}

	}
}

#endif
