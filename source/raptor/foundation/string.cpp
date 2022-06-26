#include "string.hpp"
#include "memory.hpp"
#include "log.hpp"
#include "assert.hpp"

#include <stdio.h>
#include <stdarg.h>
#include <memory.h>

#include "hash_map.hpp"

#define ASSERT_ON_OVERFLOW

#if defined (ASSERT_ON_OVERFLOW)
#define RASSERT_OVERFLOW() RASSERT(false)
#else
#define RASSERT_OVERFLOW()
#endif // ASSERT_ON_OVERFLOW

namespace raptor {

// StringView ///////////////////////////////////////////////////////////////////
bool StringView::equals( const StringView& a, const StringView& b ) {
    if ( a.length != b.length )
        return false;

    for ( u32 i = 0; i < a.length; ++i ) {
        if ( a.text[ i ] != b.text[ i ] ) {
            return false;
        }
    }

    return true;
}

void StringView::copy_to( const StringView& a, char* buffer, sizet buffer_size ) {
    // Take in account the null vector
    const sizet max_length = buffer_size - 1 < a.length ? buffer_size - 1 : a.length;
    memory_copy( buffer, a.text, max_length );
    buffer[ a.length ] = 0;
}


//
// StringBuffer /////////////////////////////////////////////////////////////////
void StringBuffer::init( sizet size, Allocator* allocator_ ) {
    if ( data ) {
        allocator->deallocate( data );
    }

    if ( size < 1 ) {
        rprint( "ERROR: Buffer cannot be empty!\n" );
        return;
    }
    allocator = allocator_;
    data = ( char* )ralloca( size + 1, allocator_ );
    RASSERT( data );
    data[ 0 ] = 0;
    buffer_size = ( u32 )size;
    current_size = 0;
}

void StringBuffer::shutdown() {

    rfree( data, allocator );

    buffer_size = current_size = 0;
}

void StringBuffer::append( const char* string ) {
    append_f( "%s", string );
}

void StringBuffer::append_f( const char* format, ... ) {
    if ( current_size >= buffer_size ) {
        RASSERT_OVERFLOW();
        rprint( "Buffer full! Please allocate more size.\n" );
        return;
    }

    // TODO: safer version!
    va_list args;
    va_start( args, format );
#if defined(_MSC_VER)
    int written_chars = vsnprintf_s( &data[ current_size ], buffer_size - current_size, _TRUNCATE, format, args );
#else
    int written_chars = vsnprintf( &data[ current_size ], buffer_size - current_size, format, args );
#endif

    current_size += written_chars > 0 ? written_chars : 0;
    va_end( args );

    if ( written_chars < 0 ) {
        RASSERT_OVERFLOW();
        rprint( "New string too big for current buffer! Please allocate more size.\n" );
    }
}

void StringBuffer::append( const StringView& text ) {
    const sizet max_length = current_size + text.length < buffer_size ? text.length : buffer_size - current_size;
    if ( max_length == 0 || max_length >= buffer_size ) {
        RASSERT_OVERFLOW();
        rprint( "Buffer full! Please allocate more size.\n" );
        return;
    }

    memcpy( &data[ current_size ], text.text, max_length );
    current_size += ( u32 )max_length;

    // Add null termination for string.
    // By allocating one extra character for the null termination this is always safe to do.
    data[ current_size ] = 0;
}

void StringBuffer::append_m( void* memory, sizet size ) {

    if ( current_size + size >= buffer_size ) {
        RASSERT_OVERFLOW();
        rprint( "Buffer full! Please allocate more size.\n" );
        return;
    }

    memcpy( &data[ current_size ], memory, size );
    current_size += ( u32 )size;
}

void StringBuffer::append( const StringBuffer& other_buffer ) {

    if ( other_buffer.current_size == 0 ) {
        return;
    }

    if ( current_size + other_buffer.current_size >= buffer_size ) {
        RASSERT_OVERFLOW();
        rprint( "Buffer full! Please allocate more size.\n" );
        return;
    }

    memcpy( &data[ current_size ], other_buffer.data, other_buffer.current_size );
    current_size += other_buffer.current_size;
}


char* StringBuffer::append_use( const char* string ) {
    return append_use_f( "%s", string );
}

char* StringBuffer::append_use_f( const char* format, ... ) {
    u32 cached_offset = this->current_size;

    // TODO: safer version!
    // TODO: do not copy paste!
    if ( current_size >= buffer_size ) {
        RASSERT_OVERFLOW();
        rprint( "Buffer full! Please allocate more size.\n" );
        return nullptr;
    }

    va_list args;
    va_start( args, format );
#if defined(_MSC_VER)
    int written_chars = vsnprintf_s( &data[ current_size ], buffer_size - current_size, _TRUNCATE, format, args );
#else
    int written_chars = vsnprintf( &data[ current_size ], buffer_size - current_size, format, args );
#endif
    current_size += written_chars > 0 ? written_chars : 0;
    va_end( args );

    if ( written_chars < 0 ) {
        rprint( "New string too big for current buffer! Please allocate more size.\n" );
    }

    // Add null termination for string.
    // By allocating one extra character for the null termination this is always safe to do.
    data[ current_size ] = 0;
    ++current_size;

    return this->data + cached_offset;
}

char* StringBuffer::append_use( const StringView& text ) {
    u32 cached_offset = this->current_size;

    append( text );
    ++current_size;

    return this->data + cached_offset;
}

char* StringBuffer::append_use_substring( const char* string, u32 start_index, u32 end_index ) {
    u32 size = end_index - start_index;
    if ( current_size + size >= buffer_size ) {
        RASSERT_OVERFLOW();
        rprint( "Buffer full! Please allocate more size.\n" );
        return nullptr;
    }   

    u32 cached_offset = this->current_size;

    memcpy( &data[ current_size ], string, size );
    current_size += size;

    data[ current_size ] = 0;
    ++current_size;

    return this->data + cached_offset;
}

void StringBuffer::close_current_string() {
    data[ current_size ] = 0;
    ++current_size;
}

u32 StringBuffer::get_index( cstring text ) const {
    u64 text_distance = text - data;
    // TODO: how to handle an error here ?
    return text_distance < buffer_size ? u32( text_distance ) : u32_max;
}

cstring StringBuffer::get_text( u32 index ) const {
    // TODO: how to handle an error here ?
    return index < buffer_size ? cstring(data + index) : nullptr;
}

char* StringBuffer::reserve( sizet size ) {
    if ( current_size + size >= buffer_size )
        return nullptr;

    u32 offset = current_size;
    current_size += ( u32 )size;

    return data + offset;
}

void StringBuffer::clear() {
    current_size = 0;
    data[ 0 ] = 0;
}

// StringArray ////////////////////////////////////////////////////////////
void StringArray::init( u32 size, Allocator* allocator_ ) {

    allocator = allocator_;
    // Allocate also memory for the hash map
    char* allocated_memory = ( char* )allocator_->allocate( size + sizeof(FlatHashMap<u64, u32>) + sizeof(FlatHashMapIterator), 1 );
    string_to_index = ( FlatHashMap<u64, u32>* )allocated_memory;
    string_to_index->init( allocator, 8 );
    string_to_index->set_default_value( u32_max );

    strings_iterator = ( FlatHashMapIterator* )( allocated_memory + sizeof( FlatHashMap<u64, u32> ) );

    data = allocated_memory + sizeof( FlatHashMap<u64, u32> ) + sizeof(FlatHashMapIterator);

    buffer_size = size;
    current_size = 0;
}

void StringArray::shutdown() {
    // string_to_index contains ALL the memory including data.
    rfree( string_to_index, allocator );

    buffer_size = current_size = 0;
}

void StringArray::clear() {
    current_size = 0;
    
    string_to_index->clear();
}

FlatHashMapIterator* StringArray::begin_string_iteration() {
    *strings_iterator = string_to_index->iterator_begin();
    return strings_iterator;
}

sizet StringArray::get_string_count() const {
    return string_to_index->size;
}

cstring StringArray::get_next_string( FlatHashMapIterator* it ) const {
    u32 index = string_to_index->get( *it );
    string_to_index->iterator_advance( *it );
    cstring string = get_string( index );
    return string;
}

bool StringArray::has_next_string( FlatHashMapIterator* it ) const {
    return it->is_valid();
}

cstring StringArray::get_string( u32 index ) const {
    u32 data_index = index;
    if ( data_index < current_size ) {
        return data + data_index ;
    }
    return nullptr;
}

cstring StringArray::intern( cstring string ) {
    static sizet seed = 0xf2ea4ffad;
    const sizet length = strlen( string );
    const sizet hashed_string = raptor::hash_bytes( ( void* )string, length, seed );

    u32 string_index = string_to_index->get( hashed_string );
    if ( string_index != u32_max ) {
        return data + string_index;
    }

    string_index = current_size;
    // Increase current buffer with new interned string
    current_size += ( u32 )length + 1; // null termination
    strcpy( data + string_index, string );

    // Update hash map
    string_to_index->insert( hashed_string, string_index );

    return data + string_index;
}

} // namespace raptor