#pragma once

#include "foundation/platform.hpp"

namespace raptor {


//
//
// Memory blob used to serialize versioned data.
// Uses a serialized offset to track where to read/write memory from/to, and an allocated
// offset to track where to allocate memory from when writing, so that Relative structures
// like pointers and arrays can be serialized.
//
// TODO: when finalized and when reading, if data version matches between the one written in
// the file and the root structure is marked as 'relative only', memory mappable is doable and
// thus serialization is automatic.
//
struct BlobHeader {
    u32                 version;
    u32                 mappable;
}; // struct BlobHeader

//
//
struct Blob {
    BlobHeader          header;
}; // struct Blob

} // namespace raptor