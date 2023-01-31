// Common Raytracing code ////////////////////////////////////////////////
#if defined (RAYGEN_REFLECTIONS_RT) || defined (CLOSEST_HIT_REFLECTIONS_RT) || defined (MISS_REFLECTIONS_RT)

#extension GL_EXT_ray_tracing : enable

struct RayPayload {
    int geometry_id;
    int primitive_id;
    vec2 barycentric_weights;
    mat4x3 object_to_world;
    uint triangle_facing;
    float t;
};

layout( set = MATERIAL_SET, binding = 40 ) uniform ReflectionsConstants
{
    uint sbt_offset; // shader binding table offset
    uint sbt_stride; // shader binding table stride
    uint miss_index;
    uint out_image_index;

    uvec4 gbuffer_texures; // x = roughness, y= normals, z = indirect lighting
};

layout( push_constant ) uniform PushConstants {
    float       resolution_scale;
};

#endif

#if defined (RAYGEN_REFLECTIONS_RT)

layout( location = 0 ) rayPayloadEXT RayPayload payload;

uint rng_state;

// NOTE(marco): as implemented in https://www.reedbeta.com/blog/quick-and-easy-gpu-random-numbers-in-d3d11/
uint wang_hash( uint seed ) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

uint get_uniform_random_value( uint state ) {
    // Xorshift algorithm from George Marsaglia's paper
    state ^= ( state << 13 );
    state ^= ( state >> 17 );
    state ^= ( state << 5 );
    return state;
}

// NOTE(marco): as implemented in https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint rand_pcg() {
    uint state = rng_state;
    rng_state = rng_state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uvec2 pcg2d( uvec2 v )
{
    v = v * 1664525u + 1013904223u;

    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;

    v = v ^ ( v >> 16u );

    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;

    v = v ^ (v >> 16u);

    return v;
}

// NOTE(marco): reference: https://jcgt.org/published/0009/03/02/ - Hash Functions for GPU Rendering
uint seed(uvec2 p) {
    return 19u * p.x + 47u * p.y + 101u;
}

// NOTE(marco): taken from https://jcgt.org/published/0007/04/01/ - Sampling the GGX Distribution of Visible Normals
// Input Ve: view direction
// Input alpha_x, alpha_y: roughness parameters
// Input U1, U2: uniform random numbers
// Output Ne: normal sampled with PDF D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z
vec3 sampleGGXVNDF(vec3 Ve, float alpha_x, float alpha_y, float U1, float U2)
{
    // Section 3.2: transforming the view direction to the hemisphere configuration
    vec3 Vh = normalize(vec3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));
    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * inversesqrt(lensq) : vec3(1,0,0);
    vec3 T2 = cross(Vh, T1);
    // Section 4.2: parameterization of the projected area
    float r = sqrt(U1);
    float phi = 2.0 * PI * U2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s)*sqrt(1.0 - t1*t1) + s*t2;
    // Section 4.3: reprojection onto hemisphere
    vec3 Nh = t1*T1 + t2*T2 + sqrt(max(0.0, 1.0 - t1*t1 - t2*t2))*Vh;
    // Section 3.4: transforming the normal back to the ellipsoid configuration
    vec3 Ne = normalize(vec3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));
    return Ne;
}

