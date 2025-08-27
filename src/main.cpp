#include "main.hpp"

int main(int , char **)
{
    auto &app_controller = app::AppController::instance();
    app_controller.init();
    return app_controller.run();
}