
#if defined(VERTEX)

layout ( std140, set = MATERIAL_SET, binding = 0 ) uniform LocalConstants {
    mat4        view_projection;
    vec4        eye;
    vec4        light;
    float       light_range;
    float       light_intensity;
};

layout(location=0) in vec3 position;

void main() {
    gl_Position = view_projection * model * vec4(position, 1.0);
}

#endif // VERTEX