


#if defined (TASK_DEPTH_PRE) || defined(TASK_GBUFFER_CULLING) || defined(TASK_TRANSPARENT_NO_CULL)

#define CULL 1

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(set = MATERIAL_SET, binding = 1) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

layout(set = MATERIAL_SET, binding = 6) buffer VisibleMeshInstances
{
    MeshDrawCommand draw_commands[];
};

layout(set = MATERIAL_SET, binding = 7) readonly buffer VisibleMeshCount
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
    uint meshlet_instances_count;
};

out taskNV block
{
    uint meshletIndices[32];
};

void main()
{
    uint ti = gl_LocalInvocationID.x;
    uint mgi = gl_WorkGroupID.x;

    uint mi = mgi * 32 + ti;

#if defined(TASK_TRANSPARENT_NO_CULL)
    uint mesh_instance_index = draw_commands[gl_DrawIDARB + total_count].drawId;
#else
    uint mesh_instance_index = draw_commands[gl_DrawIDARB].drawId;
#endif
    mat4 model = mesh_instance_draws[mesh_instance_index].model;

#if CULL
    vec4 world_center = model * vec4(meshlets[mi].center, 1);
    float scale = length( model[0] );
    float radius = meshlets[mi].radius * scale * 1.1;   // Artificially inflate bounding sphere.
    vec3 cone_axis = mat3( model ) * vec3(int(meshlets[mi].cone_axis[0]) / 127.0, int(meshlets[mi].cone_axis[1]) / 127.0, int(meshlets[mi].cone_axis[2]) / 127.0);
    float cone_cutoff = int(meshlets[mi].cone_cutoff) / 127.0;

    bool accept = false;
    vec4 view_center = vec4(0);
    // Backface culling and move meshlet in camera space
    if ( freeze_occlusion_camera == 0 ) {
        accept = !coneCull(world_center.xyz, radius, cone_axis, cone_cutoff, camera_position.xyz);
        view_center = world_to_camera * world_center;
    } else {
        accept = !coneCull(world_center.xyz, radius, cone_axis, cone_cutoff, camera_position_debug.xyz);
        view_center = world_to_camera_debug * world_center;
    }

    bool frustum_visible = true;
    for ( uint i = 0; i < 6; ++i ) {
        frustum_visible = frustum_visible && (dot( frustum_planes[i], view_center) > -radius);
    }

    frustum_visible = frustum_visible || (frustum_cull_meshlets == 0);

    bool occlusion_visible = true;
    if ( frustum_visible ) {

        // vec3 camera_world_position = freeze_occlusion_camera == 0 ? camera_position.xyz : camera_position_debug.xyz;
        // mat4 culling_view_projection = late_flag == 0 ? previous_view_projection : view_projection;

        // occlusion_visible = occlusion_cull( view_center.xyz, radius, z_near, projection_00, projection_11,
        //                                     depth_pyramid_texture_index, world_center.xyz, camera_world_position,
        //                                     culling_view_projection );

        vec4 aabb;
        if ( project_sphere(view_center.xyz, radius, z_near, projection_00, projection_11, aabb ) ) {
            // TODO: improve
            ivec2 depth_pyramid_size = textureSize(global_textures[nonuniformEXT(depth_pyramid_texture_index)], 0);
            float width = (aabb.z - aabb.x) * depth_pyramid_size.x;
            float height = (aabb.w - aabb.y) * depth_pyramid_size.y;

            float level = floor(log2(max(width, height)));

            // Sampler is set up to do max reduction, so this computes the minimum depth of a 2x2 texel quad
            vec2 uv = (aabb.xy + aabb.zw) * 0.5;
            uv.y = 1 - uv.y;

            // Depth is raw, 0..1 space.
            float depth = textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], uv, level).r;
            // Sample also 4 corners
            depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.x, 1.0f - aabb.y), level).r);
            depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.z, 1.0f - aabb.w), level).r);
            depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.x, 1.0f - aabb.w), level).r);
            depth = max(depth, textureLod(global_textures[nonuniformEXT(depth_pyramid_texture_index)], vec2(aabb.z, 1.0f - aabb.y), level).r);

            vec3 dir = normalize(camera_position.xyz - world_center.xyz);
            vec4 sceen_space_center_last = previous_view_projection * vec4(world_center.xyz + dir * radius, 1.0);

            float depth_sphere = sceen_space_center_last.z / sceen_space_center_last.w;

            occlusion_visible = (depth_sphere <= depth);

            // Debug: write world space calculated bounds.
            // vec2 uv_center = (aabb.xy + aabb.zw) * 0.5;
            // vec4 sspos = vec4(uv_center * 2 - 1, depth, 1);
            // vec4 aabb_world = inverse_view_projection * sspos;
            // aabb_world.xyz /= aabb_world.w;

            //debug_draw_box( world_center.xyz - vec3(radius), world_center.xyz + vec3(radius), vec4(1,1,1,0.5));
            //debug_draw_box( aabb_world.xyz - vec3(radius * 1.1), aabb_world.xyz + vec3(radius * 1.1), vec4(0,depth_sphere - depth,1,0.5));

            debug_draw_2d_box(aabb.xy * 2.0 - 1, aabb.zw * 2 - 1, occlusion_visible ? vec4(0,1,0,1) : vec4(1,0,0,1));
        }
    }

    occlusion_visible = occlusion_visible || (occlusion_cull_meshlets == 0);

    accept = accept && frustum_visible && occlusion_visible;

    uvec4 ballot = subgroupBallot(accept);

    uint index = subgroupBallotExclusiveBitCount(ballot);

    if (accept)
        meshletIndices[index] = mi;

    uint count = subgroupBallotBitCount(ballot);

    if (ti == 0)
        gl_TaskCountNV = count;
