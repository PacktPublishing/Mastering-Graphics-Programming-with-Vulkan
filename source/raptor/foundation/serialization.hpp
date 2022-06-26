#pragma once

#include "foundation/platform.hpp"
#include "foundation/file.hpp"

namespace raptor {

// Serialization taken from article https://yave.handmade.network/blogs/p/2723-how_media_molecule_does_serialization
    /*
struct Serializer {

    void            start_writing( FILE* file, u32 data_version );
    void            start_reading( FILE* file );

    FILE*           file_handle;
    u32             data_version;
    u8              is_writing;

}; // struct Serializer

// Serialization methods
void                 Serialize( Serializer* s, i8* data );
void                 Serialize( Serializer* s, u8* data );
void                 Serialize( Serializer* s, i16* data );
void                 Serialize( Serializer* s, u16* data );
void                 Serialize( Serializer* s, i32* data );
void                 Serialize( Serializer* s, u32* data );
void                 Serialize( Serializer* s, i64* data );
void                 Serialize( Serializer* s, u64* data );
void                 Serialize( Serializer* s, f32* data );
void                 Serialize( Serializer* s, f64* data );
void                 Serialize( Serializer* s, bool* data );


// Serialization macros
#define VERSION_IN_RANGE(_from, _to) \
    (s->data_version >= (_from) && serializer->data_version < (_to))

#define ADD(_fieldAdded, _fieldName) \
    if (s->data_version >= (_fieldAdded)) \
    { \
        Serialize(s, &(data->_fieldName)); \
    }

#define ADD_TYPED(_fieldAdded, _fieldName, _castType) \
    if (s->data_version >= (_fieldAdded)) \
    { \
        Serialize(s, (_castType*)&(data->_fieldName)); \
    }

#define ADD_LOCAL(_localAdded, _type, _localName, _defaultValue) \
    _type _localName = (_defaultValue); \
    if (s->data_version >= (_localAdded)) \
    { \
        Serialize(s, &(_localName)); \
    }

#define REM(_fieldAdded, _fieldRemoved, _type, _fieldName, _defaultValue) \
    _type _fieldName = (_defaultValue); \
    if (VERSION_IN_RANGE((_fieldAdded),(_fieldRemoved))) \
    { \
        Serialize(s, &(_fieldName)); \
    }
    */
} // namespace raptor