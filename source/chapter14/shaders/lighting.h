
#ifndef RAPTOR_GLSL_LIGHTING_H
#define RAPTOR_GLSL_LIGHTING_H

#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

#define DEBUG_OPTIONS
#define ENABLE_OPTIMIZATION 1
#define RAYTRACED_SHADOWS 1
#define FRAME_HISTORY_COUNT 4
#define USE_SHADOW_VISIBILITY 1 // TODO(marco): make into scene option
#define MAX_SHADOW_VISIBILITY_SAMPLE_COUNT 5 // TODO(marco): make into scene option

#if RAYTRACED_SHADOWS
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#endif

struct Light {
    vec3            world_position;
    float           radius;

    vec3            color;
    float           intensity;

    float           shadow_map_resolution;
    float           rcp_n_minus_f; // Calculation of 1 / (n - f) used to retrieve cubemap shadows depth value.
    float           padding_l_001;
    float           padding_l_002;
};

layout( set = MATERIAL_SET, binding = 20 ) readonly buffer ZBins {
    uint bins[];
};

layout( set = MATERIAL_SET, binding = 21 ) readonly buffer Lights {
    Light lights[];
};

layout( set = MATERIAL_SET, binding = 22 ) readonly buffer Tiles {
    uint tiles[];
};

layout ( std140, set = MATERIAL_SET, binding = 23 ) uniform LightConstants {

    uint        cubemap_shadows_index;
    uint        debug_show_light_tiles;
    uint        debug_show_tiles;
    uint        debug_show_bins;

    uint        disable_shadows;
    uint        debug_modes;
    uint        debug_texture_index;
    uint        shadow_visibility_texture_index;

    uint        volumetric_fog_texture_index;
    int         volumetric_fog_num_slices;
    float       volumetric_fog_near;
    float       volumetric_fog_far;

    float       volumetric_fog_distribution_scale;
    float       volumetric_fog_distribution_bias;
    float       gi_intensity;
    uint        indirect_lighting_texture_index;

    uint        bilateral_weights_texture_index;
    uint        reflections_texture_index;
    uint        raytraced_shadow_light_color_type;
    float       raytraced_shadow_light_radius;

    vec3        raytraced_shadow_light_position;
    float       raytraced_shadow_light_intensity;
};

layout( set = MATERIAL_SET, binding = 25 ) readonly buffer LightIndices {
    uint light_indices[];
};

#if RAYTRACED_SHADOWS
layout( set = MATERIAL_SET, binding = 26 ) uniform accelerationStructureEXT as;
#endif

bool is_raytrace_shadow_point_light() {
    return ((raytraced_shadow_light_color_type >> 24) & 1) == 0;
}

uint hash(uint a) {
   a = (a+0x7ed55d16) + (a<<12);
   a = (a^0xc761c23c) ^ (a>>19);
   a = (a+0x165667b1) + (a<<5);
   a = (a+0xd3a2646c) ^ (a<<9);
   a = (a+0xfd7046c5) + (a<<3);
   a = (a^0xb55a4f09) ^ (a>>16);
   return a;
}

// BRDF //////////////////////////////////////////////////////////////////

vec3 f_schlick(const vec3 f0, float VoH) {
    float f = pow(1.0 - VoH, 5.0);
    return f + f0 * (1.0 - f);
}

float f_schlick_f90(float u, float f0, float f90) {
    return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}

vec3 fresnel_schlick_roughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

// Diffuse functions
float fd_lambert() {
    return 1.0 / PI;
}
float fd_burley(float NoV, float NoL, float LoH, float roughness) {
    float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
    float lightScatter = f_schlick_f90(NoL, 1.0, f90);
    float viewScatter = f_schlick_f90(NoV, 1.0, f90);
    return lightScatter * viewScatter * (1.0 / PI);
}

// Specular functions
float d_ggx(float roughness, float NoH, const vec3 h) {
    // Walter et al. 2007, "Microfacet Models for Refraction through Rough Surfaces"
    float oneMinusNoHSquared = 1.0 - NoH * NoH;

    float a = NoH * roughness;
    float k = roughness / (oneMinusNoHSquared + a * a);
    float d = k * k * (1.0 / PI);
    return d;
}

