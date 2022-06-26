#define RAPTOR_BLOB_WRITE
#include <string.h>
#include "blob_serialization.hpp"

#include <stdio.h>
#include <stdarg.h>

namespace raptor {

void BlobSerializer::write_common( Allocator* allocator_, u32 serializer_version_, sizet size ) {
    allocator = allocator_;
    // Allocate memory
    blob_memory = ( char* )ralloca( size + sizeof( BlobHeader ), allocator_ );
    RASSERT( blob_memory );

    has_allocated_memory = 1;

    total_size = ( u32 )size + sizeof( BlobHeader );
    serialized_offset = allocated_offset = 0;

    serializer_version = serializer_version_;
    // This will be written into the blob
    data_version = serializer_version_;
    is_reading = 0;
    is_mappable = 0;

    // Write header
    BlobHeader* header = ( BlobHeader* )allocate_static( sizeof( BlobHeader ) );
    header->version = serializer_version;
    header->mappable = is_mappable;

    serialized_offset = allocated_offset;
}

void BlobSerializer::shutdown() {

    if ( is_reading ) {
        // When reading and serializing, we can free blob memory after read.
        // Otherwise we will free the pointer when done.
        if ( blob_memory && has_allocated_memory )
            rfree( blob_memory, allocator );
    }
    else {
        if ( blob_memory )
            rfree( blob_memory, allocator );
    }

    /*if ( blob_memory )
        rfree( blob_memory, allocator );*/
    

    serialized_offset = allocated_offset = 0;
}

void BlobSerializer::serialize( char* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( char ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( char ) );
    }

    serialized_offset += sizeof( char );
}

void BlobSerializer::serialize( i8* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( i8 ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( i8 ) );
    }

    serialized_offset += sizeof( i8 );
}

void BlobSerializer::serialize( u8* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( u8 ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( u8 ) );
    }

    serialized_offset += sizeof( u8 );
}

void BlobSerializer::serialize( i16* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( i16 ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( i16 ) );
    }

    serialized_offset += sizeof( i16 );
}

void BlobSerializer::serialize( u16* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( u16 ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( u16 ) );
    }

    serialized_offset += sizeof( u16 );
}

void BlobSerializer::serialize( i32* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( i32 ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( i32 ) );
    }

    serialized_offset += sizeof( i32 );
}

void BlobSerializer::serialize( u32* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( u32 ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( u32 ) );
    }

    serialized_offset += sizeof( u32 );
}

void BlobSerializer::serialize( i64* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( i64 ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( i64 ) );
    }

    serialized_offset += sizeof( i64 );
}

void BlobSerializer::serialize( u64* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( u64 ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( u64 ) );
    }

    serialized_offset += sizeof( u64 );
}

void BlobSerializer::serialize( f32* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( f32 ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( f32 ) );
    }

    serialized_offset += sizeof( f32 );
}

void BlobSerializer::serialize( f64* data ) {

    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( f64 ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( f64 ) );
    }

    serialized_offset += sizeof( f64 );
}

void BlobSerializer::serialize( bool* data ) {
    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], sizeof( bool ) );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, sizeof( bool ) );
    }

    serialized_offset += sizeof( bool );
}

void BlobSerializer::serialize_memory( void* data, sizet size ) {

    if ( is_reading ) {
        memcpy( data, &blob_memory[ serialized_offset ], size );
    } else {
        memcpy( &blob_memory[ serialized_offset ], data, size );
    }

    serialized_offset += (u32)size;
}

void BlobSerializer::serialize_memory_block( void** data, u32* size ) {

    serialize( size ); 

    if ( is_reading ) {
        // Blob --> Data
        i32 source_data_offset;
        serialize( &source_data_offset );

        if ( source_data_offset > 0 ) {
            // Cache serialized
            u32 cached_serialized = serialized_offset;

            serialized_offset = allocated_offset;

            *data = data_memory + allocated_offset;

            // Reserve memory
            allocate_static( *size );

            char* source_data = blob_memory + cached_serialized + source_data_offset - 4;
            memcpy( *data, source_data, *size );
            // Restore serialized
            serialized_offset = cached_serialized;
        } else {
            *data = nullptr;
            size = 0;
        }
    } else {
        // Data --> Blob
        // Data will be copied at the end of the current blob
        i32 data_offset = allocated_offset - serialized_offset;
        serialize( &data_offset );

        u32 cached_serialized = serialized_offset;
        // Move serialization to at the end of the blob.
        serialized_offset = allocated_offset;
        // Allocate memory in the blob
        allocate_static( *size );

        char* destination_data = blob_memory + serialized_offset;
        memcpy( destination_data, *data, *size );

        // Restore serialized
        serialized_offset = cached_serialized;
    }
}