#else
    meshletIndices[ti] = mi;

    if (ti == 0)
        gl_TaskCountNV = 32;
#endif
}

#endif // TASK


#if defined(MESH_GBUFFER_CULLING) || defined(MESH_MESH) || defined(MESH_TRANSPARENT_NO_CULL)

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(set = MATERIAL_SET, binding = 1) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

layout(set = MATERIAL_SET, binding = 3) readonly buffer MeshletData
{
    uint meshletData[];
};

layout(set = MATERIAL_SET, binding = 4) readonly buffer VertexPositions
{
    VertexPosition vertex_positions[];
};

layout(set = MATERIAL_SET, binding = 5) readonly buffer VertexData
{
    VertexExtraData vertex_data[];
};

layout(set = MATERIAL_SET, binding = 6) readonly buffer VisibleMeshInstances
{
    MeshDrawCommand draw_commands[];
};

layout(set = MATERIAL_SET, binding = 7) readonly buffer VisibleMeshCount
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
    uint meshlet_instances_count;
};

in taskNV block
{
    uint meshletIndices[32];
};

layout (location = 0) out vec2 vTexcoord0[];
layout (location = 1) out vec4 vNormal_BiTanX[];
layout (location = 2) out vec4 vTangent_BiTanY[];
layout (location = 3) out vec4 vPosition_BiTanZ[];
layout (location = 4) out flat uint mesh_draw_index[];

#if DEBUG
layout (location = 5) out vec4 vColour[];
#endif

uint hash(uint a)
{
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);
   return a;
}

void main()
{
    uint ti = gl_LocalInvocationID.x;
    uint mi = meshletIndices[gl_WorkGroupID.x];

    MeshDraw mesh_draw = mesh_draws[ meshlets[mi].mesh_index ];

    uint vertex_count = uint(meshlets[mi].vertex_count);
    uint triangle_count = uint(meshlets[mi].triangle_count);
    uint indexCount = triangle_count * 3;

    uint data_offset = meshlets[mi].data_offset;
    uint vertexOffset = data_offset;
    uint indexOffset = data_offset + vertex_count;

    bool has_normals = (mesh_draw.flags & DrawFlags_HasNormals) != 0;
    bool has_tangents = (mesh_draw.flags & DrawFlags_HasTangents) != 0;

    float i8_inverse = 1.0 / 127.0;

#if defined(MESH_TRANSPARENT_NO_CULL)
    uint mesh_instance_index = draw_commands[gl_DrawIDARB + total_count].drawId;
#else
    uint mesh_instance_index = draw_commands[gl_DrawIDARB].drawId;
#endif

#if DEBUG
    uint mhash = hash(mi);
    vec3 mcolor = vec3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;
#endif

    mat4 model = mesh_instance_draws[mesh_instance_index].model;
    mat4 model_inverse = mesh_instance_draws[mesh_instance_index].model_inverse;

    // TODO: if we have meshlets with 62 or 63 vertices then we pay a small penalty for branch divergence here - we can instead redundantly xform the last vertex
    for (uint i = ti; i < vertex_count; i += 32)
    {
        uint vi = meshletData[vertexOffset + i];// + mesh_draw.vertexOffset;

        vec3 position = vec3(vertex_positions[vi].v.x, vertex_positions[vi].v.y, vertex_positions[vi].v.z);

        if ( has_normals ) {
            vec3 normal = vec3(int(vertex_data[vi].nx), int(vertex_data[vi].ny), int(vertex_data[vi].nz)) * i8_inverse - 1.0;
            vNormal_BiTanX[ i ].xyz = normalize( mat3(model_inverse) * normal );
        }

        if ( has_tangents ) {
            vec3 tangent = vec3(int(vertex_data[vi].tx), int(vertex_data[vi].ty), int(vertex_data[vi].tz)) * i8_inverse - 1.0;
            vTangent_BiTanY[ i ].xyz = normalize( mat3(model) * tangent.xyz );

            vec3 bitangent = cross( vNormal_BiTanX[ i ].xyz, tangent.xyz ) * ( int(vertex_data[vi].tw) * i8_inverse  - 1.0 );
            vNormal_BiTanX[ i ].w = bitangent.x;
            vTangent_BiTanY[ i ].w = bitangent.y;
            vPosition_BiTanZ[ i ].w = bitangent.z;
        }

        vTexcoord0[i] = vec2(vertex_data[vi].tu, vertex_data[vi].tv);

        gl_MeshVerticesNV[ i ].gl_Position = view_projection * (model * vec4(position, 1));

        vec4 worldPosition = model * vec4(position, 1.0);
        vPosition_BiTanZ[ i ].xyz = worldPosition.xyz / worldPosition.w;

        mesh_draw_index[ i ] = meshlets[mi].mesh_index;


#if DEBUG
        vColour[i] = vec4(mcolor, 1.0);
#endif
    }

    uint indexGroupCount = (indexCount + 3) / 4;

    for (uint i = ti; i < indexGroupCount; i += 32)
    {
        writePackedPrimitiveIndices4x8NV(i * 4, meshletData[indexOffset + i]);
    }

    if (ti == 0)
        gl_PrimitiveCountNV = uint(meshlets[mi].triangle_count);
}

