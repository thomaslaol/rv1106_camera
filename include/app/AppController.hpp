#pragma once
#include "core/MediaEngine.hpp"

namespace app
{
    class AppController
    {
    public:
        AppController();
        ~AppController();

        int init();
        int run();
        int shutdown();

    private:
        core::MediaEngine *media_engine_;
        bool is_inited_;
    };
}
