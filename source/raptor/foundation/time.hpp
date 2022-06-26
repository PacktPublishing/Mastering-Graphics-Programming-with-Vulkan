#pragma once

#include "foundation/platform.hpp"

namespace raptor {

    void                            time_service_init();                // Needs to be called once at startup.
    void                            time_service_shutdown();           // Needs to be called at shutdown.

    i64                             time_now();                         // Get current time ticks.

    double                          time_microseconds( i64 time );  // Get microseconds from time ticks
    double                          time_milliseconds( i64 time );  // Get milliseconds from time ticks
    double                          time_seconds( i64 time );       // Get seconds from time ticks

    i64                             time_from( i64 starting_time ); // Get time difference from start to current time.
    double                          time_from_microseconds( i64 starting_time ); // Convenience method.
    double                          time_from_milliseconds( i64 starting_time ); // Convenience method.
    double                          time_from_seconds( i64 starting_time );      // Convenience method.

    double                          time_delta_seconds( i64 starting_time, i64 ending_time );
    double                          time_delta_milliseconds( i64 starting_time, i64 ending_time );

} // namespace raptor