void BlobSerializer::serialize( cstring data ) {
   // sizet len = strlen( data );
    RASSERTM( false, "To be implemented!" );
}

char* BlobSerializer::allocate_static( sizet size ) {
    if ( allocated_offset + size > total_size ) 
    {
        rprint( "Blob allocation error: allocated, requested, total - %u + %u > %u\n", allocated_offset, size, total_size );
        return nullptr;
    }

    u32 offset = allocated_offset;
    allocated_offset += ( u32 )size;

    return is_reading ? data_memory + offset : blob_memory + offset;
}

void BlobSerializer::serialize( RelativeString* data ) {

    if ( is_reading ) {
        // Blob --> Data
        serialize( &data->size );

        i32 source_data_offset;
        serialize( &source_data_offset );

        if ( source_data_offset > 0 ) {
            // Cache serialized
            u32 cached_serialized = serialized_offset;

            serialized_offset = allocated_offset;

            data->data.offset = get_relative_data_offset( data ) - 4;

            // Reserve memory + string ending
            allocate_static( ( sizet )data->size + 1 );

            char* source_data = blob_memory + cached_serialized + source_data_offset - 4;
            memcpy( ( char* )data->c_str(), source_data, ( sizet )data->size + 1 );
            rprint( "Found %s\n", data->c_str() );
            // Restore serialized
            serialized_offset = cached_serialized;
        } else {
            data->set_empty();
        }
    } else {
        // Data --> Blob
        serialize( &data->size );
        // Data will be copied at the end of the current blob
        i32 data_offset = allocated_offset - serialized_offset;
        serialize( &data_offset );

        u32 cached_serialized = serialized_offset;
        // Move serialization to at the end of the blob.
        serialized_offset = allocated_offset;
        // Allocate memory in the blob
        allocate_static( ( sizet )data->size + 1 );

        char* destination_data = blob_memory + serialized_offset;
        memcpy( destination_data, ( char* )data->c_str(), ( sizet )data->size + 1 );
        rprint( "Written %s, Found %s\n", data->c_str(), destination_data );

        // Restore serialized
        serialized_offset = cached_serialized;
    }
}

void BlobSerializer::allocate_and_set( RelativeString& string, cstring format, ... ) {

    u32 cached_offset = allocated_offset;

    char* destination_memory = is_reading ? data_memory : blob_memory;

    va_list args;
    va_start( args, format );
#if (_MSC_VER)
    int written_chars = vsnprintf_s( &destination_memory[ allocated_offset ], total_size - allocated_offset, _TRUNCATE, format, args );
#else
    int written_chars = vsnprintf( &destination_memory[ allocated_offset ], total_size - allocated_offset, format, args );
#endif
    allocated_offset += written_chars > 0 ? written_chars : 0;
    va_end( args );

    if ( written_chars < 0 ) {
        rprint( "New string too big for current buffer! Please allocate more size.\n" );
    }

    // Add null termination for string.
    // By allocating one extra character for the null termination this is always safe to do.
    destination_memory[ allocated_offset ] = 0;
    ++allocated_offset;

    string.set( destination_memory + cached_offset, written_chars );
}

void BlobSerializer::allocate_and_set( RelativeString& string, char* text, u32 length ) {
    
    if ( allocated_offset + length > total_size ) {
        rprint( "New string too big for current buffer! Please allocate more size.\n" );
        return;
    }
    u32 cached_offset = allocated_offset;

    char* destination_memory = is_reading ? data_memory : blob_memory;
    memcpy( &destination_memory[ allocated_offset ], text, length );

    allocated_offset += length;

    // Add null termination for string.
    // By allocating one extra character for the null termination this is always safe to do.
    destination_memory[ allocated_offset ] = 0;
    ++allocated_offset;

    string.set( destination_memory + cached_offset, length );
}

i32 BlobSerializer::get_relative_data_offset( void* data ) {
    // data_memory points to the newly allocated data structure to be used at runtime.
    const i32 data_offset_from_start = ( i32 )( ( char* )data - data_memory );
    const i32 data_offset = allocated_offset - data_offset_from_start;
    return data_offset;
}

} // namespace raptor