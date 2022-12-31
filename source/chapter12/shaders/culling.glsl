

#if defined(COMPUTE_GPU_MESH_CULLING)


layout(set = MATERIAL_SET, binding = 1) buffer VisibleMeshInstances
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
	uint meshlet_index_count;

	uint dispatch_task_x;
	uint dispatch_task_y;
	uint dispatch_task_z;
	uint pad001_vmc;
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
	uint early_meshlet_index_count;

	uint early_dispatch_task_x;
	uint early_dispatch_task_y;
	uint early_dispatch_task_z;
	uint pad001_emc;
};

layout (local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main() {

	uint mesh_instance_index = gl_GlobalInvocationID.x;
	uint count = total_count;
	if ( late_flag == 1 ) {
		count = early_opaque_mesh_culled_count;
	}

	// TODO: debug rendering test
	if ( mesh_instance_index == 0 && late_flag == 0 ) {
		//debug_draw_line( vec3(-1,-1,-1), vec3(1,1,1), vec4(0,0,1,1), vec4(1,0,0,1));
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
		vec4 view_bounding_center = freeze_occlusion_camera() ? world_to_camera * world_bounding_center : world_to_camera_debug * world_bounding_center;

    	float scale = length( model[0] );
    	float radius = bounding_sphere.w * scale * 1.1;	// Artificially inflate bounding sphere.

    	bool frustum_visible = true;
	    for ( uint i = 0; i < 6; ++i ) {
	        frustum_visible = frustum_visible && (dot( frustum_planes[i], view_bounding_center) > -radius);
	    }

	    frustum_visible = frustum_visible || disable_frustum_cull_meshes();

	    bool occlusion_visible = true;
	    if ( frustum_visible ) {

	    	vec3 camera_world_position = freeze_occlusion_camera() ? camera_position.xyz : camera_position_debug.xyz;
	    	mat4 culling_view_projection = early_late_flag == 0 ? previous_view_projection : view_projection;

	    	occlusion_visible = occlusion_cull( view_bounding_center.xyz, radius, z_near, projection_00, projection_11,
	    		                                depth_pyramid_texture_index, world_bounding_center.xyz, camera_world_position,
	    		                                culling_view_projection );
	    }

		occlusion_visible = occlusion_visible || disable_occlusion_cull_meshes();

	    uint flags = mesh_draw.flags;
	    if ( frustum_visible && occlusion_visible ) {
	    	// Add opaque draws
			if ( ((flags & (DrawFlags_AlphaMask | DrawFlags_Transparent)) == 0 ) ){
				uint draw_index = atomicAdd( opaque_mesh_visible_count, 1 );

				draw_commands[draw_index].drawId = mesh_instance_index;
				draw_commands[draw_index].indexCount = 0;
				draw_commands[draw_index].instanceCount = 1;
				draw_commands[draw_index].firstIndex = 0;
				draw_commands[draw_index].vertexOffset = mesh_draw.vertexOffset;
				draw_commands[draw_index].firstInstance = 0;

				uint task_count = (mesh_draw.meshlet_count + 31) / 32;
				draw_commands[draw_index].taskCount = task_count;
				draw_commands[draw_index].firstTask = mesh_draw.meshlet_offset / 32;

				// TODO: add optional flags for dispatch of task shaders emulation
				//atomicAdd( dispatch_task_x, task_count );

				draw_commands[draw_index].indexCount = mesh_draw.meshlet_index_count;
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

#endif // COMPUTE_GPU_MESH_CULLING
