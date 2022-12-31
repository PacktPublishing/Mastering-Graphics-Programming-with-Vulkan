

#if defined(VERTEX_MAIN_TRIANGLE) || defined (VERTEX_MAIN_POST)

layout (location = 0) out vec2 vTexCoord;
layout (location = 1) flat out uint out_texture_id;

void main() {

    vTexCoord.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vTexCoord.xy * 2.0f - 1.0f, 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;

    out_texture_id = gl_InstanceIndex;
}

#endif // VERTEX_MAIN

#if defined(FRAGMENT_MAIN_TRIANGLE)

layout (location = 0) in vec2 vTexCoord;
layout (location = 1) flat in uint texture_id;

layout (location = 0) out vec4 out_color;

void main() {
    vec4 color = texture(global_textures[nonuniformEXT(texture_id)], vTexCoord.xy);
    out_color = color;
}

#endif // FRAGMENT_MAIN


#if defined(FRAGMENT_MAIN_POST)

layout (location = 0) in vec2 vTexCoord;
layout (location = 1) flat in uint texture_id;

layout (location = 0) out vec4 out_color;


layout ( std140, set = MATERIAL_SET, binding = 11 ) uniform PostConstants {

    uint        tonemap_type;
    float       exposure;
    float       sharpening_amount;
    uint        pad001_pc;

    vec2        mouse_uv;
    float       zoom_scale;
    uint        enable_zoom;
};

// All filters taken from https://www.shadertoy.com/view/WdjSW3
vec3 rrt_odt_fit(vec3 v) {
    vec3 a = v*(         v + 0.0245786) - 0.000090537;
    vec3 b = v*(0.983729*v + 0.4329510) + 0.238081;
    return a/b;
}

mat3 mat3_from_rows(vec3 c0, vec3 c1, vec3 c2) {
    mat3 m = mat3(c0, c1, c2);
    m = transpose(m);

    return m;
}

vec3 aces_fitted(vec3 color) {
    mat3 ACES_INPUT_MAT = mat3(0.59719, 0.076, 0.0284, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    mat3 ACES_OUTPUT_MAT = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);

    color = ACES_INPUT_MAT * color;

    // Apply RRT and ODT
    color = rrt_odt_fit(color);

    color = ACES_OUTPUT_MAT * color;

    color = saturate(color);

    return color;
}

float sd_box( in vec2 p, in vec2 b ) {
    vec2 d = abs(p)-b;
    return length(max(d,0.0)) + min(max(d.x,d.y),0.0);
}

void main() {
    vec4 color = texture(global_textures[nonuniformEXT(texture_id)], vTexCoord.xy);
    color.rgb *= exposure;

    float input_luminance = luminance(color.rgb);
    float average_luminance = 0.f;

    // Sharpen
    for (int x = -1; x <= 1; ++x ) {
        for (int y = -1; y <= 1; ++y ) {
            vec3 sampled_color = texture(global_textures[nonuniformEXT(texture_id)], vTexCoord.xy + vec2( x / resolution.x, y / resolution.y )).rgb;
            average_luminance += luminance( sampled_color );
        }
    }

    average_luminance /= 9.0f;

    float sharpened_luminance = input_luminance - average_luminance;
    float final_luminance = input_luminance + sharpened_luminance * sharpening_amount;
    color.rgb = color.rgb * (final_luminance / input_luminance);

    // Tonemapping
    switch (tonemap_type) {
        case 1:
            color.rgb = aces_fitted(color.rgb);
            break;
    }

    // Add a zoom rectangle to debug pixels
    if ( enable_zoom > 0 ) {
        vec2 uv = vTexCoord.xy;
        float rect_size = 0.07f;
        vec2 aspect_ratio = vec2( 1.0, resolution.x / resolution.y );
        float rect_dist = sd_box( mouse_uv - uv, rect_size * aspect_ratio );
        if (rect_dist < rect_size)
             color = texture(global_textures[nonuniformEXT(texture_id)], (vTexCoord.xy + mouse_uv) / zoom_scale );
    }

    out_color = color;
}

