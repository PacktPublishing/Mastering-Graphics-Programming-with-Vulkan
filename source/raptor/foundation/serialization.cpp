#include "serialization.hpp"

namespace raptor {

/*

void Serializer::start_writing( FILE* file, u32 data_version_ ) {
    file_handle = file;
    data_version = data_version_;
    is_writing = true;

    Serialize( this, &data_version );
}

void Serializer::start_reading( FILE* file ) {
    file_handle = file;
    is_writing = false;

    Serialize( this, &data_version );
}

void Serialize( Serializer* s, i8* data ) {
    if ( s->is_writing )     {
        fwrite( data, sizeof( i8 ), 1, s->file_handle );
    }     else     {
        fread( data, sizeof( i8 ), 1, s->file_handle );
    }
}

void Serialize( Serializer* s, u8* data ) {
    if ( s->is_writing )     {
        fwrite( data, sizeof( u8 ), 1, s->file_handle );
    }     else     {
        fread( data, sizeof( u8 ), 1, s->file_handle );
    }
}

void Serialize( Serializer* s, i16* data ) {
    if ( s->is_writing )     {
        fwrite( data, sizeof( i16 ), 1, s->file_handle );
    }     else     {
        fread( data, sizeof( i16 ), 1, s->file_handle );
    }
}

void Serialize( Serializer* s, u16* data ) {
    if ( s->is_writing )     {
        fwrite( data, sizeof( u16 ), 1, s->file_handle );
    }     else     {
        fread( data, sizeof( u16 ), 1, s->file_handle );
    }
}

void Serialize( Serializer* s, i32* data ) {
    if ( s->is_writing )     {
        fwrite( data, sizeof( i32 ), 1, s->file_handle );
    }     else     {
        fread( data, sizeof( i32 ), 1, s->file_handle );
    }
}

void Serialize( Serializer* s, u32* data ) {
    if ( s->is_writing )     {
        fwrite( data, sizeof( u32 ), 1, s->file_handle );
    }     else     {
        fread( data, sizeof( u32 ), 1, s->file_handle );
    }
}

void Serialize( Serializer* s, i64* data ) {
    if ( s->is_writing )     {
        fwrite( data, sizeof( i64 ), 1, s->file_handle );
    }     else     {
        fread( data, sizeof( i64 ), 1, s->file_handle );
    }
}

void Serialize( Serializer* s, u64* data ) {
    if ( s->is_writing )     {
        fwrite( data, sizeof( u64 ), 1, s->file_handle );
    }     else     {
        fread( data, sizeof( u64 ), 1, s->file_handle );
    }
}

void Serialize( Serializer* s, f32* data ) {
    if ( s->is_writing )     {
        fwrite( data, sizeof( f32 ), 1, s->file_handle );
    }     else     {
        fread( data, sizeof( f32 ), 1, s->file_handle );
    }
}


void Serialize( Serializer* s, f64* data ) {
    if ( s->is_writing ) {
        fwrite( data, sizeof( f64 ), 1, s->file_handle );
    } else {
        fread( data, sizeof( f64 ), 1, s->file_handle );
    }
}


void Serialize( Serializer* s, bool* data ) {
    if ( s->is_writing ) {
        fwrite( data, sizeof( bool ), 1, s->file_handle );
    } else {
        fread( data, sizeof( bool ), 1, s->file_handle );
    }
}

*/

} // namespace raptor