#include "main.hpp"

int main(int argc, char *argv[])
{
    auto &app_controller = app::AppController::instance();
    app_controller.init();
    return app_controller.run();
}