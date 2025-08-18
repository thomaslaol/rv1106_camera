#include "app/AppController.hpp"
#include "core/MediaEngine.hpp"

extern "C"
{
#include "infra/logging/logger.h"
#include "sample_comm.h"
#include "rtsp_demo.h"
}

#include <signal.h>

static bool g_quit_flag = false;

// 信号处理函数（收到 Ctrl+C 时触发）
static void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        printf("\n[AppController] received Ctrl+C, preparing to quit...\n");
        g_quit_flag = true;
    }
}

namespace app
{

    AppController::AppController() : is_inited_(false)
    {
        // 初始化自定义log
        log_init("log.log", LOG_LEVEL_DEBUG);
        LOGI("init log success!");

        printf("AppController构造函数");
        printf("rtsp_port_ = %d,rtsp_path_ = %s,rtsp_codec_ = %d\n", rtsp_port_, rtsp_path_, rtsp_codec_);
        media_engine_ = new core::MediaEngine(rtsp_port_, rtsp_path_, rtsp_codec_);
    }

    AppController::~AppController()
    {
        shutdown(); // 退出时自动释放资源
        if (media_engine_)
        {
            delete media_engine_;
            media_engine_ = nullptr;
        }
        log_close();
    }

    int AppController::init()
    {
        if (is_inited_)
        {
            printf("is_inited_ already inited!\n");
            return 0;
        }

        // 清除之前的配置
        system("RkLunch-stop.sh");

        // 初始化自定义log
        // log_init("log.log", LOG_LEVEL_DEBUG);
        // LOGI("init log success!");

        // 1. 调用 core 层初始化（传入业务参数，如编码格式、分辨率，不碰硬件细节）
        int ret = media_engine_->init(RK_VIDEO_ID_HEVC, 1920, 1080); // H265 编码 + 1080P 分辨率
        CHECK_RET(ret, "media_engine_->init");

        is_inited_ = true;
        LOGI("init success!");
        return 0;
    }

    // app 层运行：启动业务流程 + 阻塞等待退出（避免程序直接退出）
    int AppController::run()
    {
        if (!is_inited_)
        {
            LOGE("not inited! call init first");
            return -1;
        }

        // 注册信号监听（监听 Ctrl+C）
        // signal(SIGINT, signalHandler);

        // 1. 启动 core 层业务（采集→编码→输出）
        int ret = media_engine_->start();
        CHECK_RET(ret, "media_engine_->start");

        LOGI("business running... (Press Ctrl+C to stop)");

        while (!g_quit_flag)
        {
            sleep(1);
        }

        // 退出前调用 shutdown 释放资源
        shutdown();

        return 0;
    }

    // app 层关闭：释放 core 层资源
    int AppController::shutdown()
    {
        if (media_engine_ && is_inited_)
        {
            media_engine_->stop(); // 停止 core 层业务
            LOGI("shutdown success!\n");
        }
        is_inited_ = false;
        return 0;
    }

} // namespace app