float v_smith_ggx_correlated_fast(float roughness, float NoV, float NoL) {
    // Hammon 2017, "PBR Diffuse Lighting for GGX+Smith Microsurfaces"
    float v = 0.5 / mix(2.0 * NoL * NoV, NoL + NoV, roughness);
    return v;
}

vec3 compute_diffuse_color(const vec4 base_color, float metallic) {
    return base_color.rgb * (1.0 - metallic);
}

vec3 compute_f0(const vec4 base_color, float metallic) {
    return mix(vec3(0.04), base_color.rgb, metallic);
}

float attenuation_square_falloff(vec3 position_to_light, float light_inverse_radius) {
    const float distance_square = dot(position_to_light, position_to_light);
    const float factor = distance_square * light_inverse_radius * light_inverse_radius;
    const float smoothFactor = max(1.0 - factor * factor, 0.0);
    return (smoothFactor * smoothFactor) / max(distance_square, 1e-4);
}

float vector_to_depth_value( vec3 direction, float radius, float rcp_n_minus_f ) {
    const vec3 absolute_vec = abs(direction);
    const float local_z_component = max(absolute_vec.x, max(absolute_vec.y, absolute_vec.z));

    const float f = radius;
    const float n = 0.01f;
    // Original value, left for reference.
    //const float normalized_z_component = -(f / (n - f) - (n * f) / (n - f) / local_z_component);
    const float normalized_z_component = ( n * f * rcp_n_minus_f ) / local_z_component - f * rcp_n_minus_f;
    return normalized_z_component;
}

// https://www.gamedev.net/articles/programming/graphics/contact-hardening-soft-shadows-made-fast-r4906/
vec2 vogel_disk_offset(uint sample_index, uint samples_count, float phi) {
    float GoldenAngle = 2.4;

    float r = sqrt(float(sample_index) + 0.5f) / sqrt(float(samples_count));
    float theta = (sample_index * GoldenAngle) + phi;

    float sine, cosine;
    sine = sin(theta);
    cosine = cos(theta);

    return vec2(r * cosine, r * sine);
}

// NOTE(marco): thanks to https://github.com/bartwronski/PoissonSamplingGenerator
#define SAMPLE_NUM 32
vec2 POISSON_SAMPLES[SAMPLE_NUM] =
{
    vec2( 0.39963964752463255f, 0.8910925368990373f ),
    vec2( -0.4940572704167889f, -0.8620650241721987f ),
    vec2( 0.8075570857119035f, -0.5440713505497983f ),
    vec2( -0.9116635046112362f, 0.2639502616182513f ),
    vec2( 0.05343036802745114f, 0.021474316209819044f ),
    vec2( 0.8499579311323042f, 0.27318537130618137f ),
    vec2( -0.3403992818902896f, 0.7063573920801801f ),
    vec2( 0.2101073022086032f, -0.8129909357248446f ),
    vec2( -0.9005900483859263f, -0.391550837884129f ),
    vec2( -0.19587476659917602f, -0.3981303634779107f ),
    vec2( -0.4648065562502342f, 0.02105911800771148f ),
    vec2( 0.35934411533835076f, 0.4121051098766807f ),
    vec2( 0.5065318505687553f, -0.10705978878497402f ),
    vec2( -0.7602340603847367f, 0.6493924352633489f ),
    vec2( -0.019782992429490595f, 0.8925406666774142f ),
    vec2( 0.3983473193951535f, -0.4801357934668924f ),
    vec2( 0.9869656537989692f, -0.09638640479894947f ),
    vec2( -0.25015603010828763f, 0.2972338092340553f ),
    vec2( -0.13317277640560815f, -0.9143508644248124f ),
    vec2( 0.6996155560882538f, 0.6876222716775685f ),
    vec2( -0.6345508708611187f, -0.24002497065722314f ),
    vec2( 0.07481225966233056f, 0.6194024571949546f ),
    vec2( -0.5795518698024703f, 0.35706998381720817f ),
    vec2( 0.10538818335743431f, -0.5072259616736443f ),
    vec2( 0.5901520300517671f, -0.8055715970062381f ),
    vec2( 0.4997349661429248f, 0.18391430091175387f ),
    vec2( -0.8936441537563113f, -0.09018813624787847f ),
    vec2( -0.49099986787705147f, -0.5534594920185129f ),
    vec2( 0.7883035678609505f, -0.2850303445322458f ),
    vec2( 0.20190051133128753f, -0.2287805625191621f ),
    vec2( 0.10095624109822983f, 0.356329397671627f ),
    vec2( 0.5999403247649068f, 0.4733652413019988f ),
};