#endif // MESH


#if defined(MESH_DEPTH_PRE)

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(set = MATERIAL_SET, binding = 1) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

layout(set = MATERIAL_SET, binding = 3) readonly buffer MeshletData
{
    uint meshletData[];
};

layout(set = MATERIAL_SET, binding = 4) readonly buffer VertexPositions
{
    VertexPosition vertex_positions[];
};

layout(set = MATERIAL_SET, binding = 5) readonly buffer VertexData
{
    VertexExtraData vertex_data[];
};

layout(set = MATERIAL_SET, binding = 6) readonly buffer VisibleMeshInstances
{
    MeshDrawCommand draw_commands[];
};

in taskNV block
{
    uint meshletIndices[32];
};

void main()
{
    uint ti = gl_LocalInvocationID.x;
    uint mi = meshletIndices[gl_WorkGroupID.x];

    MeshDraw mesh_draw = mesh_draws[ meshlets[mi].mesh_index ];

    uint vertex_count = uint(meshlets[mi].vertex_count);
    uint triangle_count = uint(meshlets[mi].triangle_count);
    uint indexCount = triangle_count * 3;

    uint data_offset = meshlets[mi].data_offset;
    uint vertexOffset = data_offset;
    uint indexOffset = data_offset + vertex_count;

    uint mesh_instance_index = draw_commands[gl_DrawIDARB].drawId;
    mat4 model = mesh_instance_draws[mesh_instance_index].model;

    // TODO: if we have meshlets with 62 or 63 vertices then we pay a small penalty for branch divergence here - we can instead redundantly xform the last vertex
    for (uint i = ti; i < vertex_count; i += 32)
    {
        uint vi = meshletData[vertexOffset + i];// + mesh_draw.vertexOffset;

        vec3 position = vec3(vertex_positions[vi].v.x, vertex_positions[vi].v.y, vertex_positions[vi].v.z);

        gl_MeshVerticesNV[ i ].gl_Position = view_projection * (model * vec4(position, 1));
    }

    uint indexGroupCount = (indexCount + 3) / 4;

    for (uint i = ti; i < indexGroupCount; i += 32)
    {
        writePackedPrimitiveIndices4x8NV(i * 4, meshletData[indexOffset + i]);
    }

    if (ti == 0)
        gl_PrimitiveCountNV = uint(meshlets[mi].triangle_count);
}

#endif // MESH

#if defined(FRAGMENT_GBUFFER_CULLING) || defined(FRAGMENT_MESH) || defined(FRAGMENT_EMULATION_GBUFFER_CULLING)

layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec4 vNormal_BiTanX;
layout (location = 2) in vec4 vTangent_BiTanY;
layout (location = 3) in vec4 vPosition_BiTanZ;
layout (location = 4) in flat uint mesh_draw_index;

#if DEBUG
layout (location = 5) in vec4 vColour;
#endif

layout (location = 0) out vec4 color_out;
layout (location = 1) out vec2 normal_out;
layout (location = 2) out vec4 occlusion_roughness_metalness_out;
layout (location = 3) out vec4 emissive_out;

