#if defined(COMPUTE_SHADOW_VISIBILITY_VARIANCE) || defined (COMPUTE_SHADOW_VISIBILITY) || defined(COMPUTE_SHADOW_VISIBILITY_FILTERING)

layout ( std140, set = MATERIAL_SET, binding = 30 ) uniform ShadowVisibilityConstants {
    uint visibility_cache_texture_index;
    uint variation_texture_index;
    uint variation_cache_texture_index;
    uint samples_count_cache_texture_index;

    uint motion_vectors_texture_index;
    uint normals_texture_index;
    uint filtered_visibility_texture;
    uint filetered_variation_texture;

    uint frame_index; // NOTE(marco): [0-3]
    float resolution_scale;
    float resolution_scale_rcp;
    uint pad_002_svc;
};

#endif

#if defined(VERTEX_DEFERRED_LIGHTING_PIXEL)

layout(location=0) in vec3 position;

layout (location = 0) out vec2 vTexcoord0;

void main() {

    vTexcoord0.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vTexcoord0.xy * 2.0f - 1.0f, 0.0f, 1.0f);
    gl_Position.y = -gl_Position.y;
}

#endif // VERTEX

#if defined (FRAGMENT_DEFERRED_LIGHTING_PIXEL)

#extension GL_EXT_fragment_shading_rate : enable

layout (location = 0) in vec2 vTexcoord0;

layout (location = 0) out vec4 frag_color;

// TODO(marco): refactor
layout ( std140, set = MATERIAL_SET, binding = 1 ) uniform LightingConstants {

    // x = albedo index, y = roughness index, z = normal index, w = position index.
    // Occlusion and roughness are encoded in the same texture
    uvec4       textures;

    uint        output_index;   // Used by compute
    uint        output_width;
    uint        output_height;
    uint        emissive_index;
};

void main() {
    vec4 base_colour = texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0);
    vec3 orm = texture(global_textures[nonuniformEXT(textures.y)], vTexcoord0).rgb;
    vec2 encoded_normal = texture(global_textures[nonuniformEXT(textures.z)], vTexcoord0).rg;
    vec3 normal = octahedral_decode(encoded_normal);
    vec3 emissive = texture(global_textures[nonuniformEXT(emissive_index)], vTexcoord0).rgb;

    vec4 color = vec4(0);

    const vec2 screen_uv = uv_nearest(ivec2( gl_FragCoord.xy ), resolution);
    const float raw_depth = texelFetch(global_textures[nonuniformEXT(textures.w)], ivec2( gl_FragCoord.xy ), 0).r;
    if ( raw_depth == 1.0f ) {
        color = vec4( base_colour.rgb, 1 );
    }
    else {
        const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

        uvec2 position = uvec2(gl_FragCoord.x - 0.5, gl_FragCoord.y - 0.5);
        position.y = uint( resolution.y ) - position.y;

        color = calculate_lighting( base_colour, orm, normal, emissive, pixel_world_position, position, screen_uv, false );
    }

    color.rgb = apply_volumetric_fog( screen_uv, raw_depth, color.rgb );

#if 0
    vec3 sr_color = vec3( 0.5, 0, 0 );

    bool has_2vert_rate = ( gl_ShadingRateEXT & gl_ShadingRateFlag2VerticalPixelsEXT ) != 0;
    bool has_2hor_rate = ( gl_ShadingRateEXT & gl_ShadingRateFlag2HorizontalPixelsEXT ) != 0;

    if ( has_2vert_rate && has_2hor_rate )
        sr_color = vec3( 0, 0.5, 0 );

    if ( has_2hor_rate )
        sr_color = vec3( 0, 0, 0.5 );

    if ( has_2vert_rate )
        sr_color = vec3( 0.5, 0.5, 0 );

    color.rgb += ( sr_color * 0.2 );
#endif
    frag_color = color;
}

#endif // FRAGMENT

#if defined(COMPUTE_DEFERRED_LIGHTING_COMPUTE)

