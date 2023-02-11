
#ifndef RAPTOR_GLSL_DDGI_H
#define RAPTOR_GLSL_DDGI_H

layout ( std140, set = MATERIAL_SET, binding = 55 ) uniform DDGIConstants {
    uint        radiance_output_index;
    uint        grid_irradiance_output_index;
    uint        indirect_output_index;
    uint        normal_texture_index;

    uint        depth_pyramid_texture_index;
    uint        depth_fullscreen_texture_index;
    uint        grid_visibility_texture_index;
    uint        probe_offset_texture_index;

    float       hysteresis;
    float       infinite_bounces_multiplier;
    int         probe_update_offset;
    int         probe_update_count;

    vec3        probe_grid_position;
    float       probe_sphere_scale;

    vec3        probe_spacing;
    float       max_probe_offset;   // [0,0.5] max offset for probes

    vec3        reciprocal_probe_spacing;
    float       self_shadow_bias;

    ivec3       probe_counts;
    uint        ddgi_debug_options;

    int         irradiance_texture_width;
    int         irradiance_texture_height;
    int         irradiance_side_length;
    int         probe_rays;

    int         visibility_texture_width;
    int         visibility_texture_height;
    int         visibility_side_length;
    int         pad003_ddgic;

    mat4        random_rotation;
};

// Debug options /////////////////////////////////////////////////////////
bool show_border_vs_inside() {
    return (ddgi_debug_options & 1) == 1;
}

bool show_border_type() {
    return (ddgi_debug_options & 2) == 2;
}

bool show_border_source_coordinates() {
    return (ddgi_debug_options & 4) == 4;
}

bool use_visibility() {
    return (ddgi_debug_options & 8) == 8;
}

bool use_smooth_backface() {
    return (ddgi_debug_options & 16) == 16;
}

bool use_perceptual_encoding() {
    return (ddgi_debug_options & 32) == 32;
}

bool use_backfacing_blending() {
    return (ddgi_debug_options & 64) == 64;
}

bool use_probe_offsetting() {
    return (ddgi_debug_options & 128) == 128;
}

bool use_probe_status() {
    return (ddgi_debug_options & 256) == 256;
}

bool use_infinite_bounces() {
    return (ddgi_debug_options & 512) == 512;
}

// Probe status //////////////////////////////////////////////////////////
#define PROBE_STATUS_OFF 0
#define PROBE_STATUS_SLEEP 1
#define PROBE_STATUS_ACTIVE 4
#define PROBE_STATUS_UNINITIALIZED 6

// Utility methods ///////////////////////////////////////////////////////
vec3 spherical_fibonacci(float i, float n) {
    const float PHI = sqrt(5.0f) * 0.5 + 0.5;
#define madfrac(A, B) ((A) * (B)-floor((A) * (B)))
    float phi       = 2.0 * PI * madfrac(i, PHI - 1);
    float cos_theta = 1.0 - (2.0 * i + 1.0) * (1.0 / n);
    float sin_theta = sqrt(clamp(1.0 - cos_theta * cos_theta, 0.0f, 1.0f));

    return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);

#undef madfrac
}


float sign_not_zero(in float k) {
    return (k >= 0.0) ? 1.0 : -1.0;
}

vec2 sign_not_zero2(in vec2 v) {
    return vec2(sign_not_zero(v.x), sign_not_zero(v.y));
}

// Assumes that v is a unit vector. The result is an octahedral vector on the [-1, +1] square.
vec2 oct_encode(in vec3 v) {
    float l1norm = abs(v.x) + abs(v.y) + abs(v.z);
    vec2 result = v.xy * (1.0 / l1norm);
    if (v.z < 0.0) {
        result = (1.0 - abs(result.yx)) * sign_not_zero(result.xy);
    }
    return result;
}