float get_directional_light_visibility( vec3 light_position, uint sample_count, vec3 world_position, vec3 normal, uint frame_index ) {

    const vec3 l = normalize( light_position );
    const float NoL = dot(normal, l);

#if 0
    vec3 right = vec3( 1, 0, 0 );
    vec3 x_axis = normalize( cross( l, right ) );
    vec3 z_axis = normalize( cross( l, x_axis ) );
    vec3 x_axis = vec3( 0 );
    if ( abs( l.x ) > abs( l.y ) ) {
        x_axis = normalize( vec3( l.z, 0, -l.x ) );
    } else {
        x_axis = normalize( vec3( 0, -l.z, l.y ) );
    }
    vec3 z_axis = normalize( cross( l, x_axis ) );

    mat3 to_local_coord = mat3(
        x_axis,
        l,
        z_axis
    );
#endif

    vec3 x_axis =  l.y == 1.0f ? normalize(cross(l, vec3(0.0f, 0.0f, 1.0f))) : normalize(cross(l, vec3(0.0f, 1.0f, 0.0f)));
    vec3 y_axis = normalize(cross(x_axis, l));

    float visiblity = 0.0;

    if ( NoL > 0.001f ) {
        for ( uint s = 0; s < sample_count; ++s ) {
#if 0
            vec2 poisson_sample = POISSON_SAMPLES[ s * FRAME_HISTORY_COUNT + frame_index ];
            vec3 random_dir = normalize( vec3( poisson_sample.x, 1.0, poisson_sample.y ) );
            random_dir = normalize( to_local_coord * random_dir );

            float NoR = dot ( random_dir, l );
            if ( NoR <= 0.0001f ) {
                continue;
            }
#endif


#if 1
            vec2 poisson_sample = POISSON_SAMPLES[ (s * FRAME_HISTORY_COUNT + frame_index) % SAMPLE_NUM ];
            vec3 random_x = x_axis * poisson_sample.x * 0.01;
            vec3 random_y = y_axis * poisson_sample.y * 0.01;
            vec3 random_dir = normalize(l + random_x + random_y);
#else
            vec3 random_dir = l;
#endif

#if RAYTRACED_SHADOWS
            rayQueryEXT rayQuery;
            rayQueryInitializeEXT(rayQuery,
                                  as,
                                  gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                                  0xff,
                                  world_position,
                                  0.05,
                                  random_dir,
                                  100.0f);
            rayQueryProceedEXT( rayQuery );

            if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
                visiblity += 1.0f;
            }
#endif
        }
    }

    return visiblity / float( sample_count );
}