// TODO(marco): refactor
layout ( std140, set = MATERIAL_SET, binding = 1 ) uniform LightingConstants {

    // x = albedo index, y = roughness index, z = normal index, w = position index.
    // Occlusion and roughness are encoded in the same texture
    uvec4       textures;

    uint        output_index;   // Used by compute
    uint        output_width;
    uint        output_height;
    uint        emissive_index;
};

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main() {
    ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);

    vec4 base_colour = texelFetch(global_textures[nonuniformEXT(textures.x)], pos.xy, 0);
    vec3 orm = texelFetch(global_textures[nonuniformEXT(textures.y)], pos.xy, 0).rgb;
    vec2 encoded_normal = texelFetch(global_textures[nonuniformEXT(textures.z)], pos.xy, 0).rg;
    vec3 normal = octahedral_decode(encoded_normal);
    vec3 emissive = texelFetch(global_textures[nonuniformEXT(emissive_index)], pos.xy, 0).rgb;

    vec4 color = vec4(0);

    const vec2 screen_uv = uv_nearest(pos.xy, resolution);
    const float raw_depth = texelFetch(global_textures[nonuniformEXT(textures.w)], pos.xy, 0).r;
    if ( raw_depth == 1.0f ) {
        color = vec4(base_colour.rgb, 1);
    }
    else {
        const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

        uvec2 position = gl_GlobalInvocationID.xy;

        color = calculate_lighting( base_colour, orm, normal, emissive, pixel_world_position, position, screen_uv, false );
    }

    color.rgb = apply_volumetric_fog( screen_uv, raw_depth, color.rgb );

    // DEBUG:
    if ( debug_modes > 0 ) {

        if ( debug_modes == 1 ) {
            const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

            imageStore(global_images_2d[debug_texture_index], pos.xy, vec4(pixel_world_position, 1));
        }
        else if (debug_modes == 2) {

            const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

            Light light = lights[ 0 ];
            vec3 position_to_light = light.world_position - pixel_world_position;
            const float current_depth = length(position_to_light) / light.radius;
            const float closest_depth = texture(global_textures_cubemaps[nonuniformEXT(cubemap_shadows_index)], vec3(position_to_light)).r;

            imageStore(global_images_2d[debug_texture_index], pos.xy, vec4(current_depth, closest_depth, vector_to_depth_value(position_to_light, light.radius, light.rcp_n_minus_f), 1));
        }
        else if (debug_modes == 3) {

            const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

            Light light = lights[ 0 ];
            const vec3 position_to_light = light.world_position - pixel_world_position;

            imageStore(global_images_2d[debug_texture_index], pos.xy, vec4(normalize(position_to_light), 1));
        }
        else if (debug_modes == 4) {

            const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

            Light light = lights[ 0 ];
            const vec3 position_to_light = pixel_world_position - light.world_position;

            imageStore(global_images_2d[debug_texture_index], pos.xy, vec4(normalize(position_to_light), 1));
        }
        else if (debug_modes == 5) {

            const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

            Light light = lights[ 0 ];
            vec3 position_to_light = pixel_world_position - light.world_position;
            const float current_depth = length(position_to_light) / light.radius;
            float current_depth2 = vector_to_depth_value(position_to_light, light.radius, light.rcp_n_minus_f);
            const float closest_depth = texture(global_textures_cubemaps[nonuniformEXT(cubemap_shadows_index)], vec3(position_to_light)).r;

            imageStore(global_images_2d[debug_texture_index], pos.xy, vec4(current_depth, current_depth2, closest_depth, 1));

            color.rgb = current_depth2 > closest_depth ? vec3(1) : vec3(0);
        }
        else if (debug_modes == 6) {

            const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

            Light light = lights[ 0 ];
            vec3 position_to_light = light.world_position - pixel_world_position;
            const float current_depth = length(position_to_light) / light.radius;
            float current_depth2 = vector_to_depth_value(position_to_light, light.radius, light.rcp_n_minus_f);
            const float closest_depth = texture(global_textures_cubemaps[nonuniformEXT(cubemap_shadows_index)], vec3(position_to_light)).r;

            imageStore(global_images_2d[debug_texture_index], pos.xy, vec4(current_depth, current_depth2, closest_depth, 1));
        }
        else if ( debug_modes == 7 ) {
            const vec3 pixel_world_position = world_position_from_depth(screen_uv, raw_depth, inverse_view_projection);

            Light light = lights[ 0 ];
            const vec3 position_to_light = pixel_world_position - light.world_position;

            // // Doing the max of the components tells us 2 things: which cubemap face we're going to use,
            // // and also what the projected distance is onto the major axis for that face.
            float projectedDistance = max(max(abs(position_to_light.x), abs(position_to_light.y)), abs(position_to_light.z));

            // fn = 1.0f / (nearZ - farZ);
            // dest[2][2] = (nearZ + farZ) * fn;
            // dest[3][2] = 2.0f * nearZ * farZ * fn;
            float fn = 1.0f / (0.01f - light.radius);
            float a = (0.01f + light.radius) * fn;
            float b = 2.0f * 0.01f * light.radius * fn;

            float z = projectedDistance * a + b;
            float dbDistance = z / projectedDistance;

            const float closest_depth = texture(global_textures_cubemaps[nonuniformEXT(cubemap_shadows_index)], vec3(position_to_light)).r;

            imageStore(global_images_2d[debug_texture_index], pos.xy, vec4(dbDistance, closest_depth, 0, 1));

        }
        else if (debug_modes == 8) {

            vec3 indirect_irradiance = textureLod(global_textures[nonuniformEXT(indirect_lighting_texture_index)], screen_uv, 0).rgb;
            color.rgb = indirect_irradiance;
        }
        else if (debug_modes == 9) {

            vec3 indirect_irradiance = textureLod(global_textures[nonuniformEXT(indirect_lighting_texture_index)], screen_uv, 0).rgb;
            color.rgb = indirect_irradiance * base_colour.rgb;
        }
    }

    imageStore(global_images_2d[output_index], pos.xy, color);
}