#endif // FRAGMENT_MAIN

#if defined(COMPUTE_TEMPORAL_AA)

layout ( std140, set = MATERIAL_SET, binding = 50 ) uniform TaaConstants {

    uint        history_color_texture_index;
    uint        taa_output_texture_index;
    uint        velocity_texture_index;
    uint        current_color_texture_index;

    uint        taa_modes;
    uint        options;
    uint        pad000_tc;
    uint        pad001_tc;

    uint        velocity_sampling_mode;
    uint        history_sampling_filter;
    uint        history_constraint_mode;
    uint        current_color_filter;
};

// TAA modes
#define TAAModeSimplest                     0
#define TAAModeRaptor                       1

// Velocity sampling modes
#define VelocitySamplingModeSingle          0
#define VelocitySamplingMode3x3             1

// History sampling filter
#define HistorySamplingFilterSingle         0
#define HistorySamplingFilterCatmullRom     1

// History clipping mode
#define HistoryConstraintModeNone           0
#define HistoryConstraintModeClamp          1
#define HistoryConstraintModeClip           2
#define HistoryConstraintModeVarianceClip   3
#define HistoryConstraintModeVarianceClipClamp 4

// Current color filter
#define CurrentColorFilterNone              0
#define CurrentColorFilterMitchell          1
#define CurrentColorFilterBlackman          2
#define CurrentColorFilterCatmullRom        3

// Options
bool use_inverse_luminance_filtering() {
    return (options & 1) == 1;
}

bool use_temporal_filtering() {
    return (options & 2) == 2;
}

bool use_luminance_difference_filtering() {
    return (options & 4) == 4;
}

bool use_ycocg() {
    return (options & 8) == 8;
}

// Optimized clip aabb function from Inside game.
vec4 clip_aabb(vec3 aabb_min, vec3 aabb_max, vec4 previous_sample, float average_alpha) {
    // note: only clips towards aabb center (but fast!)
    vec3 p_clip = 0.5 * (aabb_max + aabb_min);
    vec3 e_clip = 0.5 * (aabb_max - aabb_min) + 0.000000001f;

    vec4 v_clip = previous_sample - vec4(p_clip, average_alpha);
    vec3 v_unit = v_clip.xyz / e_clip;
    vec3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0) {
        return vec4(p_clip, average_alpha) + v_clip / ma_unit;
    }
    else {
        // point inside aabb
        return previous_sample;
    }
}

// Utility methods to sample textures.
float sample_depth(vec2 uv) {
    return textureLod(global_textures[nonuniformEXT(depth_texture_index)], uv, 0).r;
}

float sample_depth_point(ivec2 pos) {
    return texelFetch(global_textures[nonuniformEXT(depth_texture_index)], pos, 0).r;
}

vec4 sample_color(vec2 uv) {
    vec4 color = textureLod(global_textures[nonuniformEXT(current_color_texture_index)], uv, 0);

    if ( use_ycocg() ) {
        return vec4(rgb_to_ycocg(color.rgb), color.a);
    }

    return color;
}

vec4 sample_history_color(vec2 uv) {
    vec4 color = textureLod(global_textures[nonuniformEXT(history_color_texture_index)], uv, 0);

    if ( use_ycocg() ) {
        return vec4(rgb_to_ycocg(color.rgb), color.a);
    }

    return color;
}

vec4 sample_current_color_point(ivec2 pos) {
    vec4 color = texelFetch(global_textures[nonuniformEXT(current_color_texture_index)], pos, 0);

    if ( use_ycocg() ) {
        return vec4(rgb_to_ycocg(color.rgb), color.a);
    }

    return color;
}

