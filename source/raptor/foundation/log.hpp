#pragma once

#include "foundation/platform.hpp"
#include "foundation/service.hpp"

namespace raptor {

    typedef void                        ( *PrintCallback )( const char* );  // Additional callback for printing

    struct LogService : public Service {

        RAPTOR_DECLARE_SERVICE( LogService );

        void                            print_format( cstring format, ... );

        void                            set_callback( PrintCallback callback );

        PrintCallback                   print_callback = nullptr;

        static constexpr cstring        k_name = "raptor_log_service";
    };

#if defined(_MSC_VER)
    #define rprint(format, ...)          raptor::LogService::instance()->print_format(format, __VA_ARGS__);
    #define rprintret(format, ...)       raptor::LogService::instance()->print_format(format, __VA_ARGS__); raptor::LogService::instance()->print_format("\n");
#else
    #define rprint(format, ...)          raptor::LogService::instance()->print_format(format, ## __VA_ARGS__);
    #define rprintret(format, ...)       raptor::LogService::instance()->print_format(format, ## __VA_ARGS__); raptor::LogService::instance()->print_format("\n");
#endif

} // namespace raptor
