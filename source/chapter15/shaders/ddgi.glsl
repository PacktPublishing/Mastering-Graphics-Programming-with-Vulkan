
// Common Raytracing code ////////////////////////////////////////////////
#if defined (RAYGEN_PROBE_RT) || defined (CLOSEST_HIT_PROBE_RT) || defined (MISS_PROBE_RT)

struct RayPayload {
    vec3        radiance;
    float       distance;
};

#extension GL_EXT_ray_tracing : enable

layout( set = MATERIAL_SET, binding = 26 ) uniform accelerationStructureEXT as;

// TODO: put this in a common header.
struct Light {
    vec3            world_position;
    float           radius;

    vec3            color;
    float           intensity;

    float           shadow_map_resolution;
    float           padding_l_000;
    float           padding_l_001;
    float           padding_l_002;
};

layout( set = MATERIAL_SET, binding = 27 ) readonly buffer Lights {
    Light           lights[];
};

layout(std430, set = MATERIAL_SET, binding = 43) readonly buffer ProbeStatusSSBO {
    uint        probe_status[];
};

#endif

#if defined (RAYGEN_PROBE_RT)

layout( location = 0 ) rayPayloadEXT RayPayload payload;

void main() {
	const ivec2 pixel_coord = ivec2(gl_LaunchIDEXT.xy);
    const int probe_index = pixel_coord.y + probe_update_offset;
    const int ray_index = pixel_coord.x;

    const bool skip_probe = (probe_status[probe_index] == PROBE_STATUS_OFF) || (probe_status[probe_index] == PROBE_STATUS_UNINITIALIZED);
    if ( use_probe_status() && skip_probe ) {
        return;
    }

    const int probe_counts = probe_counts.x * probe_counts.y * probe_counts.z;
    if (probe_index >= probe_counts) {
        return;
    }

    ivec3 probe_grid_indices = probe_index_to_grid_indices( probe_index );
    vec3 ray_origin = grid_indices_to_world( probe_grid_indices, probe_index );
    vec3 direction = normalize( mat3(random_rotation) * spherical_fibonacci(ray_index, probe_rays) );
    payload.radiance = vec3(0);
    payload.distance = 0;

    traceRayEXT(as, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, ray_origin, 0.0, direction, 100.0, 0);

    imageStore(global_images_2d[ radiance_output_index ], ivec2(ray_index, probe_index), vec4(payload.radiance, payload.distance));
}

#endif

#if defined (CLOSEST_HIT_PROBE_RT)

layout( location = 0 ) rayPayloadInEXT RayPayload payload;
hitAttributeEXT vec2 barycentric_weights;

// todo: move this somewhere else.
float attenuation_square_falloff(vec3 position_to_light, float light_inverse_radius) {
    const float distance_square = dot(position_to_light, position_to_light);
    const float factor = distance_square * light_inverse_radius * light_inverse_radius;
    const float smoothFactor = max(1.0 - factor * factor, 0.0);
    return (smoothFactor * smoothFactor) / max(distance_square, 1e-4);
}

