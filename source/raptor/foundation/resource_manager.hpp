#pragma once

#include "foundation/platform.hpp"
#include "foundation/assert.hpp"
#include "foundation/hash_map.hpp"

namespace raptor {

struct ResourceManager;

//
// Reference counting and named resource.
struct Resource {

    void            add_reference()     { ++references; }
    void            remove_reference()  { RASSERT( references != 0 ); --references; }

    u64             references  = 0;
    cstring         name        = nullptr;

}; // struct Resource

//
//
struct ResourceCompiler {

}; // struct ResourceCompiler

//
//
struct ResourceLoader {

    virtual Resource*   get( cstring name ) = 0;
    virtual Resource*   get( u64 hashed_name ) = 0;

    virtual Resource*   unload( cstring name ) = 0;

    virtual Resource*   create_from_file( cstring name, cstring filename, raptor::ResourceManager* resource_manager ) { return nullptr; }

}; // struct ResourceLoader

//
//
struct ResourceFilenameResolver {

    virtual cstring get_binary_path_from_name( cstring name ) = 0;

}; // struct ResourceFilenameResolver

//
//
struct ResourceManager {

    void            init( Allocator* allocator, ResourceFilenameResolver* resolver );
    void            shutdown();

    template <typename T>
    T*              load( cstring name );

    template <typename T>
    T*              get( cstring name );

    template <typename T>
    T*              get( u64 hashed_name );

    template <typename T>
    T*              reload( cstring name );

    void            set_loader( cstring resource_type, ResourceLoader* loader );
    void            set_compiler( cstring resource_type, ResourceCompiler* compiler );

    FlatHashMap<u64, ResourceLoader*>       loaders;
    FlatHashMap<u64, ResourceCompiler*>     compilers;

    Allocator*      allocator;
    ResourceFilenameResolver* filename_resolver;

}; // struct ResourceManager

template<typename T>
inline T* ResourceManager::load( cstring name ) {
    ResourceLoader* loader = loaders.get( T::k_type_hash );
    if ( loader ) {
        // Search if the resource is already in cache
        T* resource = ( T* )loader->get( name );
        if ( resource )
            return resource;

        // Resource not in cache, create from file
        cstring path = filename_resolver->get_binary_path_from_name( name );
        return (T*)loader->create_from_file( name, path, this );
    }
    return nullptr;
}

template<typename T>
inline T* ResourceManager::get( cstring name ) {
    ResourceLoader* loader = loaders.get( T::k_type_hash );
    if ( loader ) {
        return ( T* )loader->get( name );
    }
    return nullptr;
}

template<typename T>
inline T* ResourceManager::get( u64 hashed_name ) {
    ResourceLoader* loader = loaders.get( T::k_type_hash );
    if ( loader ) {
        return ( T* )loader->get( hashed_name );
    }
    return nullptr;
}

template<typename T>
inline T* ResourceManager::reload(cstring name) {
    ResourceLoader* loader = loaders.get( T::k_type_hash );
    if ( loader ) {
        T* resource = ( T* )loader->get( name );
        if ( resource ) {
            loader->unload( name );

            // Resource not in cache, create from file
            cstring path = filename_resolver->get_binary_path_from_name( name );
            return ( T* )loader->create_from_file( name, path, this );
        }
    }
    return nullptr;
}

} // namespace raptor