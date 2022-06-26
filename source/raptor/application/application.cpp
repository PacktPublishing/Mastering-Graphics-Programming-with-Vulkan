#include "application.hpp"

namespace raptor {

void Application::run( const ApplicationConfiguration& configuration ) {

    create( configuration );
    main_loop();
    destroy();
}

} // namespace raptor