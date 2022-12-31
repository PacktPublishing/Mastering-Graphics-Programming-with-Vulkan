
// Common code ///////////////////////////////////////////////////////////

layout ( std140, set = MATERIAL_SET, binding = 40 ) uniform DDGIConstants {
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
    float       pad001_ddgic;
    float       pad002_ddgic;

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

struct RayPayload {
    vec3        radiance;
    float       distance;
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



// Common Raytracing code ////////////////////////////////////////////////
#if defined (RAYGEN_PROBE_RT) || defined (CLOSEST_HIT_PROBE_RT) || defined (MISS_PROBE_RT)

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
    const int probe_index = pixel_coord.y;
    const int ray_index = pixel_coord.x;

    const bool skip_probe = (probe_status[probe_index] == PROBE_STATUS_OFF) || (probe_status[probe_index] == PROBE_STATUS_UNINITIALIZED);
    if ( use_probe_status() && skip_probe ) {
        return;
    }

    ivec3 probe_grid_indices = probe_index_to_grid_indices( probe_index );
    vec3 ray_origin = grid_indices_to_world( probe_grid_indices, probe_index );
    vec3 direction = normalize( mat3(random_rotation) * spherical_fibonacci(ray_index, probe_rays) );
    payload.radiance = vec3(0);
    payload.distance = 0;

    traceRayEXT(as, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, ray_origin, 0.0, direction, 100.0, 0);

    imageStore(global_images_2d[ radiance_output_index ], pixel_coord, vec4(payload.radiance, payload.distance));
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
        uint max_backfaces = uint(probe_rays * 0.1f);//volume.probeRandomRayBackfaceThreshold);

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
            weight = pow(weight, 2.5f);//volume.probeDistanceExponent);

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


