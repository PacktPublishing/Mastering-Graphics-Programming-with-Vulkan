

layout (location = 0) in vec3 position;
layout (location = 3) in vec2 texCoord0;
layout (location = 4) in ivec4 jointIndices;
layout (location = 5) in vec4 jointWeights;

layout (location = 0) out vec2 vTexcoord0;
layout (location = 1) out flat uint mesh_draw_index;

layout(std430, set = MATERIAL_SET, binding = 3) readonly buffer JointMatrices {
    mat4 joint_matrices[];
};

void main() {

    MeshInstanceDraw mesh_draw = mesh_instance_draws[gl_InstanceIndex];
    mesh_draw_index = mesh_draw.mesh_draw_index;

    mat4 skinning_transform = 
        jointWeights.x * joint_matrices[(jointIndices.x)] +
        jointWeights.y * joint_matrices[(jointIndices.y)] +
        jointWeights.z * joint_matrices[(jointIndices.z)] +
        jointWeights.w * joint_matrices[(jointIndices.w)];

    // Better to separate multiplications to minimize precision issues, visible as Z-Fighting.
    vec4 worldPosition = mesh_draw.model * skinning_transform * vec4(position, 1.0);

    gl_Position = view_projection * worldPosition;

    vTexcoord0 = texCoord0;
}