void main() {

    vec3 radiance = vec3(0);
    float distance = 0.0f;
    if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT) {
        // Track backfacing rays with negative distance
        distance = gl_RayTminEXT + gl_HitTEXT;
        distance *= -0.2;        
    }
    else {
        uint mesh_index = mesh_instance_draws[ gl_GeometryIndexEXT ].mesh_draw_index;
        MeshDraw mesh = mesh_draws[ mesh_index ];

        int_array_type index_buffer = int_array_type( mesh.index_buffer );
        int i0 = index_buffer[ gl_PrimitiveID * 3 ].v;
        int i1 = index_buffer[ gl_PrimitiveID * 3 + 1 ].v;
        int i2 = index_buffer[ gl_PrimitiveID * 3 + 2 ].v;

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

        const mat4 transform = mesh_instance_draws[ gl_GeometryIndexEXT ].model;
        vec4 p0_world = transform * p0;
        vec4 p1_world = transform * p1;
        vec4 p2_world = transform * p2;

        // float_array_type uv_buffer = float_array_type( mesh.uv_buffer );
        // vec2 uv0 = vec2(uv_buffer[ i0 * 2 ].v, uv_buffer[ i0 * 2 + 1].v);
        // vec2 uv1 = vec2(uv_buffer[ i1 * 2 ].v, uv_buffer[ i1 * 2 + 1].v);
        // vec2 uv2 = vec2(uv_buffer[ i2 * 2 ].v, uv_buffer[ i2 * 2 + 1].v);

        vec2_array_type uv_buffer = vec2_array_type( mesh.uv_buffer );
        vec2 uv0 = uv_buffer[ i0 ].v;
        vec2 uv1 = uv_buffer[ i1 ].v;
        vec2 uv2 = uv_buffer[ i2 ].v;

        float b = barycentric_weights.x;
        float c = barycentric_weights.y;
        float a = 1 - b - c;

        vec2 uv = ( a * uv0 + b * uv1 + c * uv2 );

        // Use lower texture Lod to increase performances.
        vec3 albedo = textureLod( global_textures[ nonuniformEXT( mesh.textures.x ) ], uv, 3 ).rgb;

        // Compute plane normal
        // vec3 v0_v1 = p1_world.xyz - p0_world.xyz;
        // vec3 v0_v2 = p2_world.xyz - p0_world.xyz;
        // vec3 normal = normalize(cross( v0_v1, v0_v2 ));

        float_array_type normals_buffer = float_array_type( mesh.normals_buffer );
        vec3 n0 = vec3(normals_buffer[ i0 * 3 + 0 ].v,
                       normals_buffer[ i0 * 3 + 1 ].v,
                       normals_buffer[ i0 * 3 + 2 ].v );

        vec3 n1 = vec3(normals_buffer[ i1 * 3 + 0 ].v,
                       normals_buffer[ i1 * 3 + 1 ].v,
                       normals_buffer[ i1 * 3 + 2 ].v );

        vec3 n2 = vec3(normals_buffer[ i2 * 3 + 0 ].v,
                       normals_buffer[ i2 * 3 + 1 ].v,
                       normals_buffer[ i2 * 3 + 2 ].v );

        vec3 normal = a * n0 + b * n1 + c * n2;

        const mat3 normal_transform = mat3(mesh_instance_draws[ gl_GeometryIndexEXT ].model_inverse);
        normal = normal_transform * normal;

        const vec3 world_position = a * p0_world.xyz + b * p1_world.xyz + c * p2_world.xyz;

        // TODO: calculate lighting.
        Light light = lights[ 0 ];

        const vec3 position_to_light = light.world_position - world_position;
        const vec3 l = normalize( position_to_light );
        const float NoL = clamp(dot(normal, l), 0.0, 1.0);

        float attenuation = attenuation_square_falloff(position_to_light, 1.0f / light.radius);

        vec3 light_intensity = vec3(0.0f);
        if ( attenuation > 0.001f && NoL > 0.001f ) {
            light_intensity += ( light.intensity * attenuation * NoL ) * light.color;
        }

        vec3 diffuse = albedo * light_intensity;

        // infinite bounces
        if ( use_infinite_bounces() ) {
            diffuse += albedo * sample_irradiance( world_position, normal, camera_position.xyz ) * infinite_bounces_multiplier;
        }

        //payload = vec4( barycentric_weights, 0.0, 1.0 );
        radiance = diffuse;
        distance = gl_RayTminEXT + gl_HitTEXT;
    }

    payload.radiance = radiance;
    payload.distance = distance;
}

#endif

#if defined (MISS_PROBE_RT)

layout( location = 0 ) rayPayloadInEXT RayPayload payload;

void main() {
	payload.radiance = vec3( 0.529, 0.807, 0.921 );
    payload.distance = 1000.0f;
}

#endif


#if defined(COMPUTE_PROBE_UPDATE_IRRADIANCE) || defined(COMPUTE_PROBE_UPDATE_VISIBILITY)

layout(rgba16f, set = MATERIAL_SET, binding = 41) uniform image2D irradiance_image;
layout(rg16f, set = MATERIAL_SET, binding = 42) uniform image2D visibility_image;

layout(std430, set = MATERIAL_SET, binding = 43) readonly buffer ProbeStatusSSBO {
    uint        probe_status[];
};


#define EPSILON 0.0001f