// Returns a unit vector. Argument o is an octahedral vector packed via oct_encode,
// on the [-1, +1] square
vec3 oct_decode(vec2 o) {
    vec3 v = vec3(o.x, o.y, 1.0 - abs(o.x) - abs(o.y));
    if (v.z < 0.0) {
        v.xy = (1.0 - abs(v.yx)) * sign_not_zero2(v.xy);
    }
    return normalize(v);
}

// Compute normalized oct coord, mapping top left of top left pixel to (-1,-1) and bottom right to (1,1)
vec2 normalized_oct_coord(ivec2 fragCoord, int probe_side_length) {

    int probe_with_border_side = probe_side_length + 2;
    vec2 octahedral_texel_coordinates = ivec2((fragCoord.x - 1) % probe_with_border_side, (fragCoord.y - 1) % probe_with_border_side);

    octahedral_texel_coordinates += vec2(0.5f);
    octahedral_texel_coordinates *= (2.0f / float(probe_side_length));
    octahedral_texel_coordinates -= vec2(1.0f);

    return octahedral_texel_coordinates;
}

vec2 get_probe_uv(vec3 direction, int probe_index, int full_texture_width, int full_texture_height, int probe_side_length) {

    // Get octahedral coordinates (-1,1)
    const vec2 octahedral_coordinates = oct_encode(normalize(direction));
    // TODO: use probe index for this.
    const float probe_with_border_side = float(probe_side_length) + 2.0f;
    const int probes_per_row = (full_texture_width) / int(probe_with_border_side);
    // Get probe indices in the atlas
    ivec2 probe_indices = ivec2((probe_index % probes_per_row), 
                               (probe_index / probes_per_row));
    
    // Get top left atlas texels
    vec2 atlas_texels = vec2( probe_indices.x * probe_with_border_side, probe_indices.y * probe_with_border_side );
    // Account for 1 pixel border
    atlas_texels += vec2(1.0f);
    // Move to center of the probe area
    atlas_texels += vec2(probe_side_length * 0.5f);
    // Use octahedral coordinates (-1,1) to move between internal pixels, no border
    atlas_texels += octahedral_coordinates * (probe_side_length * 0.5f);
    // Calculate final uvs
    const vec2 uv = atlas_texels / vec2(float(full_texture_width), float(full_texture_height));
    return uv;
}

vec2 texture_coord_from_direction(vec3 dir, int probe_index, int full_texture_width, int full_texture_height, int probe_side_length) {
    // Get encoded [-1,1] octahedral coordinate
    vec2 normalized_oct_coord = oct_encode(normalize(dir));
    // Map it to [0,1]
    vec2 normalized_oct_coord_zero_one = (normalized_oct_coord * 0.5) + 0.5f;

    // Length of a probe side, plus one pixel on each edge for the border
    float probe_with_border_side = float(probe_side_length) + 2.0f;

    vec2 oct_coord_normalized_to_texture_dimensions = (normalized_oct_coord_zero_one * float(probe_side_length)) 
                                                    / vec2(float(full_texture_width), float(full_texture_height));

    int probes_per_row = (full_texture_width) / int(probe_with_border_side);

    // Add (1,1) back to texCoord within larger texture. Compensates for 1 pix border around top left probe.
    vec2 probe_top_left_position = vec2((probe_index % probes_per_row) * probe_with_border_side,
        (probe_index / probes_per_row) * probe_with_border_side) + vec2(1.0f, 1.0f);

    vec2 normalized_probe_top_left_position = vec2(probe_top_left_position) / vec2(float(full_texture_width), float(full_texture_height));

    return vec2(normalized_probe_top_left_position + oct_coord_normalized_to_texture_dimensions);
}

// Probe coordinate system ///////////////////////////////////////////////
ivec3 probe_index_to_grid_indices( int probe_index ) {
    const int probe_x = probe_index % probe_counts.x;
    const int probe_counts_xy = probe_counts.x * probe_counts.y;

    const int probe_y = (probe_index % probe_counts_xy) / probe_counts.x;
    const int probe_z = probe_index / probe_counts_xy;

    return ivec3( probe_x, probe_y, probe_z );
}

