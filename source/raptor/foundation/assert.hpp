#pragma once

#include "foundation/log.hpp"

namespace raptor {

    #define RASSERT( condition )      if (!(condition)) { rprint(RAPTOR_FILELINE("FALSE\n")); RAPTOR_DEBUG_BREAK }
#if defined(_MSC_VER)
    #define RASSERTM( condition, message, ... ) if (!(condition)) { rprint(RAPTOR_FILELINE(RAPTOR_CONCAT(message, "\n")), __VA_ARGS__); RAPTOR_DEBUG_BREAK }
#else
    #define RASSERTM( condition, message, ... ) if (!(condition)) { rprint(RAPTOR_FILELINE(RAPTOR_CONCAT(message, "\n")), ## __VA_ARGS__); RAPTOR_DEBUG_BREAK }
#endif

} // namespace raptor