vec4 sample_history_color_point(ivec2 pos) {
    vec4 color = texelFetch(global_textures[nonuniformEXT(history_color_texture_index)], pos, 0);

    if ( use_ycocg() ) {
        return vec4(rgb_to_ycocg(color.rgb), color.a);
    }

    return color;
}

vec2 sample_motion_vector( ivec2 pos ) {
    return texelFetch(global_textures[nonuniformEXT(velocity_texture_index)], pos, 0).rg;
}

vec2 sample_motion_vector_point( ivec2 pos ) {
    return texelFetch(global_textures[nonuniformEXT(velocity_texture_index)], pos, 0).rg;
}

// Find closest fragment position in a 3x3 neighborhood reading depth as metrics.
void find_closest_fragment_3x3(ivec2 pixel, out ivec2 closest_position, out float closest_depth) {

    closest_depth = 1.0f;
    closest_position = ivec2(0,0);

    for (int x = -1; x <= 1; ++x ) {
        for (int y = -1; y <= 1; ++y ) {

            ivec2 pixel_position = pixel + ivec2(x, y);
            pixel_position = clamp(pixel_position, ivec2(0), ivec2(resolution.x - 1, resolution.y - 1));

            float current_depth = texelFetch(global_textures[nonuniformEXT(depth_texture_index)], pixel_position, 0).r;
            if ( current_depth < closest_depth ) {
                closest_depth = current_depth;
                closest_position = pixel_position;
            }
        }
    }
}

// https://github.com/TheRealMJP/MSAAFilter/blob/master/MSAAFilter/Resolve.hlsl
float filter_cubic(in float x, in float B, in float C) {
    float y = 0.0f;
    float x2 = x * x;
    float x3 = x * x * x;
    if(x < 1)
        y = (12 - 9 * B - 6 * C) * x3 + (-18 + 12 * B + 6 * C) * x2 + (6 - 2 * B);
    else if (x <= 2)
        y = (-B - 6 * C) * x3 + (6 * B + 30 * C) * x2 + (-12 * B - 48 * C) * x + (8 * B + 24 * C);

    return y / 6.0f;
}

float filter_mitchell(float value) {
    float cubic_value = value;
    return filter_cubic( cubic_value, 1 / 3.0f, 1 / 3.0f );
}

float filter_blackman_harris(float value) {
    float x = 1.0f - value;

    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;
    return saturate(a0 - a1 * cos(PI * x) + a2 * cos(2 * PI * x) - a3 * cos(3 * PI * x));
}

float filter_catmull_rom(float value) {
    return filter_cubic(value, 0, 0.5f);
}

// Choose between different filters.
float subsample_filter(float value) {
    // Cubic filters works on [-2, 2] domain, thus scale the value by 2
    // for Mitchell, Blackmann and Catmull-Rom.
    switch (current_color_filter) {
        case CurrentColorFilterNone:
            return value;
        case CurrentColorFilterMitchell:
            return filter_mitchell( value * 2 );
        case CurrentColorFilterBlackman:
            return filter_blackman_harris( value * 2 );
        case CurrentColorFilterCatmullRom:
            return filter_catmull_rom( value * 2 );
    }

    return value;
}

// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
// See http://vec3.ca/bicubic-filtering-in-fewer-taps/ for more details
vec3 sample_texture_catmull_rom(vec2 uv, uint texture_index) {
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    vec2 sample_position = uv * resolution;
    vec2 tex_pos_1 = floor(sample_position - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    vec2 f = sample_position - tex_pos_1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    vec2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    vec2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    vec2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    vec2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    vec2 w12 = w1 + w2;
    vec2 offset_12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    vec2 tex_pos_0 = tex_pos_1 - 1;
    vec2 tex_pos_3 = tex_pos_1 + 2;
    vec2 tex_pos_12 = tex_pos_1 + offset_12;

    tex_pos_0 /= resolution;
    tex_pos_3 /= resolution;
    tex_pos_12 /= resolution;

    vec3 result = vec3(0);
    result += textureLod(global_textures[nonuniformEXT(texture_index)], vec2(tex_pos_0.x, tex_pos_0.y), 0).rgb * w0.x * w0.y;
    result += textureLod(global_textures[nonuniformEXT(texture_index)], vec2(tex_pos_12.x, tex_pos_0.y), 0).rgb * w12.x * w0.y;
    result += textureLod(global_textures[nonuniformEXT(texture_index)], vec2(tex_pos_3.x, tex_pos_0.y), 0).rgb * w3.x * w0.y;

    result += textureLod(global_textures[nonuniformEXT(texture_index)], vec2(tex_pos_0.x, tex_pos_12.y), 0).rgb * w0.x * w12.y;
    result += textureLod(global_textures[nonuniformEXT(texture_index)], vec2(tex_pos_12.x, tex_pos_12.y), 0).rgb * w12.x * w12.y;
    result += textureLod(global_textures[nonuniformEXT(texture_index)], vec2(tex_pos_3.x, tex_pos_12.y), 0).rgb * w3.x * w12.y;

    result += textureLod(global_textures[nonuniformEXT(texture_index)], vec2(tex_pos_0.x, tex_pos_3.y), 0).rgb * w0.x * w3.y;
    result += textureLod(global_textures[nonuniformEXT(texture_index)], vec2(tex_pos_12.x, tex_pos_3.y), 0).rgb * w12.x * w3.y;
    result += textureLod(global_textures[nonuniformEXT(texture_index)], vec2(tex_pos_3.x, tex_pos_3.y), 0).rgb * w3.x * w3.y;

    if ( use_ycocg() ) {
        result = rgb_to_ycocg( result.rgb );
    }

    return result;
}


// Simples implementation of TAA, left for learning purposes.
vec3 taa_simplest( ivec2 pos ) {
    const vec2 velocity = sample_motion_vector( pos );
    const vec2 screen_uv = uv_nearest(pos, resolution);
    const vec2 reprojected_uv = screen_uv - velocity;
    vec3 history_color = sample_history_color(reprojected_uv).rgb;
    vec3 current_color = sample_color(screen_uv.xy).rgb;

    return mix(current_color, history_color, 0.9f);
}

vec3 taa_raptor( ivec2 pos ) {

    // Velocity sampling: find coordinates to sample motion vectors.
    float closest_depth = 1.0f;
    ivec2 closest_position = ivec2(0,0);

    switch (velocity_sampling_mode) {
        case VelocitySamplingModeSingle:
            closest_position = pos;
            break;

        case VelocitySamplingMode3x3:
            find_closest_fragment_3x3( pos.xy, closest_position, closest_depth );
        default:
            break;
    }

    // Sample motion vectors.
    const vec2 velocity = sample_motion_vector_point( closest_position );
    const vec2 screen_uv = uv_nearest(pos.xy, resolution);
    const vec2 reprojected_uv = screen_uv - velocity;

    // History sampling: read history samples and optionally apply a filter to it.
    vec3 history_color = vec3(0);
    history_color = sample_history_color( reprojected_uv ).rgb;
    switch (history_sampling_filter) {
        case HistorySamplingFilterSingle:
            history_color = sample_history_color( reprojected_uv ).rgb;
            break;

        case HistorySamplingFilterCatmullRom:
            history_color = sample_texture_catmull_rom( reprojected_uv, history_color_texture_index );
            break;

    }

    // Current sampling: read a 3x3 neighborhood and cache color and other data to process history and final resolve.
    // Accumulate current sample and weights.
    vec3 current_sample_total = vec3(0);
    float current_sample_weight = 0.0f;
    // Min and Max used for history clipping
    vec3 neighborhood_min = vec3(10000);
    vec3 neighborhood_max = vec3(-10000);
    // Cache of moments used in the resolve phase
    vec3 m1 = vec3(0);
    vec3 m2 = vec3(0);

    for (int x = -1; x <= 1; ++x ) {
        for (int y = -1; y <= 1; ++y ) {

            ivec2 pixel_position = pos + ivec2(x, y);
            pixel_position = clamp(pixel_position, ivec2(0), ivec2(resolution.x - 1, resolution.y - 1));

            vec3 current_sample = sample_current_color_point(pixel_position).rgb;
            vec2 subsample_position = vec2(x * 1.f, y * 1.f);
            float subsample_distance = length( subsample_position );
            float subsample_weight = subsample_filter( subsample_distance );

            current_sample_total += current_sample * subsample_weight;
            current_sample_weight += subsample_weight;

            neighborhood_min = min( neighborhood_min, current_sample );
            neighborhood_max = max( neighborhood_max, current_sample );

            m1 += current_sample;
            m2 += current_sample * current_sample;
        }
    }

    // Calculate current sample color
    vec3 current_sample = current_sample_total / current_sample_weight;

    // Guard for outside sampling
    if (any(lessThan(reprojected_uv, vec2(0.0f))) || any(greaterThan(reprojected_uv, vec2(1.0f)))) {
        return current_sample;
    }

    // shrink chroma min-max
    if ( use_ycocg() ) {
        vec2 chroma_extent = vec2( 0.25 * 0.5 * (neighborhood_max.r - neighborhood_min.r) );
        vec2 chroma_center = current_sample.gb;
        neighborhood_min.yz = chroma_center - chroma_extent;
        neighborhood_max.yz = chroma_center + chroma_extent;
    }

    // History constraint
    switch (history_constraint_mode) {
        case HistoryConstraintModeNone:
            break;

        case HistoryConstraintModeClamp:
            history_color.rgb = clamp(history_color.rgb, neighborhood_min, neighborhood_max);
            break;

        case HistoryConstraintModeClip:
            history_color.rgb = clip_aabb(neighborhood_min, neighborhood_max, vec4(history_color, 1.0f), 1.0f).rgb;
            break;

        case HistoryConstraintModeVarianceClip: {
            float rcp_sample_count = 1.0f / 9.0f;
            float gamma = 1.0f;
            vec3 mu = m1 * rcp_sample_count;
            vec3 sigma = sqrt(abs((m2 * rcp_sample_count) - (mu * mu)));
            vec3 minc = mu - gamma * sigma;
            vec3 maxc = mu + gamma * sigma;

            history_color.rgb = clip_aabb(minc, maxc, vec4(history_color, 1), 1.0f).rgb;

            break;
        }
        case HistoryConstraintModeVarianceClipClamp:
        default: {
            float rcp_sample_count = 1.0f / 9.0f;
            float gamma = 1.0f;
            vec3 mu = m1 * rcp_sample_count;
            vec3 sigma = sqrt(abs((m2 * rcp_sample_count) - (mu * mu)));
            vec3 minc = mu - gamma * sigma;
            vec3 maxc = mu + gamma * sigma;

            vec3 clamped_history_color = clamp( history_color.rgb, neighborhood_min, neighborhood_max );
            history_color.rgb = clip_aabb(minc, maxc, vec4(clamped_history_color, 1), 1.0f).rgb;

            break;
        }
    }

    // Resolve: combine history and current colors for final pixel color.
    vec3 current_weight = vec3(0.1f);
    vec3 history_weight = vec3(1.0 - current_weight);


    // Temporal filtering
    if (use_temporal_filtering() ) {
        vec3 temporal_weight = clamp(abs(neighborhood_max - neighborhood_min) / current_sample, vec3(0), vec3(1));
        history_weight = clamp(mix(vec3(0.25), vec3(0.85), temporal_weight), vec3(0), vec3(1));
        current_weight = 1.0f - history_weight;
    }

    // Inverse luminance filtering
    if (use_inverse_luminance_filtering() || use_luminance_difference_filtering() ) {
        // Calculate compressed colors and luminances
        vec3 compressed_source = current_sample / (max(max(current_sample.r, current_sample.g), current_sample.b) + 1.0f);
        vec3 compressed_history = history_color / (max(max(history_color.r, history_color.g), history_color.b) + 1.0f);
        float luminance_source = use_ycocg() ? compressed_source.r : luminance(compressed_source);
        float luminance_history = use_ycocg() ? compressed_history.r : luminance(compressed_history);

        if ( use_luminance_difference_filtering() ) {
            float unbiased_diff = abs(luminance_source - luminance_history) / max(luminance_source, max(luminance_history, 0.2));
            float unbiased_weight = 1.0 - unbiased_diff;
            float unbiased_weight_sqr = unbiased_weight * unbiased_weight;
            float k_feedback = mix(0.0f, 1.0f, unbiased_weight_sqr);

            history_weight = vec3(1.0 - k_feedback);
            current_weight = vec3(k_feedback);
        }

        current_weight *= 1.0 / (1.0 + luminance_source);
        history_weight *= 1.0 / (1.0 + luminance_history);
    }

    vec3 result = ( current_sample * current_weight + history_color * history_weight ) / max( current_weight + history_weight, 0.00001 );
    return result;
}


layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
    ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
    const vec2 screen_uv = uv_nearest(pos.xy, resolution);

    vec3 final_color = vec3(0);

    if ( taa_modes == 0 ) {
        final_color = taa_simplest( pos.xy );
    }
    else if ( taa_modes == 1 ) {
        final_color = taa_raptor( pos.xy );
    }

    if ( use_ycocg() ) {
        final_color = ycocg_to_rgb( final_color );
    }

    imageStore( global_images_2d[taa_output_texture_index], pos.xy, vec4(final_color, 1) );
}

