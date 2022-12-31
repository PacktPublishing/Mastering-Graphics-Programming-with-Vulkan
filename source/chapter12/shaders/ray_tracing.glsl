#extension GL_EXT_ray_tracing : enable

struct ray_payload {
    int geometry_id;
    int primitive_id;
    vec2 barycentric_weights;
    mat4x3 object_to_world;
    float t;
};

#ifdef RAYGEN_TEST

layout( location = 0 ) rayPayloadEXT ray_payload payload;

layout( binding = 1, set = MATERIAL_SET ) uniform accelerationStructureEXT as;
layout( binding = 3, set = MATERIAL_SET ) uniform rayParams
{
    uint sbt_offset; // shader binding table offset
    uint sbt_stride; // shader binding table stride
    uint miss_index;
    uint out_image_index;
};

// NOTE(marco): adapted from https://www.scratchapixel.com/lessons/3d-basic-rendering/ray-tracing-generating-camera-rays/generating-camera-rays
vec3 compute_ray_dir( uvec3 launchID, uvec3 launchSize) {
    float x = ( 2 * ( float( launchID.x ) + 0.5 ) / float( launchSize.x ) - 1.0 );
    float y = ( 1.0 - 2 * ( float( launchID.y ) + 0.5 ) / float( launchSize.y ) );
    vec4 dir = inverse_view_projection * vec4( x, y, 1, 1 );
    dir = normalize( dir );

    return dir.xyz;
}

void main()
{
    traceRayEXT( as, // topLevel
                 gl_RayFlagsOpaqueEXT, // rayFlags
                 0xff, // cullMask
                 sbt_offset, // sbtRecordOffset
                 sbt_stride, // sbtRecordStride
                 miss_index, // missIndex
                 camera_position.xyz, // origin
                 0.0, // Tmin
                 compute_ray_dir( gl_LaunchIDEXT, gl_LaunchSizeEXT ), // direction
                 100.0, // Tmax
                 0 // payload index
                );

    if ( payload.geometry_id != -1 ) {
        MeshInstanceDraw instance = mesh_instance_draws[ payload.geometry_id ];
        uint mesh_index = instance.mesh_draw_index;
        MeshDraw mesh = mesh_draws[ mesh_index ];

        int_array_type index_buffer = int_array_type( mesh.index_buffer );
        int i0 = index_buffer[ payload.primitive_id * 3 ].v;
        int i1 = index_buffer[ payload.primitive_id * 3 + 1 ].v;
        int i2 = index_buffer[ payload.primitive_id * 3 + 2 ].v;

        float_array_type vertex_buffer = float_array_type( mesh.position_buffer );
        vec4 p0 = vec4(
            vertex_buffer[ i0 * 3 + 0 ].v,
            vertex_buffer[ i0 * 3 + 1 ].v,
            vertex_buffer[ i0 * 3 + 2 ].v,
            1.0
        );
        vec4 p1 = vec4(
            vertex_buffer[ i1 * 3 + 0 ].v,
            vertex_buffer[ i1 * 3 + 1 ].v,
            vertex_buffer[ i1 * 3 + 2 ].v,
            1.0
        );
        vec4 p2 = vec4(
            vertex_buffer[ i2 * 3 + 0 ].v,
            vertex_buffer[ i2 * 3 + 1 ].v,
            vertex_buffer[ i2 * 3 + 2 ].v,
            1.0
        );

        vec4 p0_world = vec4( payload.object_to_world * p0, 1.0 );
        vec4 p1_world = vec4( payload.object_to_world * p1, 1.0 );
        vec4 p2_world = vec4( payload.object_to_world * p2, 1.0 );

        vec4 p0_screen = view_projection * p0_world;
        vec4 p1_screen = view_projection * p1_world;
        vec4 p2_screen = view_projection * p2_world;

        ivec2 texture_size = textureSize( global_textures[ nonuniformEXT( mesh.textures.x ) ], 0 );

        vec2_array_type uv_buffer = vec2_array_type( mesh.uv_buffer );
        vec2 uv0 = uv_buffer[ i0 ].v;
        vec2 uv1 = uv_buffer[ i1 ].v;
        vec2 uv2 = uv_buffer[ i2 ].v;

        // TODO(marco): use ray differentials
        float texel_area = texture_size.x * texture_size.y * abs( ( uv1.x - uv0.x ) * ( uv2.y - uv0.y ) - ( uv2.x - uv0.x ) * ( uv1.y - uv0.y ) );
        float triangle_area = abs( ( p1_screen.x - p0_screen.x ) * ( p2_screen.y - p0_screen.y ) - ( p2_screen.x - p0_screen.x ) * ( p1_screen.y - p0_screen.y ) );
        float lod = floor( 0.5 * log2( texel_area / triangle_area ) );

        float b = payload.barycentric_weights.x;
        float c = payload.barycentric_weights.y;
        float a = 1 - b - c;

        vec2 uv = ( a * uv0 + b * uv1 + c * uv2 );

        vec3 diffuse = textureLod( global_textures[ nonuniformEXT( mesh.textures.x ) ], uv, lod ).rgb;

        imageStore( global_images_2d[ out_image_index ], ivec2( gl_LaunchIDEXT.xy ), vec4( diffuse, 1.0 ) );
    } else {
        imageStore( global_images_2d[ out_image_index ], ivec2( gl_LaunchIDEXT.xy ), vec4( 0.529, 0.807, 0.921, 1 ) );
    }
}

#endif

#ifdef CLOSEST_HIT_TEST

layout( location = 0 ) rayPayloadInEXT ray_payload payload;
hitAttributeEXT vec2 barycentric_weights;

void main() {
    payload.geometry_id = gl_GeometryIndexEXT;
    payload.primitive_id = gl_PrimitiveID;
    payload.barycentric_weights = barycentric_weights;
    payload.object_to_world = gl_ObjectToWorldEXT;
    payload.t = gl_HitTEXT;
}

#endif

#ifdef MISS_TEST

layout( location = 0 ) rayPayloadInEXT ray_payload payload;

void main() {
    payload.geometry_id = -1;
    payload.primitive_id = -1;
    payload.barycentric_weights = vec2( 0 );
}

#endif
