#pragma once

#include "foundation/platform.hpp"

namespace raptor {

    //
    // Color class that embeds color in a uint32.
    //
    struct Color {

        void                        set( float r, float g, float b, float a ) { abgr = uint8_t( r * 255.f ) | ( uint8_t( g * 255.f ) << 8 ) | ( uint8_t( b * 255.f ) << 16 ) | ( uint8_t( a * 255.f ) << 24 ); }

        f32                         r() const                               { return ( abgr & 0xff ) / 255.f; }
        f32                         g() const                               { return ( ( abgr >> 8 ) & 0xff ) / 255.f; }
        f32                         b() const                               { return ( ( abgr >> 16 ) & 0xff ) / 255.f; }
        f32                         a() const                               { return ( ( abgr >> 24 ) & 0xff ) / 255.f; }

        Color                       operator=( const u32 color )            { abgr = color; return *this; }

        static u32                  from_u8( u8 r, u8 g, u8 b, u8 a )       { return ( r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ) ); }

        static u32                  get_distinct_color( u32 index );

        static const u32            red         = 0xff0000ff;
        static const u32            green       = 0xff00ff00;
        static const u32            blue        = 0xffff0000;
        static const u32            yellow      = 0xff00ffff;
        static const u32            black       = 0xff000000;
        static const u32            white       = 0xffffffff;
        static const u32            transparent = 0x00000000;

        u32                         abgr;

    }; // struct Color

} // namespace raptor