void main() {
    ivec2 xy = ivec2( gl_LaunchIDEXT.xy );
    ivec2 test_fragment = ivec2( resolution ) / 2;

    bool render_debug_line = false;//( xy == test_fragment );
    vec4 white =  vec4( 1 );
    vec4 black =  vec4( 0, 0, 0, 1 );
    vec4 yellow = vec4( 1, 1, 0, 1 );
    vec4 red =    vec4( 1, 0, 0, 1 );
    vec4 green =  vec4( 0, 1, 0, 1 );
    vec4 blue =   vec4( 0, 0, 1, 1 );

    ivec2 scaled_xy = ivec2( xy * resolution_scale );

    float roughness = forced_roughness > 0.0 ? forced_roughness : texelFetch( global_textures[ gbuffer_texures.x ], scaled_xy, 0 ).y;

    rng_state = seed( gl_LaunchIDEXT.xy ) + current_frame;

    float rnd_normalizer = 1.0 / float( 0xFFFFFFFFu );

    // float U1 = rand_pcg() * rnd_normalizer;
    // float U2 = rand_pcg() * rnd_normalizer;
    vec2 U = vec2( pcg2d( gl_LaunchIDEXT.xy + uvec2( current_frame) ) ) * rnd_normalizer;
    U = interleaved_gradient_noise2( xy, current_frame );

    vec3 reflection_colour = vec3( 0 );

    if ( roughness <= 0.3 ) {
        ivec2 scaled_xy = ivec2(xy * resolution_scale);
        float depth = texelFetch( global_textures[ depth_texture_index ], scaled_xy, 0 ).r;
        vec2 screen_uv = uv_nearest( xy, resolution / resolution_scale );
        vec3 world_pos = world_position_from_depth( screen_uv, depth, inverse_view_projection );

        vec3 incoming = normalize( world_pos - camera_position.xyz );

        vec2 encoded_normal = texelFetch( global_textures[ gbuffer_texures.y ], scaled_xy, 0 ).rg;
        vec3 normal = octahedral_decode( encoded_normal );

        vec3 vndf_normal = sampleGGXVNDF( incoming, roughness, roughness, U.x, U.y );

        // TODO(marco): if we use this we get no reflections?!
        float vndf_angle = dot( normal, vndf_normal );

        vec3 reflected_ray = normalize( reflect( incoming, vndf_normal ) );

        traceRayEXT( as, // topLevel
                 gl_RayFlagsOpaqueEXT, // rayFlags
                 0xff, // cullMask
                 sbt_offset, // sbtRecordOffset
                 sbt_stride, // sbtRecordStride
                 miss_index, // missIndex
                 world_pos, // origin
                 0.05, // Tmin
                 reflected_ray, // direction
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

            float flip_normal = payload.triangle_facing == gl_HitKindFrontFacingTriangleEXT ? -1 : 1;
            vec3 triangle_normal = normalize( cross( p1_world.xyz - p0_world.xyz, p2_world.xyz - p0_world.xyz ) ) * flip_normal;

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
            vec3 p_world = world_pos + reflected_ray * payload.t;

            if ( render_debug_line ) {
                debug_draw_line( world_pos, p_world, white, yellow );
                debug_draw_line( p_world, p_world + ( triangle_normal * 2 ), white, red );
            }

            float lights_importance[ NUM_LIGHTS ];
            float total_importance = 0.0;

            for ( uint l = 0; l < active_lights; ++l ) {
                // Compute light importance by using something similar to "Importance Sampling of Many Lights on the GPU"
                Light light = lights[ l ];
                vec3 p_to_light = light.world_position - p_world.xyz;

                float point_light_angle = dot( normalize( p_to_light ), triangle_normal );

                float distance_sq = dot( p_to_light, p_to_light );
                float r_sq = light.radius * light.radius;

                bool light_active = ( point_light_angle > 1e-4 ) && ( distance_sq <= r_sq );
                float theta_u = asin( light.radius / sqrt( distance_sq ) );

                // TODO(marco): can we avoid using acos?
                float theta_i = acos( point_light_angle );

                float theta_prime = max( 0, theta_i - theta_u );
                float orientation = abs( cos( theta_prime ) );

                float importance = ( light.intensity * orientation ) / distance_sq;

                float final_value = light_active ? importance : 0.0;
                lights_importance[ l ] = final_value;

                total_importance += final_value;
            }

            for ( uint l = 0; l < active_lights; ++l ) {
                lights_importance[ l ] /= total_importance;
            }

            float rnd_value = rand_pcg() * rnd_normalizer;

            uint light_index = 0;
            float accum_probability = 0.0;
            for ( ; light_index < active_lights; ++light_index ) {
                accum_probability += lights_importance[ light_index ];

                if ( accum_probability > rnd_value ) {
                    break;
                }
            }

            if ( light_index < active_lights ) {
                Light light = lights[ light_index ];
                vec3 p_to_light = light.world_position - p_world.xyz;
                vec3 l = normalize( p_to_light );
                float light_distance = sqrt( dot( p_to_light, p_to_light ) );

                traceRayEXT( as, // topLevel
                    gl_RayFlagsOpaqueEXT, // rayFlags
                    0xff, // cullMask
                    sbt_offset, // sbtRecordOffset
                    sbt_stride, // sbtRecordStride
                    miss_index, // missIndex
                    p_world.xyz, // origin
                    0.05, // Tmin
                    l, // direction
                    light_distance, // Tmax
                    0 // payload index
                    );

                float shadow_term = payload.geometry_id == -1 ? 1.0 : 0.0;

                if ( render_debug_line ) {
                    debug_draw_line( p_world, p_world + l * light_distance, yellow, yellow );
                }

                // TODO(marco): refactor this to use calculate_point_light_contribution
                float attenuation = attenuation_square_falloff( p_to_light, 1.0f / light.radius ) * shadow_term;
                float NoL = clamp(dot( triangle_normal, l ), 0.0, 1.0);

                if ( attenuation > 0.0001f  && NoL > 0.0001f ) {
                    vec3 orm = calculate_pbr_parameters( mesh.metallic_roughness_occlusion_factor.x, mesh.metallic_roughness_occlusion_factor.y,
                                                        mesh.textures.y, mesh.metallic_roughness_occlusion_factor.z, mesh.textures.w, uv );

                    vec3 view = normalize( world_pos - p_world.xyz );
                    float NoV = saturate( dot( triangle_normal, view ));

                    float roughness = forced_roughness > 0.0 ? forced_roughness : orm.g * orm.g;
                    float metallic = forced_metalness > 0.0 ? forced_metalness : orm.b;

                    vec4 albedo = textureLod( global_textures[ nonuniformEXT( mesh.textures.x ) ], uv, lod );

                    vec3 light_intensity = NoL * light.intensity * attenuation * light.color;

                    reflection_colour = albedo.rgb * light_intensity;
                }

                // reflection_colour = vec3( shadow_term, 0, 0 );
            }

            // Indirect light sampling
            vec3 indirect_color = sample_irradiance( p_world.xyz, triangle_normal, camera_position.xyz );
            reflection_colour += indirect_color;
        }
    }

    imageStore( global_images_2d[ out_image_index ], ivec2( gl_LaunchIDEXT.xy ), vec4( reflection_colour, 1 ) );
}

