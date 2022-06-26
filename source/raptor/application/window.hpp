#pragma once

#include "foundation/service.hpp"
#include "foundation/array.hpp"

namespace raptor {

struct WindowConfiguration {

    u32             width;
    u32             height;

    cstring         name;

    Allocator*      allocator;

}; // struct WindowConfiguration

typedef void        ( *OsMessagesCallback )( void* os_event, void* user_data );

struct Window : public Service {

    void            init( void* configuration ) override;
    void            shutdown() override;

    void            handle_os_messages();

    void            set_fullscreen( bool value );

    void            register_os_messages_callback( OsMessagesCallback callback, void* user_data );
    void            unregister_os_messages_callback( OsMessagesCallback callback );

    void            center_mouse( bool dragging );

    Array<OsMessagesCallback> os_messages_callbacks;
    Array<void*>    os_messages_callbacks_data;

    void*           platform_handle     = nullptr;
    bool            requested_exit      = false;
    bool            resized             = false;
    bool            minimized           = false;
    u32             width               = 0;
    u32             height              = 0;
    f32             display_refresh     = 1.0f / 60.0f;

    static constexpr cstring    k_name = "raptor_window_service";

}; // struct Window

} // namespace raptor