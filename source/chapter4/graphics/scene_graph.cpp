#include "graphics/scene_graph.hpp"

#include "foundation/numerics.hpp"
#include "foundation/time.hpp"

#include <string.h>

namespace raptor {


void SceneGraph::init( Allocator* resident_allocator, u32 num_nodes ) {
    nodes_hierarchy.init( resident_allocator, num_nodes, num_nodes );
    local_matrices.init( resident_allocator, num_nodes, num_nodes );
    world_matrices.init( resident_allocator, num_nodes, num_nodes );

    updated_nodes.init( resident_allocator, num_nodes );
}

void SceneGraph::shutdown() {
    nodes_hierarchy.shutdown();
    updated_nodes.shutdown();
    local_matrices.shutdown();
    world_matrices.shutdown();
}

void SceneGraph::resize( u32 num_nodes ) {
    nodes_hierarchy.set_size( num_nodes );
    local_matrices.set_size( num_nodes );
    world_matrices.set_size( num_nodes );

    updated_nodes.resize( num_nodes );

    memset( nodes_hierarchy.data, 0, num_nodes * 4 );

    for ( u32 i = 0; i < num_nodes; ++i ) {
        nodes_hierarchy[ i ].parent = -1;
    }
}

void SceneGraph::update_matrices() {

    // TODO: per level update
    u32 max_level = 0;
    for ( u32 i = 0; i < nodes_hierarchy.size; ++i ) {
        max_level = raptor::max(max_level, (u32)nodes_hierarchy[ i ].level );
    }
    u32 current_level = 0;
    u32 nodes_visited = 0;

    //i64 time = time_now();
    while ( current_level <= max_level ) {

        for ( u32 i = 0; i < nodes_hierarchy.size; ++i ) {

            if ( nodes_hierarchy[ i ].level != current_level ) {
                continue;
            }

            if ( updated_nodes.get_bit( i ) == 0 ) {
                continue;
            }

            updated_nodes.clear_bit( i );

            if ( nodes_hierarchy[ i ].parent == -1 ) {
                world_matrices[ i ] = local_matrices[ i ];
            } else {
                const mat4s& parent_matrix = world_matrices[ nodes_hierarchy[ i ].parent ];
                world_matrices[ i ] = glms_mat4_mul( parent_matrix, local_matrices[ i ] );
            }

            ++nodes_visited;
        }

        ++current_level;
    }

    //rprint( "Updated scene graph in %fms\n", time_from_milliseconds( time ) );

    /*for ( u32 i = 0; i < nodes_hierarchy.size; ++i ) {
        if ( updated_nodes.get_bit( i ) == 0 ) {
            continue;
        }

        updated_nodes.clear_bit( i );

        if ( nodes_hierarchy[ i ].parent == -1 ) {
            world_matrices[ i ] = local_matrices[ i ];
        }
        else {
            const mat4s& parent_matrix = world_matrices[ nodes_hierarchy[ i ].parent ];
            world_matrices[ i ] = glms_mat4_mul( parent_matrix, local_matrices[ i ] );
        }
    }*/
}

void SceneGraph::set_hierarchy( u32 node_index, u32 parent_index, u32 level ) {
    // Mark node as updated
    updated_nodes.set_bit( node_index );
    nodes_hierarchy[ node_index ].parent = parent_index;
    nodes_hierarchy[ node_index ].level = level;

    sort_update_order = true;
}

void SceneGraph::set_local_matrix( u32 node_index, const mat4s& local_matrix ) {
    // Mark node as updated
    updated_nodes.set_bit( node_index );
    local_matrices[ node_index ] = local_matrix;
}


} // namespace raptor