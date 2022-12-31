
#extension GL_EXT_shader_16bit_storage: require
#extension GL_EXT_shader_8bit_storage: require
#extension GL_NV_mesh_shader: require

#extension GL_KHR_shader_subgroup_ballot: require

#define DEBUG 0

// Common data
struct VertexExtraData
{
    uint8_t nx, ny, nz, nw; // normal
    uint8_t tx, ty, tz, tw; // tangent
    float16_t tu, tv;       // tex coords
    float padding;
};

struct VertexPosition
{
    vec3 v;
    float padding;
};

struct Meshlet
{
    // vec3 keeps Meshlet aligned to 16 bytes which is important because C++ has an alignas() directive
    vec3 center;
    float radius;
    int8_t cone_axis[3];
    int8_t cone_cutoff;

    uint dataOffset;
    uint mesh_index;
    uint8_t vertexCount;
    uint8_t triangleCount;
};


#if defined (TASK_DEPTH_PRE) || defined(TASK_GBUFFER_CULLING) || defined(TASK_TRANSPARENT_NO_CULL)

#define CULL 1

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(set = MATERIAL_SET, binding = 1) readonly buffer Meshlets
{
    Meshlet meshlets[];
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
};

out taskNV block
{
    uint meshletIndices[32];
};

// NOTE(marco): as described in meshoptimizer.h
bool coneCull(vec3 center, float radius, vec3 cone_axis, float cone_cutoff, vec3 camera_position)
{
    return dot(center - camera_position, cone_axis) >= cone_cutoff * length(center - camera_position) + radius;
}

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
        accept = !coneCull(world_center.xyz, radius, cone_axis, cone_cutoff, eye.xyz);
        view_center = world_to_camera * world_center;
    } else {
        accept = !coneCull(world_center.xyz, radius, cone_axis, cone_cutoff, eye_debug.xyz);
        view_center = world_to_camera_debug * world_center;
    }

    bool frustum_visible = true;
    for ( uint i = 0; i < 6; ++i ) {
        frustum_visible = frustum_visible && (dot( frustum_planes[i], view_center) > -radius);
    }

    frustum_visible = frustum_visible || (frustum_cull_meshlets == 0);

    bool occlusion_visible = true;
    if ( frustum_visible ) {
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

            vec3 dir = normalize(eye.xyz - world_center.xyz);
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

    uint vertexCount = uint(meshlets[mi].vertexCount);
    uint triangleCount = uint(meshlets[mi].triangleCount);
    uint indexCount = triangleCount * 3;

    uint dataOffset = meshlets[mi].dataOffset;
    uint vertexOffset = dataOffset;
    uint indexOffset = dataOffset + vertexCount;

    bool has_normals = (mesh_draw.flags & DrawFlags_HasNormals) != 0;
    bool has_tangents = (mesh_draw.flags & DrawFlags_HasTangents) != 0;

    float i8_inverse = 1.0 / 127.0;

#if DEBUG
    uint mhash = hash(mi);
    vec3 mcolor = vec3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;
#endif

#if defined(MESH_TRANSPARENT_NO_CULL)
    uint mesh_instance_index = draw_commands[gl_DrawIDARB + total_count].drawId;
#else
    uint mesh_instance_index = draw_commands[gl_DrawIDARB].drawId;
#endif

    mat4 model = mesh_instance_draws[mesh_instance_index].model;
    mat4 model_inverse = mesh_instance_draws[mesh_instance_index].model_inverse;

    // TODO: if we have meshlets with 62 or 63 vertices then we pay a small penalty for branch divergence here - we can instead redundantly xform the last vertex
    for (uint i = ti; i < vertexCount; i += 32)
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
        gl_PrimitiveCountNV = uint(meshlets[mi].triangleCount);
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

    uint vertexCount = uint(meshlets[mi].vertexCount);
    uint triangleCount = uint(meshlets[mi].triangleCount);
    uint indexCount = triangleCount * 3;

    uint dataOffset = meshlets[mi].dataOffset;
    uint vertexOffset = dataOffset;
    uint indexOffset = dataOffset + vertexCount;

    uint mesh_instance_index = draw_commands[gl_DrawIDARB].drawId;
    mat4 model = mesh_instance_draws[mesh_instance_index].model;

    // TODO: if we have meshlets with 62 or 63 vertices then we pay a small penalty for branch divergence here - we can instead redundantly xform the last vertex
    for (uint i = ti; i < vertexCount; i += 32)
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
        gl_PrimitiveCountNV = uint(meshlets[mi].triangleCount);
}