#endif // COMPUTE

#if defined(COMPUTE_SHADOW_VISIBILITY_VARIANCE)

layout ( local_size_x = 8, local_size_y = 8, local_size_z = 1 ) in;

void main() {
    ivec2 iresolution = ivec2( resolution * 0.5 );
    if ( gl_GlobalInvocationID.x >= iresolution.x || gl_GlobalInvocationID.y >= iresolution.y )
        return;

    ivec3 tex_coord = ivec3( gl_GlobalInvocationID.xyz );

    vec4 last_visibility_values = texelFetch( global_textures_3d[ visibility_cache_texture_index ], tex_coord, 0 );

    float max_v = max( max( max( last_visibility_values.x, last_visibility_values.y ), last_visibility_values.z ), last_visibility_values.w );
    float min_v = min( min( min( last_visibility_values.x, last_visibility_values.y ), last_visibility_values.z ), last_visibility_values.w );

    float delta = max_v - min_v;

    imageStore( global_images_3d[ variation_texture_index ], tex_coord, vec4( delta, 0, 0, 0 ) );
}

#endif // COMPUTE_SHADOW_VISIBILITY_VARIANCE

#if defined(COMPUTE_SHADOW_VISIBILITY)

#define GROUP_SIZE 8
#define LOCAL_DATA_SIZE ( GROUP_SIZE + 6 * 2 ) // NOTE(marco): we need to cover up to 13x13 around the pixel

layout ( local_size_x = GROUP_SIZE, local_size_y = GROUP_SIZE, local_size_z = 1 ) in;

shared float local_image_data[ LOCAL_DATA_SIZE ][ LOCAL_DATA_SIZE ];
shared float local_max_image_data[ LOCAL_DATA_SIZE ][ LOCAL_DATA_SIZE ];

float max_filter( ivec3 index ) {
    // NOTE(marco): visibility values should be between 0 and 1
    float max_v = -1.0;

    // NOTE(marco): 5x5 max filter
    for ( int y = -2; y <= 2; ++y  ) {
        for ( int x = -2; x <= 2; ++x ) {
            ivec2 xy = index.xy + ivec2( x, y );

            float v = local_image_data[ xy.y ][ xy.x ];
            max_v = max( max_v, v );
        }
    }

    return max_v;
}

float read_variation_value( ivec3 index ) {
    bool valid_index = ( any( lessThan( index.xy, ivec2( 0 ) ) ) || any( greaterThanEqual( index.xy, ivec2( resolution * 0.5 ) ) ) );

    float v = valid_index ? texelFetch( global_textures_3d[ variation_texture_index ], index, 0 ).r : 0.0;

    return v;
}

// TODO(marco): pass by buffer?
float tent_kernel[13][13] = {
    { 0.00041649, 0.00083299, 0.00124948, 0.00166597, 0.00208247, 0.00249896, 0.00291545, 0.00249896, 0.00208247, 0.00166597, 0.00124948, 0.00083299, 0.00041649 },
    { 0.00083299, 0.00166597, 0.00249896, 0.00333195, 0.00416493, 0.00499792, 0.0058309,  0.00499792, 0.00416493, 0.00333195, 0.00249896, 0.00166597, 0.00083299 },
    { 0.00124948, 0.00249896, 0.00374844, 0.00499792, 0.0062474,  0.00749688, 0.00874636, 0.00749688, 0.0062474,  0.00499792, 0.00374844, 0.00249896, 0.00124948 },
    { 0.00166597, 0.00333195, 0.00499792, 0.00666389, 0.00832986, 0.00999584, 0.01166181, 0.00999584, 0.00832986, 0.00666389, 0.00499792, 0.00333195, 0.00166597 },
    { 0.00208247, 0.00416493, 0.0062474,  0.00832986, 0.01041233, 0.01249479, 0.01457726, 0.01249479, 0.01041233, 0.00832986, 0.0062474,  0.00416493, 0.00208247 },
    { 0.00249896, 0.00499792, 0.00749688, 0.00999584, 0.01249479, 0.01499375, 0.01749271, 0.01499375, 0.01249479, 0.00999584, 0.00749688, 0.00499792, 0.00249896 },
    { 0.00291545, 0.0058309,  0.00874636, 0.01166181, 0.01457726, 0.01749271, 0.02040816, 0.01749271, 0.01457726, 0.01166181, 0.00874636, 0.0058309, 0.00291545 },
    { 0.00249896, 0.00499792, 0.00749688, 0.00999584, 0.01249479, 0.01499375, 0.01749271, 0.01499375, 0.01249479, 0.00999584, 0.00749688, 0.00499792, 0.00249896 },
    { 0.00208247, 0.00416493, 0.0062474,  0.00832986, 0.01041233, 0.01249479, 0.01457726, 0.01249479, 0.01041233, 0.00832986, 0.0062474,  0.00416493, 0.00208247 },
    { 0.00166597, 0.00333195, 0.00499792, 0.00666389, 0.00832986, 0.00999584, 0.01166181, 0.00999584, 0.00832986, 0.00666389, 0.00499792, 0.00333195, 0.00166597 },
    { 0.00124948, 0.00249896, 0.00374844, 0.00499792, 0.0062474,  0.00749688, 0.00874636, 0.00749688, 0.0062474,  0.00499792, 0.00374844, 0.00249896, 0.00124948 },
    { 0.00083299, 0.00166597, 0.00249896, 0.00333195, 0.00416493, 0.00499792, 0.0058309,  0.00499792, 0.00416493, 0.00333195, 0.00249896, 0.00166597, 0.00083299 },
    { 0.00041649, 0.00083299, 0.00124948, 0.00166597, 0.00208247, 0.00249896, 0.00291545, 0.00249896, 0.00208247, 0.00166597, 0.00124948, 0.00083299, 0.00041649 }
};

