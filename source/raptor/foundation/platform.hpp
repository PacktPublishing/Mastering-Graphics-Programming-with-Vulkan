#pragma once

#include <stdint.h>

#if !defined(_MSC_VER)
#include <signal.h>
#endif

// Macros ////////////////////////////////////////////////////////////////

#define ArraySize(array)        ( sizeof(array)/sizeof((array)[0]) )


#if defined (_MSC_VER)
#define RAPTOR_INLINE                               inline
#define RAPTOR_FINLINE                              __forceinline
#define RAPTOR_DEBUG_BREAK                          __debugbreak();
#define RAPTOR_DISABLE_WARNING(warning_number)      __pragma( warning( disable : warning_number ) )
#define RAPTOR_CONCAT_OPERATOR(x, y)                x##y
#else
#define RAPTOR_INLINE                               inline
#define RAPTOR_FINLINE                              always_inline
#define RAPTOR_DEBUG_BREAK                          raise(SIGTRAP);
#define RAPTOR_CONCAT_OPERATOR(x, y)                x y
#endif // MSVC

#define RAPTOR_STRINGIZE( L )                       #L 
#define RAPTOR_MAKESTRING( L )                      RAPTOR_STRINGIZE( L )
#define RAPTOR_CONCAT(x, y)                         RAPTOR_CONCAT_OPERATOR(x, y)
#define RAPTOR_LINE_STRING                          RAPTOR_MAKESTRING( __LINE__ ) 
#define RAPTOR_FILELINE(MESSAGE)                    __FILE__ "(" RAPTOR_LINE_STRING ") : " MESSAGE

// Unique names
#define RAPTOR_UNIQUE_SUFFIX(PARAM)                 RAPTOR_CONCAT(PARAM, __LINE__ )


// Native types typedefs /////////////////////////////////////////////////
typedef uint8_t                 u8;
typedef uint16_t                u16;
typedef uint32_t                u32;
typedef uint64_t                u64;

typedef int8_t                  i8;
typedef int16_t                 i16;
typedef int32_t                 i32;
typedef int64_t                 i64;

typedef float                   f32;
typedef double                  f64;

typedef size_t                  sizet;

typedef const char*             cstring;

static const u64                u64_max = UINT64_MAX;
static const i64                i64_max = INT64_MAX;
static const u32                u32_max = UINT32_MAX;
static const i32                i32_max = INT32_MAX;
static const u16                u16_max = UINT16_MAX;
static const i16                i16_max = INT16_MAX;
static const u8                  u8_max = UINT8_MAX;
static const i8                  i8_max = INT8_MAX;