int k_read_table[6] = {5, 3, 1, -1, -3, -5};

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {

    ivec3 coords = ivec3(gl_GlobalInvocationID.xyz);

#if defined(COMPUTE_PROBE_UPDATE_IRRADIANCE)
    int probe_texture_width = irradiance_texture_width;
    int probe_texture_height = irradiance_texture_height;
    int probe_side_length = irradiance_side_length;
#else
    int probe_texture_width = visibility_texture_width;
    int probe_texture_height = visibility_texture_height;
    int probe_side_length = visibility_side_length;
#endif // COMPUTE_PROBE_UPDATE_IRRADIANCE

    // Early out for 1 pixel border around all image and outside bound pixels.
    if (coords.x >= probe_texture_width || coords.y >= probe_texture_height) {
        return;
    }

    const uint probe_with_border_side = probe_side_length + 2;
    const uint probe_last_pixel = probe_side_length + 1;

    int probe_index = get_probe_index_from_pixels(coords.xy, int(probe_with_border_side), probe_texture_width);

    // Check if thread is a border pixel
    bool border_pixel = ((gl_GlobalInvocationID.x % probe_with_border_side) == 0) || ((gl_GlobalInvocationID.x % probe_with_border_side ) == probe_last_pixel );
    border_pixel = border_pixel || ((gl_GlobalInvocationID.y % probe_with_border_side) == 0) || ((gl_GlobalInvocationID.y % probe_with_border_side ) == probe_last_pixel );

    // Perform full calculations
    if ( !border_pixel ) {
        vec4 result = vec4(0);

        const float energy_conservation = 0.95;

        uint backfaces = 0;
        uint max_backfaces = uint(probe_rays * 0.1f);

        for ( int ray_index = 0; ray_index < probe_rays; ++ray_index ) {
            ivec2 sample_position = ivec2( ray_index, probe_index );

            vec3 ray_direction = normalize( mat3(random_rotation) * spherical_fibonacci(ray_index, probe_rays) );

            vec3 texel_direction = oct_decode(normalized_oct_coord(coords.xy, probe_side_length));

            float weight = max(0.0, dot(texel_direction, ray_direction));

            float distance2 = texelFetch(global_textures[nonuniformEXT(radiance_output_index)], sample_position, 0).w;
            if ( distance2 < 0.0f && use_backfacing_blending() ) {
                ++backfaces;

                // Early out: only blend ray radiance into the probe if the backface threshold hasn't been exceeded
                if (backfaces >= max_backfaces)
                    return;

                continue;
            }

#if defined(COMPUTE_PROBE_UPDATE_IRRADIANCE)
            if (weight >= EPSILON) {
                vec3 radiance = texelFetch(global_textures[nonuniformEXT(radiance_output_index)], sample_position, 0).rgb;
                radiance.rgb *= energy_conservation;

                // Storing the sum of the weights in alpha temporarily
                result += vec4(radiance * weight, weight);
            }
#else
            // TODO: spacing is 1.0f
            float probe_max_ray_distance = 1.0f * 1.5f;

            // Increase or decrease the filtered distance value's "sharpness"
            weight = pow(weight, 2.5f);

            if (weight >= EPSILON) {
                float distance = texelFetch(global_textures[nonuniformEXT(radiance_output_index)], sample_position, 0).w;
                // Limit
                distance = min(abs(distance), probe_max_ray_distance);
                vec3 value = vec3(distance, distance * distance, 0);
                // Storing the sum of the weights in alpha temporarily
                result += vec4(value * weight, weight);
            }
#endif
        }

        if (result.w > EPSILON) {
            result.xyz /= result.w;
            result.w = 1.0f;
        }

        // Read previous frame value
#if defined(COMPUTE_PROBE_UPDATE_IRRADIANCE)
        vec4 previous_value = imageLoad( irradiance_image, coords.xy );
#else
        vec2 previous_value = imageLoad( visibility_image, coords.xy ).rg;
#endif

        // Debug inside with color green
        if (show_border_vs_inside()) {
            result = vec4(0,1,0,1);
        }

#if defined(COMPUTE_PROBE_UPDATE_IRRADIANCE)
        if ( use_perceptual_encoding() ) {
            result.rgb = pow(result.rgb, vec3(1.0f / 5.0f));    
        }

        result = mix( result, previous_value, hysteresis );
        imageStore(irradiance_image, coords.xy, result);
#else
        result.rg = mix( result.rg, previous_value, hysteresis );
        imageStore(visibility_image, coords.xy, vec4(result.rg, 0, 1));
#endif

        // NOTE: returning here.
        return;
    }

    // Wait for all local threads to have finished to copy the border pixels.
    groupMemoryBarrier();
    barrier();

    // Copy border pixel calculating source pixels.
    const uint probe_pixel_x = gl_GlobalInvocationID.x % probe_with_border_side;
    const uint probe_pixel_y = gl_GlobalInvocationID.y % probe_with_border_side;
    bool corner_pixel = (probe_pixel_x == 0 || probe_pixel_x == probe_last_pixel) &&
                        (probe_pixel_y == 0 || probe_pixel_y == probe_last_pixel);
    bool row_pixel = (probe_pixel_x > 0 && probe_pixel_x < probe_last_pixel);

    ivec2 source_pixel_coordinate = coords.xy;

    if ( corner_pixel ) {
        source_pixel_coordinate.x += probe_pixel_x == 0 ? probe_side_length : -probe_side_length;
        source_pixel_coordinate.y += probe_pixel_y == 0 ? probe_side_length : -probe_side_length;

        if (show_border_type()) {
            source_pixel_coordinate = ivec2(2,2);
        }
    }
    else if ( row_pixel ) {
        source_pixel_coordinate.x += k_read_table[probe_pixel_x - 1];
        source_pixel_coordinate.y += (probe_pixel_y > 0) ? -1 : 1;

        if (show_border_type()) {
            source_pixel_coordinate = ivec2(3,3);
        }
    }
    else {
        source_pixel_coordinate.x += (probe_pixel_x > 0) ? -1 : 1;
        source_pixel_coordinate.y += k_read_table[probe_pixel_y - 1];

        if (show_border_type()) {
            source_pixel_coordinate = ivec2(4,4);
        }
    }

#if defined(COMPUTE_PROBE_UPDATE_IRRADIANCE)
    vec4 copied_data = imageLoad( irradiance_image, source_pixel_coordinate );
#else
    vec4 copied_data = imageLoad( visibility_image, source_pixel_coordinate );
#endif

    // Debug border source coordinates
    if ( show_border_source_coordinates() ) {
        copied_data = vec4(coords.xy, source_pixel_coordinate);
    }

    // Debug border with color red
    if (show_border_vs_inside()) {
        copied_data = vec4(1,0,0,1);
    }

#if defined(COMPUTE_PROBE_UPDATE_IRRADIANCE)
    imageStore( irradiance_image, coords.xy, copied_data );
#else
    imageStore( visibility_image, coords.xy, copied_data );
#endif
}

