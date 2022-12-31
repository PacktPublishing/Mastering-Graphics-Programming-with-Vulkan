#extension GL_EXT_ray_tracing : enable

#ifdef RAYGEN_MAIN

layout( location = 0 ) rayPayloadEXT vec4 payload;

layout( binding = 1, set = MATERIAL_SET ) uniform accelerationStructureEXT as;
layout( binding = 2, set = MATERIAL_SET ) uniform rayParams
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
    payload = vec4( 0, 0, 1, 1 );

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

    imageStore( global_images_2d[ out_image_index ], ivec2( gl_LaunchIDEXT.xy ), payload );
}

#endif

#ifdef CLOSEST_HIT_MAIN

layout( location = 0 ) rayPayloadInEXT vec4 payload;

void main() {
    payload = vec4( 1.0, 0.0, 0.0, 1.0 );
}

#endif

#ifdef MISS_MAIN

layout( location = 0 ) rayPayloadInEXT vec4 payload;

void main() {
    payload = vec4( 0.0, 1.0, 0.0, 1.0 );
}

#endif