#endif // MESH

#if defined(FRAGMENT_GBUFFER_CULLING) || defined(FRAGMENT_MESH)

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
    uint flags = mesh_draw.flags;

    vec3 world_position = vPosition_BiTanZ.xyz;
    vec3 normal = normalize(vNormal_BiTanX.xyz);
    if ( (flags & DrawFlags_HasNormals) == 0 ) {
        normal = normalize(cross(dFdx(world_position), dFdy(world_position)));
    }

    vec3 tangent = normalize(vTangent_BiTanY.xyz);
    vec3 bitangent = normalize(vec3(vNormal_BiTanX.w, vTangent_BiTanY.w, vPosition_BiTanZ.w));
    if ( (flags & DrawFlags_HasTangents) == 0 ) {
        vec3 uv_dx = dFdx(vec3(vTexcoord0, 0.0));
        vec3 uv_dy = dFdy(vec3(vTexcoord0, 0.0));

        // NOTE(marco): code taken from https://github.com/KhronosGroup/glTF-Sample-Viewer
        vec3 t_ = (uv_dy.t * dFdx(world_position) - uv_dx.t * dFdy(world_position)) /
                  (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);
        tangent = normalize(t_ - normal * dot(normal, t_));

        bitangent = cross( normal, tangent );
    }

    bool phong = ( flags & DrawFlags_Phong ) != 0;

    uvec4 textures = mesh_draw.textures;
    vec4 base_colour = phong ? mesh_draw.diffuse : mesh_draw.base_color_factor;
    if (textures.x != INVALID_TEXTURE_INDEX) {
        vec3 texture_colour = decode_srgb( texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0).rgb );
        base_colour *= vec4( texture_colour, 1.0 );
    }

    bool useAlphaMask = (flags & DrawFlags_AlphaMask) != 0;
    if (useAlphaMask && base_colour.a < mesh_draw.alpha_cutoff) {
        discard;
    }

    bool use_alpha_dither = (flags & DrawFlags_AlphaDither) != 0;
    if ( use_alpha_dither ) {
        float dithered_alpha = dither(gl_FragCoord.xy, base_colour.a);
        if (dithered_alpha < 0.001f) {
            discard;
        }
    }

    if (gl_FrontFacing == false)
    {
        tangent *= -1.0;
        bitangent *= -1.0;
        normal *= -1.0;
    }

    if (textures.z != INVALID_TEXTURE_INDEX) {
        // NOTE(marco): normal textures are encoded to [0, 1] but need to be mapped to [-1, 1] value
        vec3 bump_normal = normalize( texture(global_textures[nonuniformEXT(textures.z)], vTexcoord0).rgb * 2.0 - 1.0 );
        mat3 TBN = mat3(
            tangent,
            bitangent,
            normal
        );

        normal = normalize(TBN * normalize(bump_normal));
    }

    normal_out.rg = octahedral_encode(normal);

    float metalness = 0.0;
    float roughness = 0.0;
    float occlusion = 0.0;
    if (phong) {
        // TODO(marco): better conversion
        metalness = 0.5;
        roughness = max(pow((1 - mesh_draw.specular_exp), 2), 0.0001);
        emissive_out = vec4( 0, 0, 0, 1 );
    } else {
        roughness = mesh_draw.metallic_roughness_occlusion_factor.x;
        metalness = mesh_draw.metallic_roughness_occlusion_factor.y;

        if (textures.y != INVALID_TEXTURE_INDEX) {
            vec4 rm = texture(global_textures[nonuniformEXT(textures.y)], vTexcoord0);

            // Green channel contains roughness values
            roughness *= rm.g;

            // Blue channel contains metalness
            metalness *= rm.b;
        }

        occlusion = mesh_draw.metallic_roughness_occlusion_factor.z;
        if (textures.w != INVALID_TEXTURE_INDEX) {
            vec4 o = texture(global_textures[nonuniformEXT(textures.w)], vTexcoord0);
            // Red channel for occlusion value
            occlusion *= o.r;
        }

        emissive_out = vec4( mesh_draw.emissive.rgb, 1.0 );
        uint emissive_texture = uint(mesh_draw.emissive.w);
        if ( emissive_texture != INVALID_TEXTURE_INDEX ) {
            emissive_out *= vec4( decode_srgb( texture(global_textures[nonuniformEXT(emissive_texture)], vTexcoord0).rgb ), 1.0 );
        }
    }

    occlusion_roughness_metalness_out.rgb = vec3( occlusion, roughness, metalness );
