struct PhysicsVertex {
    vec3 position;
    vec3 start_position;
    vec3 previous_position;
    vec3 normal;
    uint joint_count;
    vec3 velocity;
    float mass;
    vec3 force; // TODO(marco): maybe we can remove this
    uint joints[ 12 ];
};

layout ( set = MATERIAL_SET, binding = 1 ) buffer PhysicsMesh {
    uint index_count;
    uint vertex_count;

    PhysicsVertex physics_vertices[];
};

#if defined(VERTEX)

layout(location=0) in vec3 position;

void main() {
    gl_Position = view_projection * vec4( ( position * vec3( 0.02, 0.02, 0.02 ) + physics_vertices[ gl_InstanceIndex ].position ), 1.0);
}

#endif // VERTEX


#if defined (FRAGMENT)

layout (location = 0) out vec4 colour;

void main() {

    colour = vec4( 0.2, 0.7, 0.2, 1.0 );
}

#endif // FRAGMENT
