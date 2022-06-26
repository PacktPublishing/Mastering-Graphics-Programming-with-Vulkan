#include "log.hpp"

#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <stdio.h>
#include <stdarg.h>

namespace raptor {

LogService              s_log_service;

static constexpr u32    k_string_buffer_size = 1024 * 1024;
static char             log_buffer[ k_string_buffer_size ];

static void output_console( char* log_buffer_ ) {
    printf( "%s", log_buffer_ );
}

#if defined(_MSC_VER)
static void output_visual_studio( char* log_buffer_ ) {
    OutputDebugStringA( log_buffer_ );
}
#endif

LogService* LogService::instance() {
    return &s_log_service;
}

void LogService::print_format( cstring format, ... ) {
    va_list args;

    va_start( args, format );
#if defined(_MSC_VER)
    vsnprintf_s( log_buffer, ArraySize( log_buffer ), format, args );
#else
    vsnprintf( log_buffer, ArraySize( log_buffer), format, args );
#endif
    log_buffer[ ArraySize( log_buffer ) - 1 ] = '\0';
    va_end( args );

    output_console( log_buffer );
#if defined(_MSC_VER)
    output_visual_studio( log_buffer );
#endif // _MSC_VER

    if ( print_callback )
      print_callback( log_buffer );
}

void LogService::set_callback( PrintCallback callback ) {
    print_callback = callback;
}

} // namespace raptor