#endif

#if defined (CLOSEST_HIT_REFLECTIONS_RT)

layout( location = 0 ) rayPayloadInEXT RayPayload payload;
hitAttributeEXT vec2 barycentric_weights;

void main() {

    payload.geometry_id = gl_GeometryIndexEXT;
    payload.primitive_id = gl_PrimitiveID;
    payload.barycentric_weights = barycentric_weights;
    payload.object_to_world = gl_ObjectToWorldEXT;
    payload.t = gl_HitTEXT;
    payload.triangle_facing = gl_HitKindEXT;
}

#endif

#if defined (MISS_REFLECTIONS_RT)

layout( location = 0 ) rayPayloadInEXT RayPayload payload;

void main() {
    payload.geometry_id = -1;
}

#endif

#if defined( COMPUTE_SVGF_ACCUMULATION ) || defined( COMPUTE_SVGF_VARIANCE ) || defined( COMPUTE_SVGF_WAVELET ) || defined(COMPUTE_SVGF_DOWNSAMPLE)

layout( set = MATERIAL_SET, binding = 40 ) uniform SVGFAccumulationConstants
{
    uint motion_vectors_texture_index;
    uint mesh_id_texture_index;
    uint normals_texture_index;
    uint depth_normal_fwidth_texture_index;

    uint history_mesh_id_texture_index;
    uint history_normals_texture_index;
    uint history_linear_depth_texture;
    uint reflections_texture_index;

    uint history_reflections_texture_index;
    uint history_moments_texture_index;
    uint integrated_color_texture_index;
    uint integrated_moments_texture_index;

    uint variance_texture_index;
    uint filtered_color_texture_index;
    uint updated_variance_texture_index;
    uint linear_z_dd_texture_index;

    float resolution_scale;
    float resolution_scale_rcp;
    float temporal_depth_difference;
    float temporal_normal_difference;
};

#endif

#if defined( COMPUTE_SVGF_ACCUMULATION )

