#pragma once

#include "foundation/array.hpp"
#include "foundation/hash_map.hpp"

namespace raptor {

    struct Service;

    struct ServiceManager {

        void                    init( Allocator* allocator );
        void                    shutdown();

        void                    add_service( Service* service, cstring name );
        void                    remove_service( cstring name );

        Service*                get_service( cstring name );

        template<typename T>
        T*                      get();

        static ServiceManager*  instance;

        FlatHashMap<u64, Service*> services;
        Allocator*              allocator   = nullptr;

    }; // struct ServiceManager

    template<typename T>
    inline T* ServiceManager::get() {
        T* service = ( T* )get_service( T::k_name );
        if ( !service ) {
            add_service( T::instance(), T::k_name );
        }

        return T::instance();
    }

} // namespace raptor
