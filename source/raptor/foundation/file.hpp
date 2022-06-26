#pragma once

#include "foundation/platform.hpp"
#include <stdio.h>

namespace raptor {

    struct Allocator;
    struct StringArray;

#if defined(_WIN64)

    typedef struct __FILETIME {
        unsigned long       dwLowDateTime;
        unsigned long       dwHighDateTime;
    } FILETIME, * PFILETIME, * LPFILETIME;

    using FileTime = __FILETIME;

#endif

    using FileHandle = FILE*;

    static const u32                k_max_path = 512;

    //
    //
    struct Directory {
        char                        path[ k_max_path ];

#if defined (_WIN64)
        void*                       os_handle;
#endif
    }; // struct Directory

    struct FileReadResult {
        char*                       data;
        sizet                       size;
    };

    // Read file and allocate memory from allocator.
    // User is responsible for freeing the memory.
    char*                           file_read_binary( cstring filename, Allocator* allocator, sizet* size );
    char*                           file_read_text( cstring filename, Allocator* allocator, sizet* size );

    FileReadResult                  file_read_binary( cstring filename, Allocator* allocator );
    FileReadResult                  file_read_text( cstring filename, Allocator* allocator );

    void                            file_write_binary( cstring filename, void* memory, sizet size );

    bool                            file_exists( cstring path );
    void                            file_open( cstring filename, cstring mode, FileHandle* file );
    void                            file_close( FileHandle file );
    sizet                           file_write( uint8_t* memory, u32 element_size, u32 count, FileHandle file );
    bool                            file_delete( cstring path );

#if defined(_WIN64)
    FileTime                        file_last_write_time( cstring filename );
#endif

    // Try to resolve path to non-relative version.
    u32                             file_resolve_to_full_path( cstring path, char* out_full_path, u32 max_size );

    // Inplace path methods
    void                            file_directory_from_path( char* path ); // Retrieve path without the filename. Path is a preallocated string buffer. It moves the terminator before the name of the file.
    void                            file_name_from_path( char* path );
    char*                           file_extension_from_path( char* path );

    bool                            directory_exists( cstring path );
    bool                            directory_create( cstring path );
    bool                            directory_delete( cstring path );

    void                            directory_current( Directory* directory );
    void                            directory_change( cstring path );

    void                            file_open_directory( cstring path, Directory* out_directory );
    void                            file_close_directory( Directory* directory );
    void                            file_parent_directory( Directory* directory );
    void                            file_sub_directory( Directory* directory, cstring sub_directory_name );

    void                            file_find_files_in_path( cstring file_pattern, StringArray& files );            // Search files matching file_pattern and puts them in files array.
                                                                                                                    // Examples: "..\\data\\*", "*.bin", "*.*"
    void                            file_find_files_in_path( cstring extension, cstring search_pattern,
                                                             StringArray& files, StringArray& directories );        // Search files and directories using search_patterns.

    // TODO: move
    void                            environment_variable_get( cstring name, char* output, u32 output_size );

    struct ScopedFile {
        ScopedFile( cstring filename, cstring mode );
        ~ScopedFile();

        FileHandle                  file;
    }; // struct ScopedFile

} // namespace raptor