bool check_temporal_consistency( uvec2 frag_coord ) {

    ivec2 scaled_xy = ivec2( (frag_coord + 0.5) * resolution_scale_rcp );
    vec2 frag_coord_center = vec2( frag_coord ) + 0.5;

    // All current frame textures are fullscreen, while history are half size.
    vec2 motion_vector = texelFetch( global_textures[ motion_vectors_texture_index ], ivec2( frag_coord ), 0 ).rg;

    vec2 prev_frag_coord = frag_coord_center + motion_vector * resolution_scale;

    // NOTE(marco): previous sample is outside texture
    if ( any( lessThan( prev_frag_coord, vec2( 0 ) ) ) || any( greaterThanEqual( prev_frag_coord, resolution * resolution_scale  ) ) ) {
        return false;
    }

    uint mesh_id = texelFetch( global_utextures[ mesh_id_texture_index ], scaled_xy, 0 ).r;
    uint prev_mesh_id = texelFetch( global_utextures[ history_mesh_id_texture_index ], ivec2( frag_coord ), 0 ).r;

    if ( mesh_id != prev_mesh_id ) {
        return false;
    }

    vec2 depth_normal_fwidth = texelFetch( global_textures[ depth_normal_fwidth_texture_index ], scaled_xy, 0 ).rg;
    float z = texelFetch( global_textures[ linear_z_dd_texture_index ], scaled_xy, 0 ).r;
    float prev_z = texelFetch( global_textures[ history_linear_depth_texture ], ivec2( prev_frag_coord ), 0 ).r;

    float depth_diff = abs( prev_z - z ) / ( depth_normal_fwidth.x + 1e-2 );

    if ( depth_diff > temporal_depth_difference ) {
        return false;
    }

    vec2 encoded_normal = texelFetch( global_textures[ normals_texture_index ], scaled_xy, 0 ).rg;
    vec3 normal = octahedral_decode( encoded_normal );

    vec2 prev_encoded_normal = texelFetch( global_textures[ history_normals_texture_index ], ivec2( prev_frag_coord ), 0 ).rg;
    vec3 prev_normal = octahedral_decode( prev_encoded_normal );

    float normal_diff = distance( normal, prev_normal ) / ( depth_normal_fwidth.y + 1e-2 );
    if ( normal_diff > temporal_normal_difference ) {
        return false;
    }

    return true;
}

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
    uvec2 frag_coord = gl_GlobalInvocationID.xy;

    vec3 reflections_color = texelFetch( global_textures[ reflections_texture_index ], ivec2( frag_coord ), 0 ).rgb;

    float u_1 = luminance( reflections_color );
    float u_2 = u_1 * u_1;
    vec2 moments = vec2( u_1, u_2 );

    bool is_consistent = check_temporal_consistency( frag_coord );

    vec3 integrated_color_out = vec3( 0 );
    vec2 integrated_moments_out = vec2( 0 );

    if ( is_consistent ) {
        vec3 history_reflections_color = texelFetch( global_textures[ history_reflections_texture_index ], ivec2( frag_coord ), 0 ).rgb;
        vec2 history_moments = texelFetch( global_textures[ history_moments_texture_index ], ivec2( frag_coord ), 0 ).rg;

        float alpha = 0.2;
        integrated_color_out = reflections_color * alpha + ( 1 - alpha ) * history_reflections_color;
        integrated_moments_out = moments * alpha + ( 1 - alpha ) * moments;
    } else {
        integrated_color_out = reflections_color;
        integrated_moments_out = moments;
    }

    imageStore( global_images_2d[ integrated_color_texture_index ], ivec2( frag_coord ), vec4( integrated_color_out, 0 ) );
    imageStore( global_images_2d[ integrated_moments_texture_index ], ivec2( frag_coord ), vec4( integrated_moments_out, 0, 0 ) );
}

#endif // COMPUTE_SVGF_ACCUMULATION

#if defined( COMPUTE_SVGF_VARIANCE )

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
    uvec2 frag_coord = gl_GlobalInvocationID.xy;

    // TODO(marco): if reprojection failed, estimate variance from 7x7 bilateral filter
    vec2 moments = texelFetch( global_textures[ integrated_moments_texture_index ], ivec2( frag_coord ), 0 ).rg;

    float variance = moments.y - pow( moments.x, 2 );

    imageStore( global_images_2d[ variance_texture_index ], ivec2( frag_coord ), vec4( variance, 0, 0, 0 ) );
}

#endif // COMPUTE_SVGF_VARIANCE

#if defined( COMPUTE_SVGF_WAVELET )

float h[ 3 ] = {
    3.0 / 8.0,
    1.0 / 4.0,
    1.0 / 16.0
};

layout( push_constant ) uniform PushConstants {
    uint        step_size;
    float       sigma_z;
    float       sigma_n;
    float       sigma_l;
};