void main() {
    ivec2 iresolution = ivec2( resolution * 0.5 );
    if ( gl_GlobalInvocationID.x >= iresolution.x || gl_GlobalInvocationID.y >= iresolution.y )
        return;

    ivec3 local_index = ivec3( gl_LocalInvocationID.xyz ) + ivec3( 6, 6, 0 );
    ivec3 global_index = ivec3( gl_GlobalInvocationID.xyz );

    local_image_data[ local_index.y ][ local_index.x ] = texelFetch( global_textures_3d[ variation_texture_index ], global_index, 0 ).r;

    if ( gl_LocalInvocationID.x == 0 && gl_LocalInvocationID.y == 0 ) {
        for ( int y = -6; y <= -1; ++y  ) {
            for ( int x = -6; x <= -1; ++x ) {
                ivec3 offset = ivec3( x, y, 0 );
                ivec3 index = local_index + offset;
                ivec3 filter_index = global_index + offset;
                local_image_data[ index.y ][ index.x ] = read_variation_value( filter_index );
            }
        }
    }

    if ( gl_LocalInvocationID.x == 0 ) {
        for ( int i = -6; i <= -1; ++i ) {
            ivec3 offset = ivec3( i, 0, 0 );
            ivec3 index = local_index + offset;
            ivec3 filter_index = global_index + offset;
            local_image_data[ index.y ][ index.x ] = read_variation_value( filter_index );
        }
    }

    if ( gl_LocalInvocationID.y == 0 ) {
        for ( int i = -6; i <= -1; ++i ) {
            ivec3 offset = ivec3( 0, i, 0 );
            ivec3 index = local_index + offset;
            ivec3 filter_index = global_index + offset;
            local_image_data[ index.y ][ index.x ] = read_variation_value( filter_index );
        }
    }

    if ( gl_LocalInvocationID.x == ( GROUP_SIZE - 1 ) && gl_LocalInvocationID.y == ( GROUP_SIZE - 1 ) ) {
        for ( int y = 1; y <= 6; ++y  ) {
            for ( int x = 1; x <= 6; ++x ) {
                ivec3 offset = ivec3( x, y, 0 );
                ivec3 index = local_index + offset;
                ivec3 filter_index = global_index + offset;
                local_image_data[ index.y ][ index.x ] = read_variation_value( filter_index );
            }
        }
    }

    if ( gl_LocalInvocationID.x == ( GROUP_SIZE - 1 ) ) {
        for ( int i = 1; i <= 6; ++i ) {
            ivec3 offset = ivec3( i, 0, 0 );
            ivec3 index = local_index + offset;
            ivec3 filter_index = global_index + offset;
            local_image_data[ index.y ][ index.x ] = read_variation_value( filter_index );
        }
    }

    if ( gl_LocalInvocationID.y == ( GROUP_SIZE - 1 ) ) {
        for ( int i = 1; i <= 6; ++i ) {
            ivec3 offset = ivec3( 0, i, 0 );
            ivec3 index = local_index + offset;
            ivec3 filter_index = global_index + offset;
            local_image_data[ index.y ][ index.x ] = read_variation_value( filter_index );
        }
    }

    memoryBarrierShared();
    barrier();

    local_max_image_data[ local_index.y ][ local_index.x ] = max_filter( local_index );

    if ( gl_LocalInvocationID.x == 0 && gl_LocalInvocationID.y == 0 ) {
        for ( int y = -6; y <= -1; ++y  ) {
            for ( int x = -6; x <= -1; ++x ) {
                ivec3 offset = ivec3( x, y, 0 );
                ivec3 index = local_index + offset;
                local_max_image_data[ index.y ][ index.x ] = max_filter( index );
            }
        }
    }

    if ( gl_LocalInvocationID.x == 0 ) {
        for ( int i = -6; i <= -1; ++i ) {
            ivec3 offset = ivec3( i, 0, 0 );
            ivec3 index = local_index + offset;
            local_max_image_data[ index.y ][ index.x ] = max_filter( index );
        }
    }

    if ( gl_LocalInvocationID.y == 0 ) {
        for ( int i = -6; i <= -1; ++i ) {
            ivec3 offset = ivec3( 0, i, 0 );
            ivec3 index = local_index + offset;
            local_max_image_data[ index.y ][ index.x ] = max_filter( index );
        }
    }

    if ( gl_LocalInvocationID.x == ( GROUP_SIZE - 1 ) && gl_LocalInvocationID.y == ( GROUP_SIZE - 1 ) ) {
        for ( int y = 1; y <= 6; ++y  ) {
            for ( int x = 1; x <= 6; ++x ) {
                ivec3 offset = ivec3( x, y, 0 );
                ivec3 index = local_index + offset;
                local_max_image_data[ index.y ][ index.x ] = max_filter( index );
            }
        }
    }

    if ( gl_LocalInvocationID.x == ( GROUP_SIZE - 1 ) ) {
        for ( int i = 1; i <= 6; ++i ) {
            ivec3 offset = ivec3( i, 0, 0 );
            ivec3 index = local_index + offset;
            local_max_image_data[ index.y ][ index.x ] = max_filter( index );
        }
    }

    if ( gl_LocalInvocationID.y == ( GROUP_SIZE - 1 ) ) {
        for ( int i = 1; i <= 6; ++i ) {
            ivec3 offset = ivec3( 0, i, 0 );
            ivec3 index = local_index + offset;
            local_max_image_data[ index.y ][ index.x ] = max_filter( index );
        }
    }

    memoryBarrierShared();
    barrier();

    // NOTE(marco): 13x13 tent filter
    // TODO(marco): use separable version
    float spatial_filtered_value = 0.0;
    for ( int y = -6; y <= 6; ++y ) {
        for ( int x = -6; x <= 6; ++x ) {
            ivec2 index = local_index.xy + ivec2( x, y );
            float v = local_max_image_data[ index.y ][ index.x ];
            float f = tent_kernel[ y + 6 ][ x + 6 ];

            spatial_filtered_value += v * f;
        }
    }

    vec4 last_variation_values = texelFetch( global_textures_3d[ variation_cache_texture_index ], global_index, 0 );

    float filtered_value = 0.5 * ( spatial_filtered_value + 0.25 * ( last_variation_values.x + last_variation_values.y + last_variation_values.z + last_variation_values.w ) );

    last_variation_values.w = last_variation_values.z;
    last_variation_values.z = last_variation_values.y;
    last_variation_values.y = last_variation_values.x;
    last_variation_values.x = texelFetch( global_textures_3d[ variation_texture_index ], global_index, 0 ).r;

    ivec2 scaled_xy = ivec2( global_index.xy * resolution_scale_rcp );

    float motion_vectors_value = texelFetch( global_textures[ motion_vectors_texture_index ], scaled_xy.xy, 0 ).r;
    uvec4 sample_count_history = texelFetch( global_utextures_3d[ samples_count_cache_texture_index ], global_index, 0 );
    const float raw_depth = texelFetch( global_textures[ depth_texture_index ], scaled_xy.xy, 0).r;

    uint sample_count = MAX_SHADOW_VISIBILITY_SAMPLE_COUNT;
    if ( motion_vectors_value.r != -1.0 ) {
        sample_count = sample_count_history.x;

        bool stable_sample_count = ( sample_count_history.x == sample_count_history.y ) && ( sample_count_history.x == sample_count_history.z ) && ( sample_count_history.x == sample_count_history.w );

        if ( raw_depth == 1.0f ) {
            sample_count = 0;
        } else {
            float delta = 0.2;
            if ( filtered_value > delta && sample_count < MAX_SHADOW_VISIBILITY_SAMPLE_COUNT ) {
                sample_count += 1;
            } else if ( stable_sample_count && sample_count >= 1 && (filtered_value < delta) ) {
                sample_count -= 1;
            }

#if 0
            // NOTE(marco): this is the implementation described in the book. If we don't
            // sample for one frame, the output is very noisy and blocky. Always forcing at
            // least one sample reduces this issue. Needs further investigation.
            uvec4 new_sample_count_history = uvec4( sample_count, sample_count_history.x, sample_count_history.y, sample_count_history.z );
            bool zeroSampleHistory = all( lessThan( new_sample_count_history, uvec4( 1 ) ) );
            if ( zeroSampleHistory ) {
                // NOTE(marco): force this frame to have at least one sample
                sample_count = 1;
            }
#else
            if ( sample_count <= 0 ) {
                sample_count = 1;
            }
#endif
        }
    }

    float visibility = 0.0;
    if ( sample_count > 0 ) {
        const vec2 screen_uv = uv_nearest( global_index.xy, resolution * resolution_scale );
        const vec3 pixel_world_position = world_position_from_depth( screen_uv, raw_depth, inverse_view_projection);

        vec2 encoded_normal = texelFetch( global_textures[ normals_texture_index ], scaled_xy, 0).rg;
        vec3 normal = octahedral_decode( encoded_normal );

        // Point light
        if ( is_raytrace_shadow_point_light() ) {
            // NOTE(marco): this is the code in the book
            visibility = get_point_light_visibility( gl_GlobalInvocationID.z, sample_count, pixel_world_position, normal, frame_index );
        }
        else {
            visibility = get_directional_light_visibility( raytraced_shadow_light_position, sample_count, pixel_world_position, normal, frame_index );
        }
    }

    vec4 last_visibility_values = vec4(0);
    if ( motion_vectors_value.r != -1.0 ) {
        last_visibility_values = texelFetch( global_textures_3d[ visibility_cache_texture_index ], global_index, 0 );

        last_visibility_values.w = last_visibility_values.z;
        last_visibility_values.z = last_visibility_values.y;
        last_visibility_values.y = last_visibility_values.x;
    } else {
        last_visibility_values.w = visibility;
        last_visibility_values.z = visibility;
        last_visibility_values.y = visibility;
    }
    last_visibility_values.x = visibility;

    sample_count_history.w = sample_count_history.z;
    sample_count_history.z = sample_count_history.y;
    sample_count_history.y = sample_count_history.x;
    sample_count_history.x = sample_count;

    memoryBarrierImage(); // NOTE(marco): we are reading and writing from the same image
    barrier();

    imageStore( global_images_3d[ visibility_cache_texture_index ], global_index, last_visibility_values );

    imageStore( global_images_3d[ filetered_variation_texture ], global_index, vec4( spatial_filtered_value, 0, 0, 0 ) );

    imageStore( global_images_3d[ variation_cache_texture_index ], global_index, last_variation_values );

    imageStore( global_uimages_3d[ samples_count_cache_texture_index ], global_index, sample_count_history );
}

