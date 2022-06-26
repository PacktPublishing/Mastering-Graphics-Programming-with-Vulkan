#pragma once

#if 0

#include "application/application.hpp"

#include "graphics/debug_renderer.hpp"
#include "graphics/camera.hpp"

#include "foundation/array.hpp"

// Forward declarations
namespace raptor {
    struct ApplicationConfiguration;
    struct Window;
    struct InputService;
    struct MemoryService;
    struct LogService;
    struct ImGuiService;

    struct Device;
    struct Renderer;
    struct Texture;
} // namespace raptor

// GameApplication ///////////////////////////////////////////////////////

namespace raptor {

struct GameApplication : public Application {

    void                        create( const raptor::ApplicationConfiguration& configuration ) override;
    void                        destroy() override;
    bool                        main_loop() override;

    void                        fixed_update( f32 delta ) override;
    void                        variable_update( f32 delta ) override;

    void                        frame_begin() override;
    void                        frame_end() override;

    void                        render( f32 interpolation ) override;

    void                        on_resize( u32 new_width, u32 new_height );

    f64                         accumulator     = 0.0;
    f64                         current_time    = 0.0;
    f32                         step            = 1.0f / 60.0f;

    raptor::Window*              window          = nullptr;

    raptor::InputService*        input           = nullptr;
    raptor::gfx::Renderer*       renderer        = nullptr;
    raptor::ImGuiService*        imgui           = nullptr;

}; // struct GameApp

} // namespace raptor

#endif