int probe_indices_to_index(in ivec3 probe_coords) {
    return int(probe_coords.x + probe_coords.y * probe_counts.x + probe_coords.z * probe_counts.x * probe_counts.y);
}

vec3 grid_indices_to_world_no_offsets( ivec3 grid_indices ) {
    return grid_indices * probe_spacing + probe_grid_position;
}

vec3 grid_indices_to_world( ivec3 grid_indices, int probe_index ) {
    const int probe_counts_xy = probe_counts.x * probe_counts.y;
    ivec2 probe_offset_sampling_coordinates = ivec2(probe_index % probe_counts_xy, probe_index / probe_counts_xy);
    vec3 probe_offset = use_probe_offsetting() ? texelFetch(global_textures[nonuniformEXT(probe_offset_texture_index)], probe_offset_sampling_coordinates, 0).rgb : vec3(0);

    return grid_indices_to_world_no_offsets( grid_indices ) + probe_offset;
}

ivec3 world_to_grid_indices( vec3 world_position ) {
    return clamp(ivec3((world_position - probe_grid_position) * reciprocal_probe_spacing), ivec3(0), probe_counts - ivec3(1));
}

int get_probe_index_from_pixels(ivec2 pixels, int probe_with_border_side, int full_texture_width) {
    int probes_per_side = full_texture_width / probe_with_border_side;
    return int(pixels.x / probe_with_border_side) + probes_per_side * int(pixels.y / probe_with_border_side);
}

// Sample Irradiance /////////////////////////////////////////////////////

