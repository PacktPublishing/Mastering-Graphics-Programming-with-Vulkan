#include "service_manager.hpp"
#include "assert.hpp"

namespace raptor {

static ServiceManager           s_service_manager;
ServiceManager*                 ServiceManager::instance    = &s_service_manager;

void ServiceManager::init( Allocator* allocator_ ) {

    rprint( "ServiceManager init\n" );
    allocator = allocator_;
    
    services.init( allocator, 8 );
}

void ServiceManager::shutdown() {

    services.shutdown();

    rprint( "ServiceManager shutdown\n" );
}

void ServiceManager::add_service( Service* service, cstring name ) {
    u64 hash_name = hash_calculate( name );
    FlatHashMapIterator it = services.find( hash_name );
    RASSERTM( it.is_invalid(), "Overwriting service %s, is this intended ?", name );
    services.insert( hash_name, service );
}

void ServiceManager::remove_service( cstring name ) {
    u64 hash_name = hash_calculate( name );
    services.remove( hash_name );
}

Service* ServiceManager::get_service( cstring name ) {
    u64 hash_name = hash_calculate( name );
    return services.get( hash_name );
}

} // namespace raptor