float compute_w( vec3 n_p, vec2 linear_z_dd, float l_p, float l_q, ivec2 p, ivec2 q, float phi_depth ) {

    ivec2 scaled_q = ivec2(q * resolution_scale_rcp );
    // w_n
    // This normal is the gbuffer_normals
    const vec2 encoded_normal_q = texelFetch( global_textures[ normals_texture_index ], scaled_q, 0 ).rg;
    vec3 n_q = octahedral_decode( encoded_normal_q );

    float w_n = pow( max( 0, dot( n_p, n_q ) ), sigma_n );

    // w_z
    // This is the main depth
    float z_q = texelFetch( global_textures[ linear_z_dd_texture_index ], scaled_q, 0 ).r;

    float w_z = exp( -( abs( linear_z_dd.x - z_q ) / ( sigma_z * abs( linear_z_dd.y ) + 1e-5 ) ) );
    // Different filter coming from the falcor implementation, works better.
    w_z = ( phi_depth == 0 ) ? 0.0f : abs( linear_z_dd.x - z_q ) / phi_depth;

    // w_l
    // NOTE(marco): gaussian filter, adapted from Falcor
    // https://github.com/NVIDIAGameWorks/Falcor/blob/master/Source/RenderPasses/SVGFPass/SVGFAtrous.ps.slang
    const float kernel[2][2] = {
        { 1.0 / 4.0, 1.0 / 8.0  },
        { 1.0 / 8.0, 1.0 / 16.0 }
    };

    float g = 0.0;
    const int radius = 1;
    for ( int yy = -radius; yy <= radius; yy++ ) {
        for ( int xx = -radius; xx <= radius; xx++ ) {
            ivec2 s = p + ivec2( xx, yy );

            if ( any( lessThan( s, ivec2( 0 ) ) ) || any( greaterThanEqual( s, ivec2( resolution * resolution_scale) ) ) ) {
                continue;
            }

            float k = kernel[ abs( xx ) ][ abs( yy ) ];
            float v = texelFetch( global_textures[ variance_texture_index ], s, 0 ).r;
            g += v * k;
        }
    }

    float w_l = exp( -( abs( l_p - l_q ) / ( sigma_l * sqrt( max( 0, g ) ) + 1e-5 ) ) );

    // Calculate final weight
    float final_weight = exp( 0.0 - max( w_l, 0.0 ) - max( w_z, 0.0 ) ) * w_n;
    return final_weight;
}

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
    ivec2 frag_coord = ivec2( gl_GlobalInvocationID.xy );

    vec3 new_filtered_color = vec3( 0 );
    float color_weight = 1e-5;

    float new_variance = 0;

    ivec2 scaled_xy = ivec2( frag_coord * resolution_scale_rcp );
    vec2 encoded_normal_p = texelFetch( global_textures[ normals_texture_index ], scaled_xy, 0 ).rg;
    vec3 normal_p = octahedral_decode( encoded_normal_p );
    // In this case this is the depth texture.
    vec2 linear_z_dd = texelFetch( global_textures[ linear_z_dd_texture_index ], scaled_xy, 0 ).rg;
    vec3 color_p = texelFetch( global_textures[ integrated_color_texture_index ], frag_coord, 0 ).rgb;
    float luminance_p = luminance( color_p );

    const int radius = 1;

    const float phi_depth = max(linear_z_dd.y, 1e-8) * step_size;

    for ( int y = -radius; y <= radius; ++y) {
        for( int x = -radius; x <= radius; ++x ) {
            ivec2 offset = ivec2( x, y );
            ivec2 q = frag_coord + ivec2(offset * resolution_scale * step_size);

            if ( any( lessThan( q, ivec2( 0 ) ) ) || any( greaterThanEqual( q, ivec2( resolution * resolution_scale ) ) ) ) {
                continue;
            }

            if ( x == 0 && y == 0 ) {
                continue;
            }

            vec3 c_q = texelFetch( global_textures[ integrated_color_texture_index ], q, 0 ).rgb;
            float l_q = luminance( c_q );
            float h_q = h[ abs( x ) ] * h[ abs( y ) ];

            float w_pq = compute_w( normal_p, linear_z_dd, luminance_p, l_q, frag_coord, q, phi_depth );

            float prev_variance = texelFetch( global_textures[ variance_texture_index ], q, 0 ).r;

            float sample_weight = h_q * w_pq;

            new_filtered_color += sample_weight * c_q;
            color_weight += sample_weight;

            new_variance += pow( h_q, 2 ) * pow( w_pq, 2 ) * prev_variance;
        }
    }

    new_filtered_color /= color_weight;
    new_variance /= pow( color_weight, 2 );

    imageStore( global_images_2d[ filtered_color_texture_index ], frag_coord, vec4( new_filtered_color, 0 ) );
    imageStore( global_images_2d[ updated_variance_texture_index ], frag_coord, vec4( new_variance, 0, 0, 0 ) );
}