#endif // COMPUTE_PROBE_UPDATE


#if defined(COMPUTE_CALCULATE_PROBE_OFFSETS) || defined(COMPUTE_CALCULATE_PROBE_STATUSES)

layout( push_constant ) uniform PushConstants {
    uint        first_frame;
};

layout(std430, set = MATERIAL_SET, binding = 43) buffer ProbeStatusSSBO {
    uint        probe_status[];
};

#endif // COMPUTE_CALCULATE_PROBE_OFFSETS || COMPUTE_CALCULATE_PROBE_STATUSES

#if defined(COMPUTE_CALCULATE_PROBE_OFFSETS)

layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main() {

    ivec3 coords = ivec3(gl_GlobalInvocationID.xyz);

    // Invoke this shader for each probe
    int probe_index = coords.x;
    const int total_probes = probe_counts.x * probe_counts.y * probe_counts.z;
    // Early out if index is not valid
    if (probe_index >= total_probes) {
        return;
    }

    int closest_backface_index = -1;
    float closest_backface_distance = 100000000.f;

    int closest_frontface_index = -1;
    float closest_frontface_distance = 100000000.f;

    int farthest_frontface_index = -1;
    float farthest_frontface_distance = 0;

    int backfaces_count = 0;
    // For each ray cache front/backfaces index and distances.
    for (int ray_index = 0; ray_index < probe_rays; ++ray_index) {

        ivec2 ray_tex_coord = ivec2(ray_index, probe_index);

        float ray_distance = texelFetch(global_textures[nonuniformEXT(radiance_output_index)], ray_tex_coord, 0).w;
        // Negative distance is stored for backface hits in the Ray-Tracing Hit shader.
        if ( ray_distance <= 0.0f ) {
            ++backfaces_count;
            // Distance is a positive value, thus negate ray_distance as it is negative already if
            // we are inside this branch.
            if ( (-ray_distance) < closest_backface_distance ) {
                closest_backface_distance = ray_distance;
                closest_backface_index = ray_index;
            }
        }
        else {
            // Cache either closest or farther distance and indices for this ray.
            if (ray_distance < closest_frontface_distance) {
                closest_frontface_distance = ray_distance;
                closest_frontface_index = ray_index;
            } else if (ray_distance > farthest_frontface_distance) {
                farthest_frontface_distance = ray_distance;
                farthest_frontface_index = ray_index;
            }
        }
    }

    vec3 full_offset = vec3(10000.f);
    vec3 cell_offset_limit = max_probe_offset * probe_spacing;

    vec4 current_offset = vec4(0);
    // Read previous offset after the first frame.
    if ( first_frame == 0 ) {
        const int probe_counts_xy = probe_counts.x * probe_counts.y;
        ivec2 probe_offset_sampling_coordinates = ivec2(probe_index % probe_counts_xy, probe_index / probe_counts_xy);
        current_offset.rgb = texelFetch(global_textures[nonuniformEXT(probe_offset_texture_index)], probe_offset_sampling_coordinates, 0).rgb;
    }

    // Check if a fourth of the rays was a backface, we can assume the probe is inside a geometry.
    const bool inside_geometry = (float(backfaces_count) / probe_rays) > 0.25f;
    if (inside_geometry && (closest_backface_index != -1)) {
        // Calculate the backface direction.
        // NOTE: Distance is always positive
        const vec3 closest_backface_direction = closest_backface_distance * normalize( mat3(random_rotation) * spherical_fibonacci(closest_backface_index, probe_rays) );
        
        // Find the maximum offset inside the cell.
        const vec3 positive_offset = (current_offset.xyz + cell_offset_limit) / closest_backface_direction;
        const vec3 negative_offset = (current_offset.xyz - cell_offset_limit) / closest_backface_direction;
        const vec3 maximum_offset = vec3(max(positive_offset.x, negative_offset.x), max(positive_offset.y, negative_offset.y), max(positive_offset.z, negative_offset.z));

        // Get the smallest of the offsets to scale the direction
        const float direction_scale_factor = min(min(maximum_offset.x, maximum_offset.y), maximum_offset.z) - 0.001f;

        // Move the offset in the opposite direction of the backface one.
        full_offset = current_offset.xyz - closest_backface_direction * direction_scale_factor;
    }
    else if (closest_frontface_distance < 0.05f) {
        // In this case we have a very small hit distance.

        // Ensure that we never move through the farthest frontface
        // Move minimum distance to ensure not moving on a future iteration.
        const vec3 farthest_direction = min(0.2f, farthest_frontface_distance) * normalize( mat3(random_rotation) * spherical_fibonacci(farthest_frontface_index, probe_rays) );
        const vec3 closest_direction = normalize(mat3(random_rotation) * spherical_fibonacci(closest_frontface_index, probe_rays));
        // The farthest frontface may also be the closest if the probe can only 
        // see one surface. If this is the case, don't move the probe.
        if (dot(farthest_direction, closest_direction) < 0.5f) {
            full_offset = current_offset.xyz + farthest_direction;
        }
    } 

    // Move the probe only if the newly calculated offset is within the cell.
    if (all(lessThan(abs(full_offset), cell_offset_limit))) {
        current_offset.xyz = full_offset;
    }

    // Write probe offset
    const int probe_counts_xy = probe_counts.x * probe_counts.y;

    const int probe_texel_x = (probe_index % probe_counts_xy);
    const int probe_texel_y = probe_index / probe_counts_xy;

    imageStore(global_images_2d[ probe_offset_texture_index ], ivec2(probe_texel_x, probe_texel_y), current_offset);
}

