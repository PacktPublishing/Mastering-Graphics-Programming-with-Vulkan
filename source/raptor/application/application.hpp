#pragma once

#include "foundation/platform.hpp"

namespace raptor {

struct ServiceManager;

struct ApplicationConfiguration {

    u32                         width;
    u32                         height;

    cstring                     name = nullptr;

    bool                        init_base_services  = false;

    ApplicationConfiguration&   w( u32 value ) { width = value; return *this; }
    ApplicationConfiguration&   h( u32 value ) { height = value; return *this; }
    ApplicationConfiguration&   name_( cstring value ) { name = value; return *this; }

}; // struct ApplicationConfiguration

struct Application {
    // 
    virtual void                create( const ApplicationConfiguration& configuration ) {}
    virtual void                destroy() {}
    virtual bool                main_loop() { return false; }

    // Fixed update. Can be called more than once compared to rendering.
    virtual void                fixed_update( f32 delta ) {}
    // Variable time update. Called only once per frame.
    virtual void                variable_update( f32 delta ) {}
    // Rendering with optional interpolation factor.
    virtual void                render( f32 interpolation ) {}
    // Per frame begin/end.
    virtual void                frame_begin() {}
    virtual void                frame_end() {}

    void                        run( const ApplicationConfiguration& configuration );

    ServiceManager*             service_manager = nullptr;

}; // struct Application

} // namespace raptor