#endif // COMPUTE_SHADOW_VISIBILITY

#if defined(COMPUTE_SHADOW_VISIBILITY_FILTERING)

#define GROUP_SIZE 8
#define LOCAL_DATA_SIZE ( GROUP_SIZE + 2 * 2 )

layout ( local_size_x = GROUP_SIZE, local_size_y = GROUP_SIZE, local_size_z = 1 ) in;

// NOTE(marco): computed with script from https://stackoverflow.com/questions/29731726/how-to-calculate-a-gaussian-kernel-matrix-efficiently-in-numpy
float gaussian_kernel[5][5] = {
    { 0.00296902, 0.01330621, 0.02193823, 0.01330621, 0.00296902 },
    { 0.01330621, 0.0596343,  0.09832033, 0.0596343,  0.01330621 },
    { 0.02193823, 0.09832033, 0.16210282, 0.09832033, 0.02193823 },
    { 0.01330621, 0.0596343,  0.09832033, 0.0596343,  0.01330621 },
    { 0.00296902, 0.01330621, 0.02193823, 0.01330621, 0.00296902 }
};

shared float local_image_data[ LOCAL_DATA_SIZE ][ LOCAL_DATA_SIZE ];
shared vec3 local_normal_data[ LOCAL_DATA_SIZE ][ LOCAL_DATA_SIZE ];

