#include "game_application.hpp"

#if 0
#include "kernel/service_manager.hpp"
#include "kernel/memory.hpp"
#include "kernel/log.hpp"
#include "kernel/time.hpp"

#include "application/window.hpp"
#include "application/input.hpp"
#include "application/raptor_imgui.hpp"

#include "graphics/gpu_device.hpp"
#include "graphics/renderer.hpp"
#include "graphics/command_buffer.hpp"

#include "cglm/util.h"

#include "imgui/imgui.h"

namespace raptor {

static raptor::Window s_window;

// Callbacks
static void input_os_messages_callback( void* os_event, void* user_data ) {
    raptor::InputService* input = ( raptor::InputService* )user_data;
    input->on_event( os_event );
}


// GameApplication ////////////////////////////////////////////////////////////////

void GameApplication::create( const raptor::ApplicationConfiguration& configuration ) {

    using namespace raptor;
    // Init services
    MemoryService::instance()->init( nullptr );

    time_service_init();

    service_manager = ServiceManager::instance;
    service_manager->init( &MemoryService::instance()->system_allocator );

    // Base create ////////////////////////////////////////////////////////
    
    // window
    WindowConfiguration wconf{ configuration.width, configuration.height, configuration.name, &MemoryService::instance()->system_allocator };    
    window = &s_window;
    window->init( &wconf );
    
    // input
    input = service_manager->get<raptor::InputService>();
    input->init( &MemoryService::instance()->system_allocator );

    // graphics
    raptor::gfx::DeviceCreation dc;
    dc.set_window( window->width, window->height, window->platform_handle ).set_allocator( &MemoryService::instance()->system_allocator );

    raptor::gfx::Device* gpu = service_manager->get<raptor::gfx::Device>();
    gpu->init( dc );

    // Callback register
    window->register_os_messages_callback( input_os_messages_callback, input );

    // App specific create ////////////////////////////////////////////////
    renderer = service_manager->get<raptor::gfx::Renderer>();

    raptor::gfx::RendererCreation rc{ gpu, &raptor::MemoryService::instance()->system_allocator };
    renderer->init( rc );

    // imgui backend
    imgui = service_manager->get<raptor::ImGuiService>();
    imgui->init( renderer );

    hprint( "GameApplication created successfully!\n" );
}

void GameApplication::destroy() {

    hprint( "GameApplication shutdown\n" );

    // Remove callbacks
    window->unregister_os_messages_callback( input_os_messages_callback );

    // Shutdown services
    imgui->shutdown();
    input->shutdown();
    renderer->shutdown();
    window->shutdown();

    time_service_shutdown();

    service_manager->shutdown();

    raptor::MemoryService::instance()->shutdown();
}

bool GameApplication::main_loop() {

    // Fix your timestep
    accumulator = current_time = 0.0;

    // Main loop
    while ( !window->requested_exit ) {
        // New frame
        if ( !window->minimized ) {
            renderer->begin_frame();
        }
        input->new_frame();

        window->handle_os_messages();

        if ( window->resized ) {
            renderer->resize_swapchain( window->width, window->height );
            on_resize( window->width, window->height );
            window->resized = false;
        }
        // This MUST be AFTER os messages!
        imgui->new_frame( window->platform_handle );

        // TODO: frametime
        f32 delta_time = ImGui::GetIO().DeltaTime;

        //hprint( "Dt %f\n", delta_time );
        delta_time = glm_clamp( delta_time, 0.0f, 0.25f );

        accumulator += delta_time;

        // Various updates
        input->update( delta_time );

        while ( accumulator >= step ) {
            fixed_update( step );

            accumulator -= step;
        }

        variable_update( delta_time );

        if ( !window->minimized ) {
            // Draw debug UIs
            raptor::MemoryService::instance()->imgui_draw();

            raptor::gfx::CommandBuffer* gpu_commands = renderer->get_command_buffer( raptor::gfx::QueueType::Graphics, true );
            gpu_commands->push_marker( "Frame" );

            const f32 interpolation_factor = glm_clamp( (f32)(accumulator / step), 0.0f, 1.0f );
            render( interpolation_factor );

            imgui->render( renderer, *gpu_commands );

            gpu_commands->pop_marker();

            // Send commands to GPU
            renderer->queue_command_buffer( gpu_commands );

            renderer->end_frame();
        }
        else {
            ImGui::Render();
        }
        
        // Prepare for next frame if anything must be done.
        frame_end();
    }

    return true;
}

void GameApplication::fixed_update( f32 delta ) {
    
}

void GameApplication::variable_update( f32 delta ) {
    
}

void GameApplication::render( f32 interpolation ) {
    
}

void GameApplication::on_resize( u32 new_width, u32 new_height ) {
    
}

void GameApplication::frame_begin() {

}

void GameApplication::frame_end() {
    
}

} // namespace raptor

#endif