
#ifndef RAPTOR_GLSL_LIGHTING_H
#define RAPTOR_GLSL_LIGHTING_H

#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

// NOTE(marco): needs to be kept in sync with k_light_z_bins
// TODO(marco): use push constant
#define NUM_BINS 16.0
#define BIN_WIDTH ( 1.0 / NUM_BINS )
#define TILE_SIZE 8
#define NUM_LIGHTS 256
#define NUM_WORDS ( ( NUM_LIGHTS + 31 ) / 32 )

#define DEBUG_SHOW_LIGHT_TILES 0
#define DEBUG_SHOW_TILES 0
#define ENABLE_OPTIMIZATION 1

struct Light {
    vec3            world_position;
    float           attenuation;

    vec3            color;
    float           intensity;
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

layout( set = MATERIAL_SET, binding = 25 ) readonly buffer LightIndices {
    uint light_indices[];
};

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

// BRDF //////////////////////////////////////////////////////////////////
float fd_lambert() {
    return 1.0 / PI;
}

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

vec3 f_schlick(const vec3 f0, float VoH) {
    float f = pow(1.0 - VoH, 5.0);
    return f + f0 * (1.0 - f);
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

vec3 calculate_point_light_contribution(vec4 albedo, vec3 orm, vec3 normal, vec3 emissive, vec3 world_position, vec3 v, vec3 F0, float NoV, Light light) {

    const vec3 position_to_light = light.world_position - world_position;
    const vec3 l = normalize( position_to_light );
    const float NoL = clamp(dot(normal, l), 0.0, 1.0);

    vec3 pixel_luminance = vec3(0);

    float attenuation = attenuation_square_falloff(position_to_light, 1.0f / light.attenuation);
    if ( attenuation > 0.0001f && NoL > 0.0001f ) {

        float light_intensity = NoL * light.intensity * attenuation;

        const vec3 h = normalize(v + l);
        const float NoH = saturate(dot(normal, h));
        const float LoH = saturate(dot(l, h));

        const vec3 diffuse = fd_lambert() * albedo.rgb;
        const float D = d_ggx( orm.g, NoH, h );

        float V = v_smith_ggx_correlated_fast( orm.g, NoV, NoL );

        const float VoH = saturate(dot(v, h));
        vec3 F = f_schlick(F0, VoH);

        vec3 specular = (D * V) * F;

        pixel_luminance = (diffuse + specular) * light_intensity * light.color;
    }

    return pixel_luminance;
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

vec4 calculate_lighting(vec4 base_colour, vec3 orm, vec3 normal, vec3 emissive, vec3 world_position) {

    // Light point_light;
    // point_light.world_position = light0_data0.xyz;
    // point_light.attenuation = light0_data0.w;
    // point_light.color = light0_data1.xyz;
    // point_light.intensity = light0_data1.w;

    vec3 V = normalize( camera_position.xyz - world_position );
    const float NoV = saturate(dot(normal, V));

    vec4 albedo = vec4(compute_diffuse_color(base_colour, orm.b), base_colour.a);

    // TODO: missing IOR for F0 calculations. Get default value.
    const float metallic = orm.b;
    vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);

    vec4 final_color = vec4( 0 );

    vec4 pos_camera_space = world_to_camera * vec4( world_position, 1.0 );

    float z_light_far = 100.0f;
    float linear_d = ( -pos_camera_space.z - z_near ) / ( z_light_far - z_near );
    int bin_index = int( linear_d / BIN_WIDTH );
    uint bin_value = bins[ bin_index ];

    uint min_light_id = bin_value & 0xFFFF;
    uint max_light_id = ( bin_value >> 16 ) & 0xFFFF;

#if defined(COMPUTE_COMPUTE)
    uvec2 position = gl_GlobalInvocationID.xy;
#endif

#if defined(COMPUTE_FRAGMENT) || defined(FRAGMENT_MAIN) || defined (FRAGMENT_TRANSPARENT_NO_CULL) || defined(FRAGMENT_TRANSPARENT_CULL) || defined(FRAGMENT_TRANSPARENT_SKINNING_NO_CULL)
    // NOTE(marco): integer fragment position and top-left origin
    uvec2 position = uvec2(gl_FragCoord.x - 0.5, gl_FragCoord.y - 0.5);
    position.y = uint( resolution.y ) - position.y;
#endif

    uvec2 tile = position / uint( TILE_SIZE );

    uint stride = uint( NUM_WORDS ) * ( uint( resolution.x ) / uint( TILE_SIZE ) );
    uint address = tile.y * stride + tile.x * uint( NUM_WORDS );

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
        uint mask_width = clamp( int( max_light_id ) - int( min_light_id ), 0, 32 );

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
            Light point_light = lights[ global_light_index ];
            final_color.rgb += calculate_point_light_contribution( albedo, orm, normal, emissive, world_position, V, F0, NoV, point_light );
        }
    }
#else
    if ( min_light_id != NUM_LIGHTS + 1 ) {
        for ( uint light_id = min_light_id; light_id <= max_light_id; ++light_id ) {
            uint word_id = light_id / 32;
            uint bit_id = light_id % 32;

            if ( ( tiles[ address + word_id ] & ( 1 << bit_id ) ) != 0 ) {
                uint global_light_index = light_indices[ light_id ];
                Light point_light = lights[ global_light_index ];

                final_color.rgb += calculate_point_light_contribution( albedo, orm, normal, emissive, world_position, V, F0, NoV, point_light );
            }
        }
    }
#endif

    // final_color.rgb = vec3( linear_d );
    // final_color.rgb = vec3( float(bin_index) / NUM_BINS );

#if DEBUG_SHOW_LIGHT_TILES
    uint v = 0;
    for ( int i = 0; i < NUM_WORDS; ++i ) {
        v += tiles[ address + i];
    }

    if ( v != 0 ) {
        final_color.rgb += vec3(1, 1, 0);
    }
#endif

#if DEBUG_SHOW_TILES
    uint mhash = hash( address );
    final_color.rgb *= vec3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;
#endif

    final_color.rgb += emissive;
    final_color.a = albedo.a;

    return vec4( encode_srgb( final_color.rgb ), final_color.a );
}

#endif // RAPTOR_GLSL_LIGHTING_H