void main() {
    MeshDraw mesh_draw = mesh_draws[mesh_draw_index];

    // Diffuse color
    vec4 base_colour = compute_diffuse_color( mesh_draw.base_color_factor, mesh_draw.textures.x, vTexcoord0 );

    const uint flags = mesh_draw.flags;

    apply_alpha_discards( flags, base_colour.a, mesh_draw.alpha_cutoff );

#if DEBUG
    color_out = vColour;
#else
    color_out = base_colour;
#endif

    // Geometric Normals
    vec3 world_position = vPosition_BiTanZ.xyz;

    vec3 normal = normalize(vNormal_BiTanX.xyz);
    vec3 tangent = normalize(vTangent_BiTanY.xyz);
    vec3 bitangent = normalize(vec3(vNormal_BiTanX.w, vTangent_BiTanY.w, vPosition_BiTanZ.w));

    calculate_geometric_TBN( normal, tangent, bitangent, vTexcoord0.xy, world_position, flags );

    normal = apply_pixel_normal( mesh_draw.textures.z, vTexcoord0.xy, normal, tangent, bitangent );

    normal_out.rg = octahedral_encode(normal);

    // PBR Parameters
    occlusion_roughness_metalness_out.rgb = calculate_pbr_parameters( mesh_draw.metallic_roughness_occlusion_factor.x, mesh_draw.metallic_roughness_occlusion_factor.y,
                                                                      mesh_draw.textures.y, mesh_draw.metallic_roughness_occlusion_factor.z, mesh_draw.textures.w, vTexcoord0.xy );

    emissive_out = vec4( calculate_emissive(mesh_draw.emissive.rgb, uint(mesh_draw.emissive.w), vTexcoord0.xy ), 1.0 );
}

#endif // FRAGMENT


#if defined(FRAGMENT_TRANSPARENT_NO_CULL)

layout (location = 0) in vec2 vTexcoord0;
layout (location = 1) in vec4 vNormal_BiTanX;
layout (location = 2) in vec4 vTangent_BiTanY;
layout (location = 3) in vec4 vPosition_BiTanZ;
layout (location = 4) in flat uint mesh_draw_index;

#if DEBUG
layout (location = 5) in vec4 vColour;
#endif

layout (location = 0) out vec4 color_out;

void main() {
    MeshDraw mesh_draw = mesh_draws[mesh_draw_index];
    uint flags = mesh_draw.flags;

    // Diffuse color
    vec4 base_colour = compute_diffuse_color_alpha( mesh_draw.base_color_factor, mesh_draw.textures.x, vTexcoord0 );

    apply_alpha_discards( flags, base_colour.a, mesh_draw.alpha_cutoff );

    vec3 world_position = vPosition_BiTanZ.xyz;
    vec3 normal = normalize(vNormal_BiTanX.xyz);
    vec3 tangent = normalize(vTangent_BiTanY.xyz);
    vec3 bitangent = normalize(vec3(vNormal_BiTanX.w, vTangent_BiTanY.w, vPosition_BiTanZ.w));

    calculate_geometric_TBN( normal, tangent, bitangent, vTexcoord0.xy, world_position, flags );
    // Pixel normals
    normal = apply_pixel_normal( mesh_draw.textures.z, vTexcoord0.xy, normal, tangent, bitangent );

    vec3 orm = calculate_pbr_parameters( mesh_draw.metallic_roughness_occlusion_factor.x, mesh_draw.metallic_roughness_occlusion_factor.y,
                                                                      mesh_draw.textures.y, mesh_draw.metallic_roughness_occlusion_factor.z, mesh_draw.textures.w, vTexcoord0.xy );

    vec3 emissive_colour = calculate_emissive(mesh_draw.emissive.rgb, uint(mesh_draw.emissive.w), vTexcoord0.xy );

#if DEBUG
    color_out = vColour;
#else
    color_out = calculate_lighting( base_colour, orm, normal, emissive_colour.rgb, world_position );
#endif
}

#endif // FRAGMENT_TRANSPARENT_NO_CULL

#if defined (COMPUTE_GENERATE_MESHLET_INDEX_BUFFER)

layout(set = MATERIAL_SET, binding = 1) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

layout(set = MATERIAL_SET, binding = 3) readonly buffer MeshletData
{
    uint meshlets_data[];
};

layout(set = MATERIAL_SET, binding = 6) buffer VisibleMeshInstances
{
    MeshDrawCommand draw_commands[];
};

layout(set = MATERIAL_SET, binding = 7) buffer VisibleMeshCount
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
    uint meshlet_instances_count;
};

layout(set = MATERIAL_SET, binding = 8) buffer VisibleMeshletIndexBuffer
{
    uvec4           indices[];
};

layout(set = MATERIAL_SET, binding = 9) readonly buffer MeshletInstances
{
    uvec2           meshlet_instances[];
};

layout(set = MATERIAL_SET, binding = 19) buffer VisibleMeshletInstances
{
    uint            visible_meshlet_instances[];
};