float get_point_light_visibility( uint light_index, uint sample_count, vec3 world_position, vec3 normal, uint frame_index ) {
    const vec3 position_to_light = raytraced_shadow_light_position - world_position;
    const vec3 l = normalize( position_to_light );
    const float NoL = dot(normal, l);
    float d = sqrt( dot( position_to_light, position_to_light ) );

#if 0
    vec3 right = vec3( 1, 0, 0 );
    vec3 x_axis = normalize( cross( l, right ) );
    vec3 z_axis = normalize( cross( l, x_axis ) );
    vec3 x_axis = vec3( 0 );
    if ( abs( l.x ) > abs( l.y ) ) {
        x_axis = normalize( vec3( l.z, 0, -l.x ) );
    } else {
        x_axis = normalize( vec3( 0, -l.z, l.y ) );
    }
    vec3 z_axis = normalize( cross( l, x_axis ) );

    mat3 to_local_coord = mat3(
        x_axis,
        l,
        z_axis
    );
#endif

    vec3 x_axis = normalize(cross(l, vec3(0.0f, 1.0f, 0.0f)));
    vec3 y_axis = normalize(cross(x_axis, l));

    float visiblity = 0.0;

    const float r = raytraced_shadow_light_radius;
    float attenuation = attenuation_square_falloff(position_to_light, 1.0f / r);

    const float scaled_distance = r / d;
    if ( (NoL > 0.001f) && (d <= r) && (attenuation > 0.001f) ) {
        for ( uint s = 0; s < sample_count; ++s ) {
#if 0
            vec2 poisson_sample = POISSON_SAMPLES[ s * FRAME_HISTORY_COUNT + frame_index ];
            vec3 random_dir = normalize( vec3( poisson_sample.x, 1.0, poisson_sample.y ) );
            random_dir = normalize( to_local_coord * random_dir );

            float NoR = dot ( random_dir, l );
            if ( NoR <= 0.0001f ) {
                continue;
            }
#endif


#if 1
            vec2 poisson_sample = POISSON_SAMPLES[ (s * FRAME_HISTORY_COUNT + frame_index) % SAMPLE_NUM ];
            vec3 random_x = x_axis * poisson_sample.x * (scaled_distance) * 0.01;
            vec3 random_y = y_axis * poisson_sample.y * (scaled_distance) * 0.01;
            vec3 random_dir = normalize(l + random_x + random_y);
#else
            vec3 random_dir = l;
#endif

#if RAYTRACED_SHADOWS
            rayQueryEXT rayQuery;
            rayQueryInitializeEXT(rayQuery,
                                  as,
                                  gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                                  0xff,
                                  world_position,
                                  0.05,
                                  random_dir,
                                  d);
            rayQueryProceedEXT( rayQuery );

            if (rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
                visiblity += rayQueryGetIntersectionTEXT(rayQuery, true) < d ? 0.0f : 1.0f;
            }
            else {
                visiblity += 1.0f;
            }
#endif
        }
    }

    return visiblity / float( sample_count );
}

vec3 calculate_directional_light_contribution(vec4 albedo, float roughness, vec3 normal, vec3 emissive, vec3 world_position, vec3 v, vec3 F0, float NoV, uvec2 screen_uv, vec3 light_direction) {

    const vec3 l = normalize( light_direction );
    const float NoL = clamp(dot(normal, l), 0.0, 1.0);

    vec3 pixel_luminance = vec3(0);

    // TODO(marco): better upsampling
    float shadow = texture( global_textures_3d[ shadow_visibility_texture_index ], vec3( screen_uv * ( 1 / resolution ), 0 ) ).r;

    // TODO
    if (disable_shadows > 0) {
        shadow = 1;
    }

    if ( shadow >= 0.0f && NoL > 0.0001f ) {
        float light_intensity = NoL * raytraced_shadow_light_intensity * shadow;

        const vec3 h = normalize(v + l);
        const float NoH = saturate(dot(normal, h));
        const float LoH = saturate(dot(l, h));

        const vec3 diffuse = fd_burley(NoV, NoL, LoH, roughness) * albedo.rgb;
        const float D = d_ggx( roughness, NoH, h );

        float V = v_smith_ggx_correlated_fast( roughness, NoV, NoL );

        const float VoH = saturate(dot(v, h));
        vec3 F = f_schlick(F0, VoH);

        vec3 specular = (D * V) * F;

        pixel_luminance = (diffuse + specular) * light_intensity * unpack_color_rgba(raytraced_shadow_light_color_type).rgb;
    }

    return pixel_luminance;
}

