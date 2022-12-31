#include "bit.hpp"
#include "log.hpp"
#include "memory.hpp"

#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin0.h>
#endif
#include <string.h>

namespace raptor {


u32 trailing_zeros_u32( u32 x ) {
    /*unsigned long result = 0;  // NOLINT(runtime/int)
    _BitScanForward( &result, x );
    return result;*/
#if defined(_MSC_VER)
    return _tzcnt_u32( x );
#else
    return __builtin_ctz( x );
#endif
}

u32 leading_zeroes_u32( u32 x ) {
    /*unsigned long result = 0;  // NOLINT(runtime/int)
    _BitScanReverse( &result, x );
    return result;*/
#if defined(_MSC_VER)
    return __lzcnt( x );
#else
    return __builtin_clz( x );
#endif
}

#if defined(_MSC_VER)
u32 leading_zeroes_u32_msvc( u32 x ) {
    unsigned long result = 0;  // NOLINT(runtime/int)
    if ( _BitScanReverse( &result, x ) ) {
        return 31 - result;
    }
    return 32;
}
#endif

u64 trailing_zeros_u64( u64 x ) {
#if defined(_MSC_VER)
    return _tzcnt_u64( x );
#else
    return __builtin_ctzl( x );
#endif
}

u32 round_up_to_power_of_2( u32 v ) {

    u32 nv = 1 << ( 32 - raptor::leading_zeroes_u32( v ) );
#if 0
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
#endif
    return nv;
}
void print_binary( u64 n ) {

    rprint( "0b" );
    for ( u32 i = 0; i < 64; ++i ) {
        u64 bit = (n >> ( 64 - i - 1 )) & 0x1;
        rprint( "%llu", bit );
    }
    rprint( " " );
}

void print_binary( u32 n ) {

    rprint( "0b" );
    for ( u32 i = 0; i < 32; ++i ) {
        u32 bit = ( n >> ( 32 - i - 1 ) ) & 0x1;
        rprint( "%u", bit );
    }
    rprint( " " );
}

// BitSet /////////////////////////////////////////////////////////////////
void BitSet::init( Allocator* allocator_, u32 total_bits ) {
    allocator = allocator_;
    bits = nullptr;
    size = 0;

    resize( total_bits );
}

void BitSet::shutdown() {
    rfree( bits, allocator );
}

void BitSet::resize( u32 total_bits ) {
    u8* old_bits = bits;

    const u32 new_size = ( total_bits + 7 ) / 8;
    if ( size == new_size ) {
        return;
    }

    bits = ( u8* )rallocam( new_size, allocator );

    if ( old_bits ) {
        memcpy( bits, old_bits, size );
        rfree( old_bits, allocator );
    }
    else {
        memset( bits, 0, new_size );
    }

    size = new_size;
}


} // namespace raptor
