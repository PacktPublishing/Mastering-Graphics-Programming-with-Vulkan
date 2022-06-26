#include "time.hpp"

#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <time.h>
#endif

namespace raptor {

#if defined(_MSC_VER)
// Cached frequency.
// From Microsoft Docs: (https://docs.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency)
// "The frequency of the performance counter is fixed at system boot and is consistent across all processors. 
// Therefore, the frequency need only be queried upon application initialization, and the result can be cached."
static LARGE_INTEGER s_frequency;
#endif

//
//
void time_service_init() {
#if defined(_MSC_VER)
    // Cache this value - by Microsoft Docs it will not change during process lifetime.
    QueryPerformanceFrequency(&s_frequency);
#endif
}

//
//
void time_service_shutdown() {
    // Nothing to do.
}

// Taken from the Rust code base: https://github.com/rust-lang/rust/blob/3809bbf47c8557bd149b3e52ceb47434ca8378d5/src/libstd/sys_common/mod.rs#L124
// Computes (value*numer)/denom without overflow, as long as both
// (numer*denom) and the overall result fit into i64 (which is the case
// for our time conversions).
static i64 int64_mul_div( i64 value, i64 numer, i64 denom ) {
    const i64 q = value / denom;
    const i64 r = value % denom;
    // Decompose value as (value/denom*denom + value%denom),
    // substitute into (value*numer)/denom and simplify.
    // r < denom, so (denom*numer) is the upper bound of (r*numer)
    return q * numer + r * numer / denom;
}

//
//
i64 time_now() {
#if defined(_MSC_VER)
    // Get current time
    LARGE_INTEGER time;
    QueryPerformanceCounter( &time );

    // Convert to microseconds
    // const i64 microseconds_per_second = 1000000LL;
    const i64 microseconds = int64_mul_div( time.QuadPart, 1000000LL, s_frequency.QuadPart );
#else
    timespec tp;
    clock_gettime( CLOCK_MONOTONIC, &tp );

    const u64 now = tp.tv_sec * 1000000000 + tp.tv_nsec;
    const i64 microseconds = now / 1000;
#endif

    return microseconds;
}

//
//
i64 time_from( i64 starting_time ) {
    return time_now() - starting_time;
}

//
//
double time_from_microseconds( i64 starting_time ) {
    return time_microseconds( time_from( starting_time ) );
}

//
//
double time_from_milliseconds( i64 starting_time ) {
    return time_milliseconds( time_from( starting_time ) );
}

//
//
double time_from_seconds( i64 starting_time ) {
    return time_seconds( time_from( starting_time ) );
}

double time_delta_seconds( i64 starting_time, i64 ending_time ) {
    return time_seconds( ending_time - starting_time );
}

double time_delta_milliseconds( i64 starting_time, i64 ending_time ) {
    return time_milliseconds( ending_time - starting_time );
}

//
//
double time_microseconds( i64 time ) {
    return (double)time;
}

//
//
double time_milliseconds( i64 time ) {
    return (double)time / 1000.0;
}

//
//
double time_seconds( i64 time ) {
    return (double)time / 1000000.0;
}

} // namespace raptor