float visibility_temporal_filter( ivec3 index ) {
    if ( any( lessThan( index.xy, ivec2( 0 ) ) ) || any( greaterThanEqual( index.xy, ivec2( resolution * 0.5 ) ) ) ) {
        return 0.0;
    }

    vec4 last_visibility_values = texelFetch( global_textures_3d[ visibility_cache_texture_index ], index, 0 );

    float filtered_visibility = 0.25 * ( last_visibility_values.x + last_visibility_values.y + last_visibility_values.z + last_visibility_values.w );

    return filtered_visibility;
}

vec3 get_normal( ivec3 index ) {

    ivec2 scaled_xy = ivec2( index.xy * resolution_scale_rcp );
    if ( any( lessThan( scaled_xy.xy, ivec2( 0 ) ) ) || any( greaterThanEqual( scaled_xy.xy, ivec2( resolution ) ) ) ) {
        return vec3( 0 );
    }

    const float raw_depth = texelFetch( global_textures[ depth_texture_index ], scaled_xy.xy, 0).r;
    if ( raw_depth == 1.0f ) {
        return vec3( 0 );
    } else {
        vec2 encoded_normal = texelFetch( global_textures[ normals_texture_index ], scaled_xy.xy, 0).rg;
        vec3 normal = octahedral_decode( encoded_normal );

        return normal;
    }
}

