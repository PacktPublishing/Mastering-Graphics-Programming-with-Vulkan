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

layout ( set = MATERIAL_SET, binding = 1 ) readonly buffer SphereTransforms {
    mat4 transforms[];
};

#if defined(VERTEX_DEBUG_MESH)

layout(location=0) in vec3 position;

layout(location=0) flat out uint draw_id;

void main() {
    draw_id = gl_DrawIDARB;
    gl_Position = view_projection * vec4( transforms[gl_DrawIDARB] * vec4( position, 1.0 ) );
}

#endif // VERTEX


#if defined (FRAGMENT_DEBUG_MESH)

layout (location = 0) flat in uint draw_id;

layout (location = 0) out vec4 colour;

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

void main() {

    uint mhash = hash(draw_id);
    colour = vec4( vec3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0, 0.6 );
}

#endif // FRAGMENT