vec3 sample_irradiance( vec3 world_position, vec3 normal, vec3 camera_position ) {

    const vec3 Wo = normalize(camera_position.xyz - world_position);
    // Bias vector to offset probe sampling based on normal and view vector.
    const float minimum_distance_between_probes = 1.0f;
    vec3 bias_vector = (normal * 0.2f + Wo * 0.8f) * (0.75f * minimum_distance_between_probes) * self_shadow_bias;

    vec3 biased_world_position = world_position + bias_vector;

    // Sample at world position + probe offset reduces shadow leaking.
    ivec3 base_grid_indices = world_to_grid_indices(biased_world_position);
    vec3 base_probe_world_position = grid_indices_to_world_no_offsets( base_grid_indices );

    // alpha is how far from the floor(currentVertex) position. on [0, 1] for each axis.
    vec3 alpha = clamp((biased_world_position - base_probe_world_position) , vec3(0.0f), vec3(1.0f));

    vec3  sum_irradiance = vec3(0.0f);
    float sum_weight = 0.0f;

    // Iterate over adjacent probe cage
    for (int i = 0; i < 8; ++i) {
        // Compute the offset grid coord and clamp to the probe grid boundary
        // Offset = 0 or 1 along each axis
        ivec3  offset = ivec3(i, i >> 1, i >> 2) & ivec3(1);
        ivec3  probe_grid_coord = clamp(base_grid_indices + offset, ivec3(0), probe_counts - ivec3(1));
        int probe_index = probe_indices_to_index(probe_grid_coord);

        // Make cosine falloff in tangent plane with respect to the angle from the surface to the probe so that we never
        // test a probe that is *behind* the surface.
        // It doesn't have to be cosine, but that is efficient to compute and we must clip to the tangent plane.
        vec3 probe_pos = grid_indices_to_world(probe_grid_coord, probe_index);

        // Compute the trilinear weights based on the grid cell vertex to smoothly
        // transition between probes. Avoid ever going entirely to zero because that
        // will cause problems at the border probes. This isn't really a lerp. 
        // We're using 1-a when offset = 0 and a when offset = 1.
        vec3 trilinear = mix(1.0 - alpha, alpha, offset);
        float weight = 1.0;

        if ( use_smooth_backface() ) {
            // Computed without the biasing applied to the "dir" variable. 
            // This test can cause reflection-map looking errors in the image
            // (stuff looks shiny) if the transition is poor.
            vec3 direction_to_probe = normalize(probe_pos - world_position);

            // The naive soft backface weight would ignore a probe when
            // it is behind the surface. That's good for walls. But for small details inside of a
            // room, the normals on the details might rule out all of the probes that have mutual
            // visibility to the point. So, we instead use a "wrap shading" test below inspired by
            // NPR work.

            // The small offset at the end reduces the "going to zero" impact
            // where this is really close to exactly opposite
            const float dir_dot_n = (dot(direction_to_probe, normal) + 1.0) * 0.5f;
            weight *= (dir_dot_n * dir_dot_n) + 0.2;
        }

        // Bias the position at which visibility is computed; this avoids performing a shadow 
        // test *at* a surface, which is a dangerous location because that is exactly the line
        // between shadowed and unshadowed. If the normal bias is too small, there will be
        // light and dark leaks. If it is too large, then samples can pass through thin occluders to
        // the other side (this can only happen if there are MULTIPLE occluders near each other, a wall surface
        // won't pass through itself.)
        vec3 probe_to_biased_point_direction = biased_world_position - probe_pos;
        float distance_to_biased_point = length(probe_to_biased_point_direction);
        probe_to_biased_point_direction *= 1.0 / distance_to_biased_point;

        // Visibility
        if ( use_visibility() ) {

            vec2 uv = get_probe_uv(probe_to_biased_point_direction, probe_index, visibility_texture_width, visibility_texture_height, visibility_side_length );

            vec2 visibility = textureLod(global_textures[nonuniformEXT(grid_visibility_texture_index)], uv, 0).rg;

            float mean_distance_to_occluder = visibility.x;

            float chebyshev_weight = 1.0;
            if (distance_to_biased_point > mean_distance_to_occluder) {
                // In "shadow"
                float variance = abs((visibility.x * visibility.x) - visibility.y);
                // http://www.punkuser.net/vsm/vsm_paper.pdf; equation 5
                // Need the max in the denominator because biasing can cause a negative displacement
                const float distance_diff = distance_to_biased_point - mean_distance_to_occluder;
                chebyshev_weight = variance / (variance + (distance_diff * distance_diff));
                
                // Increase contrast in the weight
                chebyshev_weight = max((chebyshev_weight * chebyshev_weight * chebyshev_weight), 0.0f);
            }

            // Avoid visibility weights ever going all of the way to zero because when *no* probe has
            // visibility we need some fallback value.
            chebyshev_weight = max(0.05f, chebyshev_weight);
            weight *= chebyshev_weight;
        }

        // Avoid zero weight
        weight = max(0.000001, weight);

        // A small amount of light is visible due to logarithmic perception, so
        // crush tiny weights but keep the curve continuous
        const float crushThreshold = 0.2f;
        if (weight < crushThreshold) {
            weight *= (weight * weight) * (1.f / (crushThreshold * crushThreshold));
        }

        vec2 uv = get_probe_uv(normal, probe_index, irradiance_texture_width, irradiance_texture_height, irradiance_side_length );

        vec3 probe_irradiance = textureLod(global_textures[nonuniformEXT(grid_irradiance_output_index)], uv, 0).rgb;

        if ( use_perceptual_encoding() ) {
            probe_irradiance = pow(probe_irradiance, vec3(0.5f * 5.0f));
        }

        // Trilinear weights
        weight *= trilinear.x * trilinear.y * trilinear.z + 0.001f;

        sum_irradiance += weight * probe_irradiance;
        sum_weight += weight;
    }

    vec3 net_irradiance = sum_irradiance / sum_weight;

    if ( use_perceptual_encoding() ) {
        net_irradiance = net_irradiance * net_irradiance;
    }

    vec3 irradiance = 0.5f * PI * net_irradiance * 0.95f;

    return irradiance;
}


#endif // RAPTOR_GLSL_DDGI_H