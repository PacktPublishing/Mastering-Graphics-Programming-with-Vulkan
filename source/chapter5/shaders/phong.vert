#version 450

layout ( std140, set = MATERIAL_SET, binding = 0 ) uniform LocalConstants {
    mat4        view_projection;
    vec4        eye;
    vec4        light;
    float       light_range;
    float       light_intensity;
};

layout ( std140, set = MATERIAL_SET, binding = 1 ) uniform Mesh {

    mat4        model;
    mat4        model_inverse;

    // x = diffuse index, y = roughness index, z = normal index, w = occlusion index.
    // Occlusion and roughness are encoded in the same texture
    uvec4       textures;
    vec4        diffuse;
    vec3        specular;
    float       specular_exp;
    vec3        ambient;
};

layout(location=0) in vec3 position;
layout(location=1) in vec4 tangent;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 texCoord0;

layout (location = 0) out vec2 vTexcoord0;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec3 vTangent;
layout (location = 3) out vec3 vBiTangent;
layout (location = 4) out vec3 vPosition;

void main() {
    vec4 worldPosition = model * vec4(position, 1.0);
    gl_Position = view_projection * worldPosition;
    vPosition = worldPosition.xyz / worldPosition.w;
    vTexcoord0 = texCoord0;
    vNormal = normalize( mat3(model_inverse) * normal );
    vTangent = normalize( mat3(model) * tangent.xyz );
    vBiTangent = cross( vNormal, vTangent ) * tangent.w;
}