void main() {
    ivec2 iresolution = ivec2( resolution * 0.5 );
    if ( gl_GlobalInvocationID.x >= iresolution.x || gl_GlobalInvocationID.y >= iresolution.y )
        return;

    ivec3 local_index = ivec3( gl_LocalInvocationID.xyz ) + ivec3( 2, 2, 0 );
    ivec3 global_index = ivec3( gl_GlobalInvocationID.xyz );

    local_image_data[ local_index.y ][ local_index.x ] = visibility_temporal_filter( global_index );
    local_normal_data[ local_index.y ][ local_index.x ] = get_normal( global_index );

    if ( gl_LocalInvocationID.x == 0 && gl_LocalInvocationID.y == 0 ) {
        for ( int y = -2; y <= -1; ++y  ) {
            for ( int x = -2; x <= -1; ++x ) {
                ivec3 offset = ivec3( x, y, 0 );
                ivec3 index = local_index + offset;
                ivec3 filter_index = global_index + offset;
                local_image_data[ index.y ][ index.x ] = visibility_temporal_filter( filter_index );
                local_normal_data[ index.y ][ index.x ] = get_normal( filter_index );
            }
        }
    }

    if ( gl_LocalInvocationID.x == 0 ) {
        for ( int i = -2; i <= -1; ++i ) {
            ivec3 offset = ivec3( i, 0, 0 );
            ivec3 index = local_index + offset;
            ivec3 filter_index = global_index + offset;
            local_image_data[ index.y ][ index.x ] = visibility_temporal_filter( filter_index );
            local_normal_data[ index.y ][ index.x ] = get_normal( filter_index );
        }
    }

    if ( gl_LocalInvocationID.y == 0 ) {
        for ( int i = -2; i <= -1; ++i ) {
            ivec3 offset = ivec3( 0, i, 0 );
            ivec3 index = local_index + offset;
            ivec3 filter_index = global_index + offset;
            local_image_data[ index.y ][ index.x ] = visibility_temporal_filter( filter_index );
            local_normal_data[ index.y ][ index.x ] = get_normal( filter_index );
        }
    }

    if ( gl_LocalInvocationID.x == ( GROUP_SIZE - 1 ) && gl_LocalInvocationID.y == ( GROUP_SIZE - 1 ) ) {
        for ( int y = 1; y <= 2; ++y  ) {
            for ( int x = 1; x <= 2; ++x ) {
                ivec3 offset = ivec3( x, y, 0 );
                ivec3 index = local_index + offset;
                ivec3 filter_index = global_index + offset;
                local_image_data[ index.y ][ index.x ] = visibility_temporal_filter( filter_index );
                local_normal_data[ index.y ][ index.x ] = get_normal( filter_index );
            }
        }
    }

    if ( gl_LocalInvocationID.x == ( GROUP_SIZE - 1 ) ) {
        for ( int i = 1; i <= 2; ++i ) {
            ivec3 offset = ivec3( i, 0, 0 );
            ivec3 index = local_index + offset;
            ivec3 filter_index = global_index + offset;
            local_image_data[ index.y ][ index.x ] = visibility_temporal_filter( filter_index );
            local_normal_data[ index.y ][ index.x ] = get_normal( filter_index );
        }
    }

    if ( gl_LocalInvocationID.y == ( GROUP_SIZE - 1 ) ) {
        for ( int i = 1; i <= 2; ++i ) {
            ivec3 offset = ivec3( 0, i, 0 );
            ivec3 index = local_index + offset;
            ivec3 filter_index = global_index + offset;
            local_image_data[ index.y ][ index.x ] = visibility_temporal_filter( filter_index );
            local_normal_data[ index.y ][ index.x ] = get_normal( filter_index );
        }
    }

    memoryBarrierShared();
    barrier();

    float spatial_filtered_value = 0.0;
    vec3 p_normal = local_normal_data[ local_index.y ][ local_index.x ];
    for ( int y = -2; y <= 2; ++y ) {
        for ( int x = -2; x <= 2; ++x ) {
            ivec2 index = local_index.xy + ivec2( x, y );

            vec3 q_normal = local_normal_data[ local_index.y + y ][ local_index.x + x ];

            if ( dot( p_normal, q_normal ) <= 0.9 ) {
                continue;
            }

            float v = local_image_data[ index.y ][ index.x ];
            float k = gaussian_kernel[ y + 2 ][ x + 2 ];

            spatial_filtered_value += v * k;
        }
    }

    imageStore( global_images_3d[ filtered_visibility_texture ], global_index, vec4( spatial_filtered_value, 0, 0, 0 ) );
}

#endif // COMPUTE_SHADOW_VISIBILITY_FILTERING

#if defined(COMPUTE_EDGE_DETECTION)

// TODO(marco): this should changed based on VkPhysicalDeviceFragmentShadingRatePropertiesKHR::minFragmentShadingRateAttachmentTexelSize
#define GROUP_SIZE 16
#define LOCAL_DATA_SIZE ( GROUP_SIZE + 2 )