#endif // COMPUTE_SVGF_WAVELET

#if defined(COMPUTE_BRDF_LUT_GENERATION)

float radical_inverse_vdc(uint bits) {

    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radical_inverse_vdc(i));
}

vec3 importance_sample_ggx(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    
    float phi = 2.0 * PI * Xi.x;
    float cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
    
    vec3 H;
    H.x = cos(phi) * sin_theta;
    H.y = sin(phi) * sin_theta;
    H.z = cos_theta;

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sample_vec = H.x * tangent + H.y * bitangent + H.z * N;
    return normalize(sample_vec);
}

float geometry_schlick_ggx(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0f;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometry_schlick_ggx(NdotV, roughness);
    float ggx1 = geometry_schlick_ggx(NdotL, roughness);
    return ggx1 * ggx2;
}

vec2 integrate_brdf(float NdotV, float roughness) {
    vec3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0f;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;
    
    vec3 N = vec3(0.0, 0.0, 1.0);
    
    const uint sample_count = 1024u;
    for(uint i = 0u; i < sample_count; ++i) {
        vec2 Xi = hammersley(i, sample_count);
        vec3 H = importance_sample_ggx(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);
        
        if(NdotL > 0.0)
        {
            float G = geometry_smith(N, V, L, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(sample_count);
    B /= float(sample_count);
    return vec2(A, B);
}

layout( push_constant ) uniform PushConstants {
    uint        output_texture_index;
    uint        output_texture_size;
};

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
    ivec2 frag_coord = ivec2( gl_GlobalInvocationID.xy );
    vec2 uv = uv_nearest( frag_coord, vec2(output_texture_size) );
    vec2 integrated_brdf = integrate_brdf( uv.x, 1 - uv.y );
    imageStore( global_images_2d[ output_texture_index ], frag_coord, vec4( integrated_brdf, 0, 0 ) );
}

#endif // COMPUTE_BRDF_LUT_GENERATION

#if defined(COMPUTE_SVGF_DOWNSAMPLE)

ivec2 pixel_offsets[] = ivec2[]( ivec2(0,0), ivec2(0,1), ivec2(1,0), ivec2(1,1));

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
    ivec2 frag_coord = ivec2( gl_GlobalInvocationID.xy );

    int chosen_hiresolution_sample_index = 0;
    float closer_depth = 0.f;
    for ( int i = 0; i < 4; ++i ) {

        float depth = texelFetch(global_textures[nonuniformEXT(depth_texture_index)], (frag_coord.xy) * 2 + pixel_offsets[i], 0).r;

        if ( closer_depth < depth ) {
            closer_depth = depth;
            chosen_hiresolution_sample_index = i;
        }
    }

    // Write the most representative sample of all the textures
    vec4 normals = texelFetch(global_textures[nonuniformEXT(normals_texture_index)], (frag_coord.xy) * 2 + pixel_offsets[chosen_hiresolution_sample_index], 0);
    imageStore( global_images_2d[ history_normals_texture_index ], frag_coord, normals );

    vec4 mesh_id = texelFetch(global_textures[nonuniformEXT(mesh_id_texture_index)], (frag_coord.xy) * 2 + pixel_offsets[chosen_hiresolution_sample_index], 0);
    imageStore( global_images_2d[ history_mesh_id_texture_index ], frag_coord, mesh_id );

    vec4 linear_z_dd = texelFetch(global_textures[nonuniformEXT(linear_z_dd_texture_index)], (frag_coord.xy) * 2 + pixel_offsets[chosen_hiresolution_sample_index], 0);
    imageStore( global_images_2d[ history_linear_depth_texture ], frag_coord, linear_z_dd );

    vec4 moments = texelFetch(global_textures[nonuniformEXT(integrated_moments_texture_index)], (frag_coord.xy), 0);
    imageStore( global_images_2d[ history_moments_texture_index ], frag_coord, moments );
}

#endif // COMPUTE_SVGF_DOWNSAMPLE
