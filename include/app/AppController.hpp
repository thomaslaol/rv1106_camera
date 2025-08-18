#pragma once
#include "core/MediaEngine.hpp"

namespace app
{
    class AppController
    {
    public:
        ~AppController();

        // 返回自身实例
        static AppController &instance();

        int init();
        int run();
        int shutdown();

    private:
        AppController();
        // 自身实例
        static AppController *instance_;
        core::MediaEngine *media_engine_;
        bool is_inited_;
        int rtsp_port_ = 554;                    // RTSP默认端口
        const char *rtsp_path_ = "/live/camera"; // RTSP推流路径（客户端通过此路径访问）
        int rtsp_codec_ = 2;                     // 2 ==> RTSP_CODEC_ID_VIDEO_H265; 编码类型（与VENC一致）
    };
}