layout ( local_size_x = GROUP_SIZE, local_size_y = GROUP_SIZE, local_size_z = 1 ) in;

layout( set = MATERIAL_SET, binding = 2 ) readonly buffer FragmentShadingRateImage {
    uint color_image_index;
    uint fsr_image_index;
};

shared float local_image_data[ LOCAL_DATA_SIZE ][ LOCAL_DATA_SIZE ];
shared uint min_rate;

void main() {
    ivec2 iresolution = ivec2( resolution ) - 1;
    if ( gl_GlobalInvocationID.x > iresolution.x || gl_GlobalInvocationID.y > iresolution.y )
        return;

    ivec2 local_index = ivec2( gl_LocalInvocationID.xy ) + ivec2( 1, 1 );
    ivec2 global_index = ivec2( gl_GlobalInvocationID.xy );

    local_image_data[ local_index.y ][ local_index.x ] = luminance( texelFetch( global_textures[ color_image_index ], global_index, 0 ).rgb );

    if ( local_index.x == 1 && local_index.y == 1 ) {
        local_image_data[ local_index.y - 1 ][ local_index.x - 1 ] = luminance( texelFetch( global_textures[ color_image_index ], clamp( global_index + ivec2( -1, -1 ), ivec2( 0 ), iresolution ), 0 ).rgb );
    }

    if ( local_index.x == 1 ) {
        local_image_data[ local_index.y ][ local_index.x - 1 ] = luminance( texelFetch( global_textures[ color_image_index ], clamp( global_index + ivec2( -1, 0 ), ivec2( 0 ), iresolution ), 0 ).rgb );
    }

    if ( local_index.y == 1 ) {
        local_image_data[ local_index.y - 1 ][ local_index.x ] = luminance( texelFetch( global_textures[ color_image_index ], clamp( global_index + ivec2( 0, -1 ), ivec2( 0 ), iresolution ), 0 ).rgb );
    }

    if ( local_index.x == GROUP_SIZE && local_index.y == GROUP_SIZE ) {
        local_image_data[ local_index.y + 1 ][ local_index.x + 1 ] = luminance( texelFetch( global_textures[ color_image_index ], clamp( global_index + ivec2( 1, 1 ), ivec2( 0 ), iresolution ), 0 ).rgb );
    }

    if ( local_index.x == GROUP_SIZE ) {
        local_image_data[ local_index.y ][ local_index.x + 1 ] = luminance( texelFetch( global_textures[ color_image_index ], clamp( global_index + ivec2( 1, 0 ), ivec2( 0 ), iresolution ), 0 ).rgb );
    }

    if ( local_index.y == GROUP_SIZE ) {
        local_image_data[ local_index.y + 1 ][ local_index.x ] = luminance( texelFetch( global_textures[ color_image_index ], clamp( global_index + ivec2( 0, 1 ), ivec2( 0 ), iresolution ), 0 ).rgb );
    }

    barrier();

    float normalization = 1.0; // 0.125;

    // Horizontal filter
    float dx =     local_image_data[ local_index.y - 1 ][ local_index.x - 1 ] -
                   local_image_data[ local_index.y - 1 ][ local_index.x + 1 ] +
               2 * local_image_data[ local_index.y     ][ local_index.x - 1 ] -
               2 * local_image_data[ local_index.y     ][ local_index.x + 1 ] +
                   local_image_data[ local_index.y + 1 ][ local_index.x - 1 ] -
                   local_image_data[ local_index.y + 1 ][ local_index.x + 1 ];

    dx *= normalization;

    // Vertical filter
    float dy =     local_image_data[ local_index.y - 1 ][ local_index.x - 1 ] +
               2 * local_image_data[ local_index.y - 1 ][ local_index.x     ] +
                   local_image_data[ local_index.y - 1 ][ local_index.x + 1 ] -
                   local_image_data[ local_index.y + 1 ][ local_index.x - 1 ] -
               2 * local_image_data[ local_index.y + 1 ][ local_index.x     ] -
                   local_image_data[ local_index.y + 1 ][ local_index.x + 1 ];

    dy *= normalization;

    float d = pow( dx, 2 ) + pow( dy, 2 );

    // NOTE(marco): 2x2 rate
    uint rate = 1 << 2 | 1;

    if ( d > 0.1 ) {
        // NOTE(marco): 1x1 rate
        rate = 0;
    }

    // TODO(marco): also use 1x2 and 2x1 rates

    atomicMin( min_rate, rate );

    barrier();

    if ( gl_LocalInvocationID.xy == uvec2( 0, 0 ) ) {
        imageStore( global_uimages_2d[ fsr_image_index ], ivec2( gl_GlobalInvocationID.xy / GROUP_SIZE ), uvec4( rate, 0, 0, 0 ) );
    }
}

#endif // EDGE_DETECTION