// Shared data between all group. Each group works share the same meshlet, so anything common should be put here.
shared uint group_meshlet_data[4];

#define WAVE_SIZE 32

layout (local_size_x = WAVE_SIZE, local_size_y = 1, local_size_z = 1) in;
void main() {

    if (gl_GlobalInvocationID.x == 0 ) {
        meshlet_index_count = 0;
    }

    global_shader_barrier();

#if defined (PER_MESHLET_INDEX_WRITE)
    uint meshlet_instance_index = gl_GlobalInvocationID.x;
#else
    uint meshlet_instance_index = gl_WorkGroupID.x;

#endif // PER_MESHLET_INDEX_WRITE

    // Early out for invisible meshlet instances.
    if (visible_meshlet_instances[meshlet_instance_index] == 0) {
        return;
    }

    // Find draw id for the command
    uvec2 meshlet_instance_data = meshlet_instances[meshlet_instance_index];

    uint mesh_instance_index = meshlet_instance_data.y;

    uint mesh_draw_index = mesh_instance_draws[mesh_instance_index].mesh_draw_index;

    MeshDraw mesh_draw = mesh_draws[mesh_draw_index];
    const uint meshlet_offset = mesh_draw.meshlet_offset;
    const uint meshlet_count = mesh_draw.meshlet_count;

#if defined (PER_MESHLET_INDEX_WRITE)
    uint global_meshlet_index = meshlet_instance_data.x;
    uint vertex_count = uint(meshlets[global_meshlet_index].vertex_count);
    uint meshlet_data_offset = meshlets[global_meshlet_index].data_offset;
    uint triangle_count = uint(meshlets[global_meshlet_index].triangle_count);
#else

    uint destination_meshlet_index = atomicAdd(meshlet_index_count, 1);

    // Group shared load of meshlet data, that is shared amongst all threads of this group.
    if (gl_LocalInvocationID.x == 0) {

        const uint cached_meshlet_index = meshlet_instance_data.x;
        group_meshlet_data[0] = cached_meshlet_index;

        group_meshlet_data[1] = uint(meshlets[cached_meshlet_index].vertex_count);

        group_meshlet_data[2] = meshlets[cached_meshlet_index].data_offset;

        group_meshlet_data[3] = uint(meshlets[cached_meshlet_index].triangle_count);
    }

    group_barrier();

    destination_meshlet_index /= 32;

    uint global_meshlet_index = group_meshlet_data[0];
    uint vertex_count = group_meshlet_data[1];
    uint meshlet_data_offset = group_meshlet_data[2];
    uint triangle_count = group_meshlet_data[3];

#endif

    uint index_data_offset = meshlet_data_offset + vertex_count;
    uint index_count = triangle_count * 3;
    uint index_group_count = (index_count + 3) / 4;

    // 128 triangles * 3 = 384 indices
    // 384 indices / 4 groups = 96
#if defined (PER_MESHLET_INDEX_WRITE)
    {
        uint total_index_group_count = 96;
        uint index_offset = meshlet_instance_index * total_index_group_count;

        // Write per index data
        for (uint i = 0; i < index_group_count; ++i ) {
            uint indices_0 = meshlets_data[index_data_offset + i];
            uvec4 packed_indices = (uvec4(indices_0, indices_0 >> 8u, indices_0 >> 16u, indices_0 >> 24u) & 0xffu) | (meshlet_instance_index << 8u);
            indices[index_offset + i] = packed_indices;
        }

        for (uint i = index_group_count; i < total_index_group_count; ++i ) {
            indices[index_offset + i] = uvec4(0,0,0,0);
        }
    }
#else
    {
        uint local_id = gl_LocalInvocationID.x;

        // Following this article: https://diaryofagraphicsprogrammer.blogspot.com/2014/03/compute-shader-optimizations-for-amd.html
        // "Accessing TGSM with addresses that are 32 DWORD apart will lead to a situation where threads will use the same bank."
        // Scattering reading and writing 32 DWORD aparts.
        const uint bank_address_difference = 32;

        // Each meshlets writes 96 groups of 4 indices, that equals 384 indices, or 128 triangles * 3 indices.
        uint destination_offset = (WAVE_SIZE * 3) * destination_meshlet_index;

        // Doing 3 loads/stores at different memory offsets separated by bank address difference.

        // NOTE: I prefer to write unrolled version of the shader so it shows the access pattern in an easier way.
        // You could also write a for loop instead.
        // Write 0
        uint local_data_offset_0 = local_id;

        if (local_data_offset_0 < index_group_count) {
            uint indices_0 = meshlets_data[ index_data_offset + local_data_offset_0 ];
            uint encoded_mesh_instance_index = meshlet_instance_index << 8u;
            indices[destination_offset + local_data_offset_0] = (uvec4(indices_0, indices_0 >> 8u, indices_0 >> 16u, indices_0 >> 24u) & 0xffu) | encoded_mesh_instance_index;
        }
        else
        {
            indices[destination_offset + local_data_offset_0] = uvec4(0,0,0,0);
        }

        // Write 1 at local id + 32 (both reading and writing local offsets)
        uint local_data_offset_1 = local_id + bank_address_difference;

        if (local_data_offset_1 < index_group_count) {
            uint indices_1 = meshlets_data[ index_data_offset + local_data_offset_1 ];
            uint encoded_mesh_instance_index = meshlet_instance_index << 8u;
            indices[destination_offset + local_data_offset_1] = (uvec4(indices_1, indices_1 >> 8u, indices_1 >> 16u, indices_1 >> 24u) & 0xffu) | encoded_mesh_instance_index;
        }
        else
        {
            indices[destination_offset + local_data_offset_1] = uvec4(0,0,0,0);
        }

        // Write 2 at local id + 64 (both reading and writing local offsets)
        uint local_data_offset_2 = local_id + bank_address_difference * 2;

        if (local_data_offset_2 < index_group_count) {
            uint indices_2 = meshlets_data[ index_data_offset + local_data_offset_2 ];
            uint encoded_mesh_instance_index = meshlet_instance_index << 8u;
            indices[destination_offset + local_data_offset_2] = (uvec4(indices_2, indices_2 >> 8u, indices_2 >> 16u, indices_2 >> 24u) & 0xffu) | encoded_mesh_instance_index;
        }
        else
        {
            indices[destination_offset + local_data_offset_2] = uvec4(0,0,0,0);
        }
    }
#endif // PER_MESHLET_INDEX_WRITE

    // Uncomment this for debugging purposes.
    //indices[meshlet_index] = uvec4(meshlet_offset, mesh_instance_index, draw_command_index, meshlet_count);
    //indices[meshlet_index] = uvec4(mesh_instance_index, draw_command_index, meshlet_offset, global_meshlet_index);
}

