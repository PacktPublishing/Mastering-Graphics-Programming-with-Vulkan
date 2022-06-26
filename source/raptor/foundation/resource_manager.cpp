#include "resource_manager.hpp"

namespace raptor {

void ResourceManager::init( Allocator* allocator_, ResourceFilenameResolver* resolver ) {

    this->allocator = allocator_;
    this->filename_resolver = resolver;

    loaders.init( allocator, 8 );
    compilers.init( allocator, 8 );
}

void ResourceManager::shutdown() {

    loaders.shutdown();
    compilers.shutdown();
}

void ResourceManager::set_loader( cstring resource_type, ResourceLoader* loader ) {
    const u64 hashed_name = hash_calculate( resource_type );
    loaders.insert( hashed_name, loader );
}

void ResourceManager::set_compiler( cstring resource_type, ResourceCompiler* compiler ) {
    const u64 hashed_name = hash_calculate( resource_type );
    compilers.insert( hashed_name, compiler );
}

} // namespace raptor