#endif // COMPUTE_CALCULATE_PROBE_OFFSETS


#if defined(COMPUTE_CALCULATE_PROBE_STATUSES)

layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
void main() {

    ivec3 coords = ivec3(gl_GlobalInvocationID.xyz);

    int offset = 0;

    int probe_index = coords.x;

    int closest_backface_index = -1;
    float closest_backface_distance = 100000000.f;

    int closest_frontface_index = -1;
    float closest_frontface_distance = 100000000.f;

    int farthest_frontface_index = -1;
    float farthest_frontface_distance = 0;

    int backfaces_count = 0;
    uint flag = first_frame == 1 ? PROBE_STATUS_UNINITIALIZED : probe_status[probe_index];

    // Worst case, view and normal contribute in the same direction, so need 2x self-shadow bias.
    vec3 outerBounds = normalize(probe_spacing) * (length(probe_spacing) + (2.0f * self_shadow_bias));

    for (int ray_index = 0; ray_index < probe_rays; ++ray_index) {

        ivec2 ray_tex_coord = ivec2(ray_index, probe_index);

        // Distance is negative if we hit a backface
        float d_front = texelFetch(global_textures[nonuniformEXT(radiance_output_index)], ray_tex_coord, 0).w;
        float d_back = -d_front;

        //Backface test backface -> position.w < 0.0f
        if (d_back > 0.0f) {
            backfaces_count += 1;
            if (d_back < closest_backface_distance) {
                // This distance is negative on a backface hit
                closest_backface_distance = d_back;
                // Recompute ray direction
                closest_backface_index = ray_index;
            }
        }

        if (d_front > 0.0f) {
            // Need to check all frontfaces to see if any are wihtin shading range.
            vec3 frontFaceDirection = d_front * normalize( mat3(random_rotation) * spherical_fibonacci(ray_index, probe_rays) );
            if (all(lessThan(abs(frontFaceDirection), outerBounds))) {
                // There is a static surface being shaded by this probe. Make it "just vigilant".
                flag = PROBE_STATUS_ACTIVE;
            }
            if (d_front < closest_frontface_distance) {
                closest_frontface_distance = d_front;
                closest_frontface_index = ray_index;
            } else if (d_front > farthest_frontface_distance) {
                farthest_frontface_distance = d_front;
                farthest_frontface_index = ray_index;
            }
        }
    }

    // If there's a close backface AND you see more than 25% backfaces, assume you're inside something.
    if (closest_backface_index != -1 && (float(backfaces_count) / probe_rays) > 0.25f) {
        // At this point, we were just in a wall, so set probe to "Off".
        flag = PROBE_STATUS_OFF;
    }
    else if (closest_frontface_index == -1) {
        // Probe sees only backfaces and sky, so set probe to "Off".
       flag = PROBE_STATUS_OFF;
    }
    else if (closest_frontface_distance < 0.05f) {
        // We hit no backfaces and a close frontface (within 2 cm). Set to "Newly Vigilant".
        flag = PROBE_STATUS_ACTIVE;
    } 

    // Write probe status
    probe_status[probe_index] = flag;
}

