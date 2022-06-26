#pragma once

#include "foundation/memory.hpp"
#include "foundation/assert.hpp"

namespace raptor {

    //
    //
    struct ResourcePool {

        void                            init( Allocator* allocator, u32 pool_size, u32 resource_size );
        void                            shutdown();

        u32                             obtain_resource();      // Returns an index to the resource
        void                            release_resource( u32 index );
        void                            free_all_resources();

        void*                           access_resource( u32 index );
        const void*                     access_resource( u32 index ) const;

        u8*                             memory          = nullptr;
        u32*                            free_indices    = nullptr;
        Allocator*                      allocator       = nullptr;

        u32                             free_indices_head   = 0;
        u32                             pool_size           = 16;
        u32                             resource_size       = 4;
        u32                             used_indices        = 0;

    }; // struct ResourcePool

    //
    //
    template <typename T>
    struct ResourcePoolTyped : public ResourcePool {

        void                            init( Allocator* allocator, u32 pool_size );
        void                            shutdown();

        T*                              obtain();
        void                            release( T* resource );

        T*                              get( u32 index );
        const T*                        get( u32 index ) const;

    }; // struct ResourcePoolTyped

    template<typename T>
    inline void ResourcePoolTyped<T>::init( Allocator* allocator_, u32 pool_size_ ) {
        ResourcePool::init( allocator_, pool_size_, sizeof( T ) );
    }

    template<typename T>
    inline void ResourcePoolTyped<T>::shutdown() {
        if ( free_indices_head != 0 ) {
            rprint( "Resource pool has unfreed resources.\n" );

            for ( u32 i = 0; i < free_indices_head; ++i ) {
                rprint( "\tResource %u, %s\n", free_indices[ i ], get( free_indices[ i ] )->name );
            }
        }
        ResourcePool::shutdown();
    }

    template<typename T>
    inline T* ResourcePoolTyped<T>::obtain() {
        u32 resource_index = ResourcePool::obtain_resource();
        if ( resource_index != u32_max ) {
            T* resource = get( resource_index );
            resource->pool_index = resource_index;
            return resource;
        }
        
        return nullptr;
    }

    template<typename T>
    inline void ResourcePoolTyped<T>::release( T* resource ) {
        ResourcePool::release_resource( resource->pool_index );
    }

    template<typename T>
    inline T* ResourcePoolTyped<T>::get( u32 index ) {
        return ( T* )ResourcePool::access_resource( index );
    }

    template<typename T>
    inline const T* ResourcePoolTyped<T>::get( u32 index ) const {
        return ( const T* )ResourcePool::access_resource( index );
    }

} // namespace raptor