#endif // COMPUTE_GENERATE_MESHLET_INDEX_BUFFER

#if defined(COMPUTE_GENERATE_MESHLET_INSTANCES)

layout(set = MATERIAL_SET, binding = 1) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

layout(set = MATERIAL_SET, binding = 3) readonly buffer MeshletData
{
    uint meshletData[];
};

layout(set = MATERIAL_SET, binding = 6) buffer VisibleMeshInstances
{
    MeshDrawCommand draw_commands[];
};

layout(set = MATERIAL_SET, binding = 7) buffer VisibleMeshCount
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
    uint meshlet_instances_count;
};

layout(set = MATERIAL_SET, binding = 17) buffer IndirectPerMeshletCounts
{
    uint per_meshlets_dispatch_task_x;
    uint per_meshlets_dispatch_task_y;
    uint per_meshlets_dispatch_task_z;
    uint ipmc_pad000;
};

layout(set = MATERIAL_SET, binding = 8) buffer VisibleMeshletIndexBuffer
{
    uvec4           indices[];
};

layout(set = MATERIAL_SET, binding = 9) buffer MeshletInstances
{
    uvec2           meshlet_instances[];
};

//
layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main() {

    const uint draw_command_index = gl_GlobalInvocationID.x;

    if ( draw_command_index == 0 ) {
        meshlet_instances_count = 0;
    }

    global_shader_barrier();

    if ( draw_command_index >= opaque_mesh_visible_count ) {
        return;
    }

    uint mesh_instance_index = draw_commands[draw_command_index].drawId;
    uint mesh_draw_index = mesh_instance_draws[mesh_instance_index].mesh_draw_index;

    MeshDraw mesh_draw = mesh_draws[mesh_draw_index];
    const uint meshlet_offset = mesh_draw.meshlet_offset;
    const uint meshlet_count = mesh_draw.meshlet_count;

    uint instance_write_offset = atomicAdd(meshlet_instances_count, meshlet_count);
    for ( uint i = 0; i < meshlet_count; ++i ) {

        meshlet_instances[instance_write_offset + i] = uvec2(meshlet_offset + (i), mesh_instance_index);
    }

    global_shader_barrier();

    if ( draw_command_index == 0 ) {

#if defined (PER_MESHLET_INDEX_WRITE)
        dispatch_task_x = (meshlet_instances_count + 31 ) / 32;
#else
        dispatch_task_x = meshlet_instances_count;
#endif // PER_MESHLET_INDEX_WRITE

        // Write per meshlets indirect counts
        per_meshlets_dispatch_task_x = (meshlet_instances_count + 31 ) / 32;
        per_meshlets_dispatch_task_y = 1;
        per_meshlets_dispatch_task_z = 1;
    }
}

#endif // COMPUTE_GENERATE_MESHLET_INSTANCES