#endif // COMPUTE_TEMPORAL_AA


#if defined(COMPUTE_COMPOSITE_CAMERA_MOTION)

// Read-write texture.
layout(rg16f, set = MATERIAL_SET, binding = 51) uniform image2D motion_vectors;
layout(rg16f, set = MATERIAL_SET, binding = 52) uniform image2D visibility_motion_vectors;

layout( set = MATERIAL_SET, binding = 53 ) uniform sampler2D normals_texture;

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
    ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);

    const float raw_depth = texelFetch(global_textures[nonuniformEXT(depth_texture_index)], pos.xy, 0).r;
    const vec2 screen_uv = uv_nearest(pos.xy, resolution);
    const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

    vec4 current_position_ndc = vec4( ndc_from_uv_raw_depth( screen_uv, raw_depth ), 1.0f );
    vec4 previous_position_ndc = previous_view_projection * vec4(pixel_world_position, 1.0f);
    previous_position_ndc.xyz /= previous_position_ndc.w;

    vec2 jitter_difference = (jitter_xy - previous_jitter_xy) * 0.5f;
    vec2 velocity = current_position_ndc.xy - previous_position_ndc.xy;
    velocity -= jitter_difference;

    imageStore( motion_vectors, pos.xy, vec4(velocity, 0, 0) );

    // NOTE(marco): compute values for shadow visibility buffer
    vec2 encoded_normal = texelFetch( normals_texture, pos.xy, 0 ).rg;
    vec3 normal = octahedral_decode( encoded_normal );
    vec4 view_normal = world_to_camera * vec4( normal, 0.0 );

    float c1 = 0.003;
    float c2 = 0.017;
    float depth_diff = abs( 1.0 - ( previous_position_ndc.z / current_position_ndc.z ) );
    float eps = c1 + c2 * abs( view_normal.z );

    vec2 visibility_motion = depth_diff < eps ? vec2( current_position_ndc.xy - previous_position_ndc.xy ) : vec2( -1, -1 );

    // TODO(marco): the article wants to store the previous depth value, but it doesn't look like it's needed?!
    imageStore( visibility_motion_vectors, pos.xy, vec4(visibility_motion, 0, 0) );
}

#endif