vec3 calculate_raytraced_point_light_contribution(vec4 albedo, float roughness, vec3 normal, vec3 emissive, vec3 world_position, vec3 v, vec3 F0, float NoV, uvec2 screen_pixels) {

    const vec3 position_to_light = raytraced_shadow_light_position - world_position;
    const vec3 l = normalize( position_to_light );
    const float NoL = clamp(dot(normal, l), 0.0, 1.0);

    vec3 pixel_luminance = vec3(0);

    float shadow = texelFetch( global_textures_3d[ shadow_visibility_texture_index ], ivec3( screen_pixels * 0.5f, 0 ), 0 ).r;

    if (disable_shadows > 0) {
        shadow = 1;
    }

    const float light_radius = raytraced_shadow_light_radius;
    float attenuation = attenuation_square_falloff(position_to_light, 1.0f / light_radius) * shadow;
    if ( attenuation > 0.0001f && NoL > 0.0001f ) {

        float light_intensity = NoL * raytraced_shadow_light_intensity * attenuation;

        const vec3 h = normalize(v + l);
        const float NoH = saturate(dot(normal, h));
        const float LoH = saturate(dot(l, h));

        const vec3 diffuse = fd_burley(NoV, NoL, LoH, roughness) * albedo.rgb;
        const float D = d_ggx( roughness, NoH, h );

        float V = v_smith_ggx_correlated_fast( roughness, NoV, NoL );

        const float VoH = saturate(dot(v, h));
        vec3 F = f_schlick(F0, VoH);

        vec3 specular = (D * V) * F;

        pixel_luminance = (diffuse + specular) * light_intensity * unpack_color_rgba(raytraced_shadow_light_color_type).rgb;
    }
    
    return pixel_luminance;
}

vec3 calculate_point_light_contribution(vec4 albedo, float roughness, vec3 normal, vec3 emissive, vec3 world_position, vec3 v, vec3 F0, float NoV, uvec2 screen_uv, uint shadow_light_index) {
    Light light = lights[ shadow_light_index ];

    const vec3 position_to_light = light.world_position - world_position;
    const vec3 l = normalize( position_to_light );
    const float NoL = clamp(dot(normal, l), 0.0, 1.0);

    vec3 pixel_luminance = vec3(0);

    vec3 shadow_position_to_light = world_position - light.world_position;
    const float current_depth = vector_to_depth_value(shadow_position_to_light, light.radius, light.rcp_n_minus_f);
    const float bias = 0.0001f;

#if 1
    const uint samples = 4;
    float shadow = 0;
    for(uint i = 0; i < samples; ++i) {

        vec2 disk_offset = vogel_disk_offset(i, 4, 0.1f);
        vec3 sampling_position = shadow_position_to_light + disk_offset.xyx * 0.0005f;
        const float closest_depth = texture(global_textures_cubemaps_array[nonuniformEXT(cubemap_shadows_index)], vec4(sampling_position, shadow_light_index)).r;
        shadow += current_depth - bias < closest_depth ? 1 : 0;
    }

    shadow /= samples;

#else
    const float closest_depth = texture(global_textures_cubemaps_array[nonuniformEXT(cubemap_shadows_index)], vec4(shadow_position_to_light, shadow_light_index)).r;
    float shadow = current_depth - bias < closest_depth ? 1 : 0;
#endif

    // TODO
    if (disable_shadows > 0) {
        shadow = 1;
    }

    float attenuation = attenuation_square_falloff(position_to_light, 1.0f / light.radius) * shadow;
    if ( attenuation > 0.0001f && NoL > 0.0001f ) {

        float light_intensity = NoL * light.intensity * attenuation;

        const vec3 h = normalize(v + l);
        const float NoH = saturate(dot(normal, h));
        const float LoH = saturate(dot(l, h));

        const vec3 diffuse = fd_burley(NoV, NoL, LoH, roughness) * albedo.rgb;
        const float D = d_ggx( roughness, NoH, h );

        float V = v_smith_ggx_correlated_fast( roughness, NoV, NoL );

        const float VoH = saturate(dot(v, h));
        vec3 F = f_schlick(F0, VoH);

        vec3 specular = (D * V) * F;

        pixel_luminance = (diffuse + specular) * light_intensity * light.color;
    }

    return pixel_luminance;
}