#if defined(COMPUTE_MESHLET_INSTANCE_CULLING)

layout(set = MATERIAL_SET, binding = 1) readonly buffer Meshlets
{
    Meshlet         meshlets[];
};

layout(set = MATERIAL_SET, binding = 6) buffer VisibleMeshInstances
{
    MeshDrawCommand draw_commands[];
};

layout(set = MATERIAL_SET, binding = 7) buffer VisibleMeshCount
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
    uint meshlet_instances_count;
};

layout(set = MATERIAL_SET, binding = 9) readonly buffer MeshletInstances
{
    uvec2           meshlet_instances[];
};

layout(set = MATERIAL_SET, binding = 17) buffer IndirectPerMeshletCounts
{
    uint meshlet_visible_count;
    uint pad000_ipmc;
    uint pad001_ipmc;
    uint pad002_ipmc;
};

layout(set = MATERIAL_SET, binding = 19) buffer VisibleMeshletInstances
{
    uint            visible_meshlet_instances[];
};

//
layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main() {

    if (gl_GlobalInvocationID.x == 0 ) {
        meshlet_visible_count = 0;
    }

    global_shader_barrier();

    uint meshlet_instance_index = gl_GlobalInvocationID.x;

    if (meshlet_instance_index >= meshlet_instances_count) {
        return;
    }

    uvec2 meshlet_instance_data = meshlet_instances[meshlet_instance_index];
    uint meshlet_index = meshlet_instance_data.x;
    uint mesh_instance_index = meshlet_instance_data.y;

    uint mesh_draw_index = mesh_instance_draws[mesh_instance_index].mesh_draw_index;
    mat4 model = mesh_instance_draws[mesh_instance_index].model;

    vec4 world_center = model * vec4(meshlets[meshlet_index].center, 1);

    float scale = length( model[0] );
    float radius = meshlets[meshlet_index].radius * scale * 1.1;   // Artificially inflate bounding sphere.

    vec3 cone_axis = mat3( model ) * vec3(int(meshlets[meshlet_index].cone_axis[0]) / 127.0, int(meshlets[meshlet_index].cone_axis[1]) / 127.0, int(meshlets[meshlet_index].cone_axis[2]) / 127.0);
    float cone_cutoff = int(meshlets[meshlet_index].cone_cutoff) / 127.0;

    bool accept = false;
    vec4 view_center = vec4(0);
    // Backface culling and move meshlet in camera space
    if ( freeze_occlusion_camera == 0 ) {
        accept = !coneCull(world_center.xyz, radius, cone_axis, cone_cutoff, camera_position.xyz);
        view_center = world_to_camera * world_center;
    } else {
        accept = !coneCull(world_center.xyz, radius, cone_axis, cone_cutoff, camera_position_debug.xyz);
        view_center = world_to_camera_debug * world_center;
    }

    bool frustum_visible = true;
    for ( uint i = 0; i < 6; ++i ) {
        frustum_visible = frustum_visible && (dot( frustum_planes[i], view_center) > -radius);
    }

    frustum_visible = frustum_visible || (frustum_cull_meshlets == 0);

    bool occlusion_visible = true;
    if ( frustum_visible ) {

        vec3 camera_world_position = freeze_occlusion_camera == 0 ? camera_position.xyz : camera_position_debug.xyz;
        mat4 culling_view_projection = late_flag == 0 ? previous_view_projection : view_projection;

        occlusion_visible = occlusion_cull( view_center.xyz, radius, z_near, projection_00, projection_11,
                                            depth_pyramid_texture_index, world_center.xyz, camera_world_position,
                                            culling_view_projection );
    }

    occlusion_visible = occlusion_visible || (occlusion_cull_meshlets == 0);

    bool visible = (frustum_visible && accept && occlusion_visible);
    visible_meshlet_instances[meshlet_instance_index] = (visible == true) ? 1 : 0;

    if (visible) {
        atomicAdd(meshlet_visible_count, 1);
    }
}

#endif // COMPUTE_MESHLET_INSTANCE_CULLING

#if defined(COMPUTE_MESHLET_WRITE_COUNTS)

layout(set = MATERIAL_SET, binding = 1) readonly buffer Meshlets
{
    Meshlet         meshlets[];
};

layout(set = MATERIAL_SET, binding = 6) buffer VisibleMeshInstances
{
    MeshDrawCommand draw_commands[];
};

layout(set = MATERIAL_SET, binding = 7) buffer VisibleMeshCount
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
    uint meshlet_instances_count;
};

layout(set = MATERIAL_SET, binding = 9) readonly buffer MeshletInstances
{
    uvec2           meshlet_instances[];
};

layout(set = MATERIAL_SET, binding = 17) readonly buffer IndirectPerMeshletCounts
{
    uint meshlet_visible_count;
    uint pad000_ipmc;
    uint pad001_ipmc;
    uint pad002_ipmc;
};