#if DEBUG
    color_out = vColour;
#else
    color_out = base_colour;
#endif
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

    vec3 world_position = vPosition_BiTanZ.xyz;
    vec3 normal = normalize(vNormal_BiTanX.xyz);
    if ( (flags & DrawFlags_HasNormals) == 0 ) {
        normal = normalize(cross(dFdx(world_position), dFdy(world_position)));
    }

    vec3 tangent = normalize(vTangent_BiTanY.xyz);
    vec3 bitangent = normalize(vec3(vNormal_BiTanX.w, vTangent_BiTanY.w, vPosition_BiTanZ.w));
    if ( (flags & DrawFlags_HasTangents) == 0 ) {
        vec3 uv_dx = dFdx(vec3(vTexcoord0, 0.0));
        vec3 uv_dy = dFdy(vec3(vTexcoord0, 0.0));

        // NOTE(marco): code taken from https://github.com/KhronosGroup/glTF-Sample-Viewer
        vec3 t_ = (uv_dy.t * dFdx(world_position) - uv_dx.t * dFdy(world_position)) /
                  (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);
        tangent = normalize(t_ - normal * dot(normal, t_));

        bitangent = cross( normal, tangent );
    }

    bool phong = ( flags & DrawFlags_Phong ) != 0;

    uvec4 textures = mesh_draw.textures;
    vec4 base_colour = phong ? mesh_draw.diffuse : mesh_draw.base_color_factor;
    if (textures.x != INVALID_TEXTURE_INDEX) {
        vec4 texture_colour = texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0);
        base_colour *= vec4( decode_srgb( texture_colour.rgb ), texture_colour.a );
    }

    bool useAlphaMask = (flags & DrawFlags_AlphaMask) != 0;
    if (useAlphaMask && base_colour.a < mesh_draw.alpha_cutoff) {
        discard;
    }

    bool use_alpha_dither = (flags & DrawFlags_AlphaDither) != 0;
    if ( use_alpha_dither ) {
        float dithered_alpha = dither(gl_FragCoord.xy, base_colour.a);
        if (dithered_alpha < 0.001f) {
            discard;
        }
    }

    if (gl_FrontFacing == false)
    {
        tangent *= -1.0;
        bitangent *= -1.0;
        normal *= -1.0;
    }

    if (textures.z != INVALID_TEXTURE_INDEX) {
        // NOTE(marco): normal textures are encoded to [0, 1] but need to be mapped to [-1, 1] value
        vec3 bump_normal = normalize( texture(global_textures[nonuniformEXT(textures.z)], vTexcoord0).rgb * 2.0 - 1.0 );
        mat3 TBN = mat3(
            tangent,
            bitangent,
            normal
        );

        normal = normalize(TBN * normalize(bump_normal));
    }

    float metalness = 0.0;
    float roughness = 0.0;
    float occlusion = 0.0;
    vec3 emissive_colour = vec3(0);
    if (phong) {
        // TODO(marco): better conversion
        metalness = 0.5;
        roughness = max(pow((1 - mesh_draw.specular_exp), 2), 0.0001);
        emissive_colour = vec3( 0 );
    } else {
        roughness = mesh_draw.metallic_roughness_occlusion_factor.x;
        metalness = mesh_draw.metallic_roughness_occlusion_factor.y;

        if (textures.y != INVALID_TEXTURE_INDEX) {
            vec4 rm = texture(global_textures[nonuniformEXT(textures.y)], vTexcoord0);

            // Green channel contains roughness values
            roughness *= rm.g;

            // Blue channel contains metalness
            metalness *= rm.b;
        }

        occlusion = mesh_draw.metallic_roughness_occlusion_factor.z;
        if (textures.w != INVALID_TEXTURE_INDEX) {
            vec4 o = texture(global_textures[nonuniformEXT(textures.w)], vTexcoord0);
            // Red channel for occlusion value
            occlusion *= o.r;
        }

        emissive_colour = mesh_draw.emissive.rgb;
        uint emissive_texture = uint(mesh_draw.emissive.w);
        if ( emissive_texture != INVALID_TEXTURE_INDEX ) {
            emissive_colour *= decode_srgb( texture(global_textures[nonuniformEXT(emissive_texture)], vTexcoord0).rgb );
        }
    }

#if DEBUG
    color_out = vColour;
#else
    color_out = calculate_lighting( base_colour, vec3(occlusion, roughness, metalness), normal, emissive_colour.rgb, world_position );
#endif
}

#endif // FRAGMENT_TRANSPARENT_NO_CULL