// Volumetric fog application
vec3 apply_volumetric_fog( vec2 screen_uv, float raw_depth, vec3 color ) {

    const float near = volumetric_fog_near;
    const float far = volumetric_fog_far;
    // Fog linear depth distribution
    float linear_depth = raw_depth_to_linear_depth( raw_depth, near, far );
    //float depth_uv = linear_depth / far;
    // Exponential
    float depth_uv = linear_depth_to_uv( near, far, linear_depth, volumetric_fog_num_slices );
    vec3 froxel_uvw = vec3(screen_uv.xy, depth_uv);
    vec4 scattering_transmittance = vec4(0,0,0,0);

    if ( enable_volumetric_fog_opacity_tricubic_filtering() ) {
        scattering_transmittance = tricubic_filtering(volumetric_fog_texture_index, froxel_uvw, vec3(volumetric_fog_num_slices));
    }
    else {
        scattering_transmittance = texture(global_textures_3d[nonuniformEXT(volumetric_fog_texture_index)], froxel_uvw);
    }

    // Add animated noise to transmittance to remove banding.
    vec2 blue_noise = texture(global_textures[nonuniformEXT(blue_noise_128_rg_texture_index)], screen_uv ).rg;
    const float k_golden_ratio_conjugate = 0.61803398875;
    float blue_noise0 = fract(ToLinear1(blue_noise.r) + float(current_frame % 256) * k_golden_ratio_conjugate);
    float blue_noise1 = fract(ToLinear1(blue_noise.g) + float(current_frame % 256) * k_golden_ratio_conjugate);

    float noise_modifier = triangular_noise(blue_noise0, blue_noise1) * volumetric_fog_application_dithering_scale;
    scattering_transmittance.a += noise_modifier;

    const float scattering_modifier = enable_volumetric_fog_opacity_anti_aliasing() ? max( 1 - scattering_transmittance.a, 0.00000001f ) : 1.0f;

    color.rgb = color.rgb * scattering_transmittance.a + scattering_transmittance.rgb * scattering_modifier;

    return color;
}

// NOTE(marco): from https://en.wikipedia.org/wiki/De_Bruijn_sequence
uint get_lowest_bit( uint v ) {
  uint bit_position_lookup[32] =
  {
    0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
    31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
  };

  v = (v & -v) * 0x077CB531U;
  return bit_position_lookup[ v >> 27 ];
}

// NOTE(marco): compute binary mask from min_bit with mask_width bits set
uint bit_field_mask( uint mask_width, uint min_bit ) {
    uint last_bit = min_bit + mask_width;
    uint v = ( ( ( 1 << last_bit ) - 1) & ~( ( 1 << min_bit ) - 1) );

    return v;
}

vec4 calculate_lighting(vec4 base_colour, vec3 orm, vec3 normal, vec3 emissive, vec3 world_position, uvec2 position, vec2 screen_uv, bool transparent) {

    vec3 V = normalize( camera_position.xyz - world_position );
    const float NoV = saturate(dot(normal, V));

    const float metallic = forced_metalness < 0.0f ? orm.b : forced_metalness;
    // roughness = perceived roughness ^ 2
    const float roughness = forced_roughness < 0.0f ? orm.g * orm.g : forced_roughness;
    vec4 albedo = vec4(compute_diffuse_color(base_colour, metallic), base_colour.a);

    // TODO: missing IOR for F0 calculations. Get default value.
    vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);

    vec4 final_color = vec4( 0 );

    vec4 pos_camera_space = world_to_camera * vec4( world_position, 1.0 );

    float z_light_far = z_far;
    float linear_d = ( pos_camera_space.z - z_near ) / ( z_light_far - z_near );
    int bin_index = int( linear_d / BIN_WIDTH );
    uint bin_value = bins[ bin_index ];

    uint min_light_id = bin_value & 0xFFFF;
    uint max_light_id = ( bin_value >> 16 ) & 0xFFFF;

    uvec2 tile = position / uint( TILE_SIZE );

    uint stride = uint( NUM_WORDS ) * ( uint( resolution.x ) / uint( TILE_SIZE ) );
    uint address = tile.y * stride + tile.x;

