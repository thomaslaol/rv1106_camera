#include "main.hpp"

int main(int argc, char* argv[])
{
    app::AppController app_controller;
    app_controller.init();
    return app_controller.run();
}