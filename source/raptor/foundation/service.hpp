#pragma once

#include "foundation/platform.hpp"

namespace raptor {

    struct Service {

        virtual void                        init( void* configuration ) { }
        virtual void                        shutdown() { }

    }; // struct Service

    #define RAPTOR_DECLARE_SERVICE(Type)        static Type* instance();

} // namespace raptor
