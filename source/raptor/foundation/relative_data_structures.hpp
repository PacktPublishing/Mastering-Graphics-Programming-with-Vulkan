#pragma once

#include "foundation/platform.hpp"
#include "foundation/memory.hpp"
#include "foundation/assert.hpp"
#include "foundation/array.hpp"

// Defines:
// RAPTOR_BLOB_WRITE         - use it in code that can write blueprints,
//                            like data compilers.
#define RAPTOR_BLOB_WRITE

namespace raptor {

struct Allocator;

//
//
template <typename T>
struct RelativePointer {

    T*                  get() const;

    bool                is_equal( const RelativePointer& other ) const;
    bool                is_null() const;
    bool                is_not_null() const;

    // Operator overloading to give a cleaner interface
    T*                  operator->() const;
    T&                  operator*() const;

#if defined RAPTOR_BLOB_WRITE
    void                set( char* raw_pointer );
    void                set_null();
#endif // RAPTOR_BLOB_WRITE

    i32                 offset;

}; // struct RelativePointer


// RelativeArray //////////////////////////////////////////////////////////

//
//
template <typename T>
struct RelativeArray {

    const T&                operator[](u32 index) const;
    T&                      operator[](u32 index);

    const T*                get() const;
    T*                      get();

#if defined RAPTOR_BLOB_WRITE
    void                    set( char* raw_pointer, u32 size );
    void                    set_empty();
#endif // RAPTOR_BLOB_WRITE

    u32                     size;
    RelativePointer<T>      data;
}; // struct RelativeArray


// RelativeString /////////////////////////////////////////////////////////

//
//
struct RelativeString : public RelativeArray<char> {

    cstring             c_str() const { return data.get(); }

    void                set( char* pointer_, u32 size_ ) { RelativeArray<char>::set( pointer_, size_ ); }
}; // struct RelativeString



// Implementations/////////////////////////////////////////////////////////

// RelativePointer ////////////////////////////////////////////////////////
template<typename T>
inline T* RelativePointer<T>::get() const {
    char* address = ( ( char* )&offset ) + offset;
    return offset != 0 ? ( T* )address : nullptr;
}

template<typename T>
inline bool RelativePointer<T>::is_equal( const RelativePointer& other ) const {
    return get() == other.get();
}

template<typename T>
inline bool RelativePointer<T>::is_null() const {
    return offset == 0;
}

template<typename T>
inline bool RelativePointer<T>::is_not_null() const {
    return offset != 0;
}

template<typename T>
inline T* RelativePointer<T>::operator->() const {
    return get();
}

template<typename T>
inline T& RelativePointer<T>::operator*() const {
    return *( get() );
}

#if defined RAPTOR_BLOB_WRITE
/* // TODO: useful or not ?
template<typename T>
inline void RelativePointer<T>::set( T* pointer ) {
    offset = pointer ? ( i32 )( ( ( char* )pointer ) - ( char* )this ) : 0;
}*/

template<typename T>
inline void RelativePointer<T>::set( char* raw_pointer ) {
    offset = raw_pointer ? ( i32 )( raw_pointer - ( char* )this ) : 0;
}
template<typename T>
inline void RelativePointer<T>::set_null() {
    offset = 0;
}
#endif // RAPTOR_BLOB_WRITE

// RelativeArray //////////////////////////////////////////////////////////
template<typename T>
inline const T& RelativeArray<T>::operator[]( u32 index ) const {
    RASSERT( index < size );
    return data.get()[ index ];
}

template<typename T>
inline T& RelativeArray<T>::operator[]( u32 index ) {
    RASSERT( index < size );
    return data.get()[ index ];
}

template<typename T>
inline const T* RelativeArray<T>::get() const {
    return data.get();
}

template<typename T>
inline T* RelativeArray<T>::get() {
    return data.get();
}

#if defined RAPTOR_BLOB_WRITE
template<typename T>
inline void RelativeArray<T>::set( char* raw_pointer, u32 size_ ) {
    data.set( raw_pointer );
    size = size_;
}
template<typename T>
inline void RelativeArray<T>::set_empty() {
    size = 0;
    data.set_null();
}
#endif // RAPTOR_BLOB_WRITE

} // namespace raptor