#endif // COMPUTE_CALCULATE_PROBE_STATUSES


#if defined(COMPUTE_SAMPLE_IRRADIANCE)

layout(std430, set = MATERIAL_SET, binding = 43) readonly buffer ProbeStatusSSBO {
    uint        probe_status[];
};

layout( push_constant ) uniform PushConstants {
    uint        output_resolution_half;
};

ivec2 pixel_offsets[] = ivec2[]( ivec2(0,0), ivec2(0,1), ivec2(1,0), ivec2(1,1));

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {

    ivec3 coords = ivec3(gl_GlobalInvocationID.xyz);

    int resolution_divider = output_resolution_half == 1 ? 2 : 1;
    vec2 screen_uv = uv_nearest(coords.xy, resolution / resolution_divider);
    
    float raw_depth = 1.0f;
    int chosen_hiresolution_sample_index = 0;
    if (output_resolution_half == 1) {
        float closer_depth = 0.f;
        for ( int i = 0; i < 4; ++i ) {

            float depth = texelFetch(global_textures[nonuniformEXT(depth_fullscreen_texture_index)], (coords.xy) * 2 + pixel_offsets[i], 0).r;

            if ( closer_depth < depth ) {
                closer_depth = depth;
                chosen_hiresolution_sample_index = i;
            }
        }

        raw_depth = closer_depth;
    }
    else {
        raw_depth = texelFetch(global_textures[nonuniformEXT(depth_fullscreen_texture_index)], coords.xy, 0).r;
    }

    if ( raw_depth == 1.0f ) {
        imageStore(global_images_2d[ indirect_output_index ], coords.xy, vec4(0,0,0,1));
        return;
    }

    // Manually fetch normals when in low resolution.
    vec3 normal = vec3(0);

    if (output_resolution_half == 1) {
        vec2 encoded_normal = texelFetch(global_textures[nonuniformEXT(normal_texture_index)], (coords.xy) * 2 + pixel_offsets[chosen_hiresolution_sample_index], 0).rg;
        normal = normalize(octahedral_decode(encoded_normal));
    }
    else {
        vec2 encoded_normal = texelFetch(global_textures[nonuniformEXT(normal_texture_index)], coords.xy, 0).rg;
        normal = octahedral_decode(encoded_normal);
    }

    const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);
    
    vec3 irradiance = sample_irradiance( pixel_world_position, normal, camera_position.xyz );

    imageStore(global_images_2d[ indirect_output_index ], coords.xy, vec4(irradiance,1));
}

