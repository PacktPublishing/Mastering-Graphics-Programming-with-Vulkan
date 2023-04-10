
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

layout ( std140, set = MATERIAL_SET, binding = 0 ) uniform PhysicsData {
    vec3 wind_direction;
    uint reset_simulation;

    float air_density;
    float spring_stiffness;
    float spring_damping;
};

layout ( set = MATERIAL_SET, binding = 1 ) buffer PhysicsMesh {
    uint index_count;
    uint vertex_count;

    PhysicsVertex physics_vertices[];
};

// NOTE(marco): we can't use vec3 for positions and normals, it will pad each entry and
// break the attribute buffer!
layout ( set = MATERIAL_SET, binding = 2 ) buffer PositionData {
    float positions[];
};

layout ( set = MATERIAL_SET, binding = 3 ) buffer NormalData {
    float normals[];
};

layout ( set = MATERIAL_SET, binding = 4 ) readonly buffer IndexData {
    uint indices[];
};


#if defined(COMPUTE)

#define GROUP_SIZE 32

layout (local_size_x = GROUP_SIZE, local_size_y = 1, local_size_z = 1) in;
void main() {
    uint sim_steps = uint( 5 );
    float dt = 1.0 / ( 60.0 * float( sim_steps ) );

    uint current_thread = gl_LocalInvocationID.x;

    vec3 fixed_vertex_1 = vec3( 0.0,  1.0, -1.0 );
    vec3 fixed_vertex_2 = vec3( 0.0, -1.0, -1.0 );
    // NOTE(marco): uncomment these to make the cloth behave like a sail
    // vec3 fixed_vertex_3 = vec3( 0.0,  1.0,  1.0 );
    // vec3 fixed_vertex_4 = vec3( 0.0, -1.0,  1.0 );

    vec3 g = vec3( 0.0, -9.8, 0.0 );

    if ( reset_simulation == uint( 1 ) ) {
        for ( uint v = current_thread; v < vertex_count; v += GROUP_SIZE ) {
            physics_vertices[ v ].position = physics_vertices[ v ].start_position;
            physics_vertices[ v ].previous_position = physics_vertices[ v ].start_position;
            physics_vertices[ v ].velocity = vec3( 0, 0, 0 );
            physics_vertices[ v ].force = vec3( 0, 0, 0 );
        }
    }

    barrier();

    for ( uint s = 0; s < sim_steps; ++s ) {
        // First calculate the force to apply to each vertex
        for ( uint v = current_thread; v < vertex_count; v+= GROUP_SIZE ) {
            if ( ( physics_vertices[ v ].start_position == fixed_vertex_1 ) || ( physics_vertices[ v ].start_position == fixed_vertex_2 )
                // NOTE(marco): uncomment these lines to make the cloth behave like a sail
                // || ( physics_vertices[ v ].start_position == fixed_vertex_3 ) || ( physics_vertices[ v ].start_position == fixed_vertex_4 )
                ) {
                continue;
            }

            float m = physics_vertices[ v ].mass;

            vec3 spring_force = vec3( 0, 0, 0 );

            for ( uint j = 0; j < physics_vertices[ v ].joint_count; ++j ) {
                uint other_vertex_index = physics_vertices[ v ].joints[ j ];

                float spring_rest_length =  length( physics_vertices[ v ].start_position - physics_vertices[ other_vertex_index ].start_position );

                vec3 pull_direction = physics_vertices[ v ].position - physics_vertices[ other_vertex_index ].position;
                vec3 relative_pull_direction = pull_direction - ( normalize( pull_direction ) * spring_rest_length );
                pull_direction = relative_pull_direction * spring_stiffness;
                spring_force += pull_direction;
            }

            vec3 viscous_damping = physics_vertices[ v ].velocity * -spring_damping;

            vec3 viscous_velocity = wind_direction - physics_vertices[ v ].velocity;
            viscous_velocity = physics_vertices[ v ].normal * dot( physics_vertices[ v ].normal, viscous_velocity );
            viscous_velocity = viscous_velocity * air_density;

            vec3 force = g * m;
            force -= spring_force;
            force += viscous_damping;
            force += viscous_velocity;

            barrier();

            physics_vertices[ v ].force = force;
        }

        // Then update their position
        for ( uint v = current_thread; v < vertex_count; v+= GROUP_SIZE ) {
            vec3 previous_position = physics_vertices[ v ].previous_position;
            vec3 current_position = physics_vertices[ v ].position;

            barrier();

            // Verlet integration
            vec3 new_position = current_position * 2.0;
            new_position -= previous_position;
            new_position += physics_vertices[ v ].force * ( dt * dt );

            physics_vertices[ v ].position = new_position;
            physics_vertices[ v ].previous_position = current_position;

            physics_vertices[ v ].velocity = new_position - current_position;
        }
    }

    for ( uint i = current_thread * 3; i < index_count; i += GROUP_SIZE * 3 ) {
        uint i0 = indices[ i + 0 ];
        uint i1 = indices[ i + 1 ];
        uint i2 = indices[ i + 2 ];

        vec3 p0 = physics_vertices[ i0 ].position;
        vec3 p1 = physics_vertices[ i1 ].position;
        vec3 p2 = physics_vertices[ i2 ].position;

        // TODO(marco): better normal compuation, also update tangents
        vec3 edge1 = p1 - p0;
        vec3 edge2 = p2 - p0;

        vec3 n = cross( edge1, edge2 );

        physics_vertices[ i0 ].normal = normalize( physics_vertices[ i0 ].normal + n );
        physics_vertices[ i1 ].normal = normalize( physics_vertices[ i1 ].normal + n );
        physics_vertices[ i2 ].normal = normalize( physics_vertices[ i2 ].normal + n );
    }

    for ( uint v = current_thread; v < vertex_count; v+= GROUP_SIZE ) {
        positions[ v * 3 + 0 ] = physics_vertices[ v ].position.x;
        positions[ v * 3 + 1 ] = physics_vertices[ v ].position.y;
        positions[ v * 3 + 2 ] = physics_vertices[ v ].position.z;

        normals[ v * 3 + 0 ] = physics_vertices[ v ].normal.x;
        normals[ v * 3 + 1 ] = physics_vertices[ v ].normal.y;
        normals[ v * 3 + 2 ] = physics_vertices[ v ].normal.z;
    }
}

#endif // COMPUTE