layout(set = MATERIAL_SET, binding = 19) buffer VisibleMeshletInstances
{
    uint            visible_meshlet_instances[];
};

//
layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main() {

    draw_commands[0].indexCount = meshlet_visible_count * 384;

    meshlet_index_count = 0;
}

#endif // COMPUTE_MESHLET_WRITE_COUNTS

#if defined (VERTEX_EMULATION_GBUFFER_CULLING)

layout(set = MATERIAL_SET, binding = 1) readonly buffer Meshlets
{
    Meshlet meshlets[];
};

layout(set = MATERIAL_SET, binding = 3) readonly buffer MeshletData
{
    uint meshletData[];
};

layout(set = MATERIAL_SET, binding = 4) readonly buffer VertexPositions
{
    VertexPosition vertex_positions[];
};

layout(set = MATERIAL_SET, binding = 5) readonly buffer VertexData
{
    VertexExtraData vertex_data[];
};

layout(set = MATERIAL_SET, binding = 6) readonly buffer VisibleMeshInstances
{
    MeshDrawCommand draw_commands[];
};

layout(set = MATERIAL_SET, binding = 7) readonly buffer VisibleMeshCount
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
    uint meshlet_instances_count;
};

layout(set = MATERIAL_SET, binding = 9) readonly buffer MeshletInstances
{
    uvec2           meshlet_instances[];
};

layout (location = 0) out vec2 vTexcoord0;
layout (location = 1) out vec4 vNormal_BiTanX;
layout (location = 2) out vec4 vTangent_BiTanY;
layout (location = 3) out vec4 vPosition_BiTanZ;
layout (location = 4) out flat uint mesh_draw_index;

#if defined(DEBUG)
layout (location = 5) out vec3 vColor;
#endif

uint hash(uint a)
{
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);
   return a;
}

void main() {

    uint meshlet_instance_index = gl_VertexIndex >> 8u;

    uvec2 meshlet_instance_data = meshlet_instances[meshlet_instance_index];
    uint mesh_instance_index = meshlet_instance_data.y;
    uint meshlet_index = meshlet_instance_data.x;

    MeshDraw mesh_draw = mesh_draws[ meshlets[meshlet_index].mesh_index ];

    uint vertex_count = uint(meshlets[meshlet_index].vertex_count);

    uint data_offset = meshlets[meshlet_index].data_offset;
    uint vertex_offset = data_offset;

    uint meshlet_vertex_index = gl_VertexIndex & 0xffu;

    if ( meshlet_vertex_index >= vertex_count ) {
        return;
    }

    mat4 model = mesh_instance_draws[mesh_instance_index].model;

    uint vi = meshletData[vertex_offset + meshlet_vertex_index];

    vec3 position = vec3(vertex_positions[vi].v.x, vertex_positions[vi].v.y, vertex_positions[vi].v.z);

    gl_Position = view_projection * (model * vec4(position, 1));

    bool has_normals = (mesh_draw.flags & DrawFlags_HasNormals) != 0;
    bool has_tangents = (mesh_draw.flags & DrawFlags_HasTangents) != 0;

    float i8_inverse = 1.0 / 127.0;

    if ( has_normals ) {
        mat4 model_inverse = mesh_instance_draws[mesh_instance_index].model_inverse;
        vec3 normal = vec3(int(vertex_data[vi].nx), int(vertex_data[vi].ny), int(vertex_data[vi].nz)) * i8_inverse - 1.0;
        vNormal_BiTanX.xyz = normalize( mat3(model_inverse) * normal );
    }

    if ( has_tangents ) {
        vec3 tangent = vec3(int(vertex_data[vi].tx), int(vertex_data[vi].ty), int(vertex_data[vi].tz)) * i8_inverse - 1.0;
        vTangent_BiTanY.xyz = normalize( mat3(model) * tangent.xyz );

        vec3 bitangent = cross( vNormal_BiTanX.xyz, tangent.xyz ) * ( int(vertex_data[vi].tw) * i8_inverse  - 1.0 );
        vNormal_BiTanX.w = bitangent.x;
        vTangent_BiTanY.w = bitangent.y;
        vPosition_BiTanZ.w = bitangent.z;
    }

    vTexcoord0 = vec2(vertex_data[vi].tu, vertex_data[vi].tv);

    vec4 worldPosition = model * vec4(position, 1.0);
    vPosition_BiTanZ.xyz = worldPosition.xyz / worldPosition.w;

    mesh_draw_index = meshlets[meshlet_index].mesh_index;

#if defined(DEBUG)
    uint mhash = hash(meshlet_index);
    vColor = vec3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;
#endif

}

#endif // VERTEX_EMULATION_GBUFFER_CULLING