#endif // COMPUTE_SAMPLE_IRRADIANCE


#if defined(VERTEX_DEBUG_MESH)

layout(location = 0) in vec3 position;

layout(location = 0) flat out int probe_index;
layout(location = 1) out vec4 normal_edge_factor;
layout(location = 2) flat out uint probe_status;

layout(std430, set = MATERIAL_SET, binding = 43) readonly buffer ProbeStatusSSBO {
    uint        probe_statuses[];
};

void main() {
    probe_index = gl_InstanceIndex;
    probe_status = probe_statuses[ probe_index ];

    const ivec3 probe_grid_indices = probe_index_to_grid_indices(int(probe_index));
    const vec3 probe_position = grid_indices_to_world( probe_grid_indices, probe_index );
    gl_Position = view_projection * vec4( (position * probe_sphere_scale) + probe_position, 1.0 );

    // We can calculate the normal simply by taking the object space position
    // and subtracting the center that is (0,0,0) and normalize.
    normal_edge_factor.xyz = normalize( position );

    normal_edge_factor.w = abs(dot(normal_edge_factor.xyz, normalize(probe_position - camera_position.xyz)));
}

#endif // VERTEX


#if defined (FRAGMENT_DEBUG_MESH)

layout (location = 0) flat in int probe_index;
layout (location = 1) in vec4 normal_edge_factor;
layout(location = 2) flat in uint probe_status;

layout (location = 0) out vec4 color;

void main() {

    vec2 uv = get_probe_uv(normal_edge_factor.xyz, probe_index, irradiance_texture_width, irradiance_texture_height, irradiance_side_length);

    vec3 irradiance = textureLod(global_textures[nonuniformEXT(grid_irradiance_output_index)], uv, 0).rgb;

    if ( use_perceptual_encoding() ) {
        irradiance = pow(irradiance, vec3(0.5f * 5.0f));
        irradiance = irradiance * irradiance;
    }

    if ( normal_edge_factor.w < 0.55f ) {
        if (probe_status == PROBE_STATUS_OFF) {
            irradiance = vec3(1,0,0);
        }
        else if (probe_status == PROBE_STATUS_UNINITIALIZED) {
            irradiance = vec3(0,0,1);
        }
        else if (probe_status == PROBE_STATUS_ACTIVE) {
            irradiance = vec3(0,1,0);
        }
        else if (probe_status == PROBE_STATUS_SLEEP) {
            irradiance = vec3(0,0,0);
        }
        else {
            irradiance = vec3(1,1,1);
        }
    }

    color = vec4(irradiance, 1.0f);
    //color.rgb = vec3(uv, 0);
}

#endif // FRAGMENT_DEBUG_MESH


