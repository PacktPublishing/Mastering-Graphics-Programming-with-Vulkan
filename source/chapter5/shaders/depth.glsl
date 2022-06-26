

#if defined(VERTEX)

layout(location=0) in vec3 position;

void main() {
    gl_Position = view_projection * model * vec4(position, 1.0);
}

#endif // VERTEX


#if defined (FRAGMENT)

layout (location = 0) in vec2 vTexcoord0;

void main() {

    float texture_alpha = texture(global_textures[nonuniformEXT(textures.x)], vTexcoord0).a;

    bool useAlphaMask = (flags & DrawFlags_AlphaMask) != 0;
    if (useAlphaMask && texture_alpha < alpha_cutoff) {
        discard;
    }

    bool use_alpha_dither = (flags & DrawFlags_AlphaDither) != 0;
    if ( use_alpha_dither ) {
        float dithered_alpha = dither(gl_FragCoord.xy, texture_alpha);
    	if (dithered_alpha < 0.001f) {
            discard;
        }
    }
}

#endif // FRAGMENT