#if ENABLE_OPTIMIZATION
    // NOTE(marco): this version has been implemented following:
    // https://www.activision.com/cdn/research/2017_Sig_Improved_Culling_final.pdf
    // See the presentation for more details

    // NOTE(marco): get the minimum and maximum light index across all threads of the wave. From this point,
    // these values are stored in scalar registers and we avoid storing them in vector registers
    uint merged_min = subgroupBroadcastFirst( subgroupMin( min_light_id ) );
    uint merged_max = subgroupBroadcastFirst( subgroupMax( max_light_id ) );

    uint word_min = max( merged_min / 32, 0 );
    uint word_max = min( merged_max / 32, NUM_WORDS );

    for ( uint word_index = word_min; word_index <= word_max; ++word_index ) {
        uint mask = tiles[ address + word_index ];

        // NOTE(marco): compute the minimum light id for this word and how many lights
        // are active in this word
        uint local_min = clamp( int( min_light_id ) - int( ( word_index * 32 ) ), 0, 31 );
        uint mask_width = clamp( int( max_light_id ) - int( min_light_id ) + 1, 0, 32 );

        // NOTE(marco): either the word is "full" or we need to compute the bit mask
        // with bit sets from local_min and local_min + mask_width
        uint zbin_mask = ( mask_width == 32 ) ? uint(0xFFFFFFFF) : bit_field_mask( mask_width, local_min );
        mask &= zbin_mask;

        // NOTE(marco): compute ORed mask across all threads. The while loop below can then use scalar
        // registers as we know the value will be uniform across all threads
        uint merged_mask = subgroupBroadcastFirst( subgroupOr( mask ) );

        while ( merged_mask != 0) {
            uint bit_index = get_lowest_bit( merged_mask );
            uint light_index = 32 * word_index + bit_index;

            merged_mask ^= ( 1 << bit_index );

            uint global_light_index = light_indices[ light_index ];

            final_color.rgb += calculate_point_light_contribution( albedo, roughness, normal, emissive, world_position, V, F0, NoV, position, global_light_index );
        }
    }
#else
    if ( min_light_id != NUM_LIGHTS + 1 ) {
        for ( uint light_id = min_light_id; light_id <= max_light_id; ++light_id ) {
            uint word_id = light_id / 32;
            uint bit_id = light_id % 32;

            if ( ( tiles[ address + word_id ] & ( 1 << bit_id ) ) != 0 ) {
                uint global_light_index = light_indices[ light_id ];

                final_color.rgb += calculate_point_light_contribution( albedo, roughness, normal, emissive, world_position, V, F0, NoV, position, global_light_index );
            }
        }
    }
#endif // ENABLE_OPTIMIZATION

    if (!transparent) {
        // Pointlight
        if ( is_raytrace_shadow_point_light() ) {
            final_color.rgb += calculate_raytraced_point_light_contribution( albedo, roughness, normal, emissive, world_position, V, F0, NoV, position );
        }
        else {
            // Directional light
            // NOTE(marco): we compute ray-traced directional lighting only for opaque objects
            final_color.rgb += calculate_directional_light_contribution( albedo, roughness, normal, emissive, world_position, V, F0, NoV, position, raytraced_shadow_light_position );
        }
    }
    
    // Ambient term
    vec3 F = fresnel_schlick_roughness(max(dot(normal, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    vec2 upsampling_weights = textureLod(global_textures[nonuniformEXT(bilateral_weights_texture_index)], screen_uv, 0).rg;
    vec3 indirect_irradiance = textureLod(global_textures[nonuniformEXT(indirect_lighting_texture_index)], screen_uv, 0).rgb;
    vec3 indirect_diffuse = indirect_irradiance * gi_intensity * base_colour.rgb;

    const float ao = 1.0f;
    final_color.rgb += (kD * indirect_diffuse) * ao;

#if defined(DEBUG_OPTIONS)

    if ( debug_show_light_tiles > 0 ) {
        uint v = 0;
        for ( int i = 0; i < NUM_WORDS; ++i ) {
            v += tiles[ address + i];
        }

        if ( v != 0 ) {
            final_color.rgb += vec3(1, 1, 0);
        }
    }

    if ( debug_show_tiles > 0 ) {
        uint mhash = hash( address );
        final_color.rgb *= vec3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;
    }

    if ( debug_show_bins > 0 ) {
        uint bin_hash = hash( bin_index );
        final_color.rgb = vec3(float(bin_hash & 255), float((bin_hash >> 8) & 255), float((bin_hash >> 16) & 255)) / 255.0;
    }

#endif // DEBUG_OPTIONS

    final_color.rgb += emissive;
    final_color.a = albedo.a;

    return vec4( encode_srgb( final_color.rgb ), final_color.a );
}

#endif // RAPTOR_GLSL_LIGHTING_H
