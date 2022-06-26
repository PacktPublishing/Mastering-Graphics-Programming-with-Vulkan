
#if defined(VERTEX)

layout(location=0) in vec3 position;
layout(location=1) in vec4 tangent;
layout(location=2) in vec3 normal;
layout(location=3) in vec2 texCoord0;
layout (location = 4) in ivec4 jointIndices;
layout (location = 5) in vec4 jointWeights;

layout (location = 0) out vec2 vTexcoord0;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec3 vTangent;
layout (location = 3) out vec3 vBiTangent;
layout (location = 4) out vec3 vPosition;

layout(std430, set = MATERIAL_SET, binding = 3) readonly buffer JointMatrices {
	mat4 joint_matrices[];
};

void main() {

	mat4 skinning_transform = 
		jointWeights.x * joint_matrices[(jointIndices.x)] +
		jointWeights.y * joint_matrices[(jointIndices.y)] +
		jointWeights.z * joint_matrices[(jointIndices.z)] +
		jointWeights.w * joint_matrices[(jointIndices.w)];

	// Better to separate multiplications to minimize precision issues, visible as Z-Fighting.
    vec4 worldPosition = model * skinning_transform * vec4(position, 1.0);

    gl_Position = view_projection * worldPosition;
    
    vPosition = worldPosition.xyz / worldPosition.w;
    vTexcoord0 = texCoord0;
    
    vNormal = normalize( mat3(model_inverse) * normal );
    vTangent = normalize( mat3(model) * tangent.xyz );
    vBiTangent = cross( vNormal, vTangent ) * tangent.w;
}

#endif // VERTEX