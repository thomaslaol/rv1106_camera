#include "app/AppController.hpp"
#include "core/VideoEngine.hpp"
#include "core/AudioEngine.hpp"
#include "core/RTSPEngine.hpp"
#include "infra/time/TimeUtils.h"
#include "iostream"
#include <thread>
#include <signal.h>

extern "C"
{
#include "infra/logging/logger.h"
}

std::atomic<bool> g_quit_flag(false);

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
    AppController::AppController() : initialized_(false)
    {
        log_init("log.log", LOG_LEVEL_DEBUG);
        LOGI("init log success!");

        video_engine_ = new core::VideoEngine();
        audio_engine_ = new core::AudioEngine();
        rtsps_engine_ = new core::RTSPEngine();
    }

    AppController::~AppController()
    {
        shutdown();
        log_close();
    }

    AppController &AppController::instance()
    {
        static AppController instance_;
        return instance_;
    }

    int AppController::init()
    {
        // 0. 检查是否已经初始化
        if (initialized_)
        {
            LOGW("is_inited_ already inited!");
            return 0;
        }

        // 1. 清除之前的配置
        system("RkLunch-stop.sh");

        // 2. 初始化视频引擎
        int ret = video_engine_->init();
        CHECK_RET(ret, "video_engine_->init");

        // 3. 初始化音频引擎
        ret = audio_engine_->init();
        CHECK_RET(ret, "audio_engine_->init");

        // 4. 初始化RTSP引擎
        core::RTSPConfig rtsp_config;
        // 推流url
        {
            rtsp_config.output_url = "rtsp://192.168.251.165/live/camera";
        }
        // 视频流参数 (硬编码)
        {
            rtsp_config.video_width = 1920;
            rtsp_config.video_height = 1080;
            rtsp_config.video_bitrate = 5 * 1024 * 1000;
            rtsp_config.video_framerate = 30;
            rtsp_config.video_codec_id = AV_CODEC_ID_H265;
        }
        // 音频流参数 (硬编码)
        {
            rtsp_config.audio_sample_rate = 48000;
            rtsp_config.audio_channels = 1;
            rtsp_config.audio_bitrate = 32 * 1024; // 32 kbps
            rtsp_config.audio_codec_id = AV_CODEC_ID_AAC;
        }
        // 网络参数
        {
            rtsp_config.rw_timeout = 3000000; // 网络超时时间 (微秒)
            rtsp_config.max_delay = 500000;   // 最大延迟 (微秒)
            rtsp_config.enable_tcp = false; // 是否强制使用TCP传输
        }

        ret = rtsps_engine_->init(rtsp_config);
        CHECK_RET(ret, "rtsps_engine_->init");

        initialized_ = true;
        LOGI("AppController::init() - success!");
        return 0;
    }

    int AppController::run()
    {
        if (!initialized_)
        {
            LOGE("not inited! call init first");
            return -1;
        }
        printf("应用层运行\n");

        // 注册信号监听
        signal(SIGINT, signalHandler);

        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 启动视频采集
        printf("启动视频采集\n");
        video_engine_->start();

        // 启动音频采集
        printf("启动音频采集\n");
        audio_engine_->start();

        AVPacket audio_out_pkt = {0};
        AVPacket *video_out_pkt = nullptr;

        int64_t vedio_pts = 0;
        int64_t audio_pts = 0;

        int cnt = 0;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        printf("主线程运行\n");
        uint64_t main_start_time = infra::now_ms();
        uint64_t main_end_time = 0;
        while (!g_quit_flag)
        {
            // main_end_time = infra::now_ms();
            // std::cout << main_end_time - main_start_time << "主线程运行" << std::endl;
            // main_start_time = main_end_time;

            // int ret = 0;

            // // 推音频
            // if (audio_engine_->getProcessedPacket(audio_out_pkt,20))
            // {
            //     rtsps_engine_->pushAudioFrame(&audio_out_pkt);
            //     av_packet_unref(&audio_out_pkt);
            // }
            // else
            // {
            //     printf("test111,\n");
            // }
            // std::this_thread::sleep_for(std::chrono::microseconds(15000));

            // 推视频
            // int v_ret = 0;
            // v_ret = video_engine_->popEncodedPacket(video_out_pkt);
            // if (v_ret == 0)
            // {
            //     rtsps_engine_->pushVideoFrame(video_out_pkt);
            //     av_packet_unref(video_out_pkt);
            //     // printf("已退流\n");
            // }
            // else
            // {
            //     // printf("test111,ret = %d\n", v_ret);
            //     std::this_thread::sleep_for(std::chrono::microseconds(100));
            // }
            // std::this_thread::sleep_for(std::chrono::microseconds(10000));

            // 音视频同步退流
            bool v_ret = video_engine_->getQueueFrontPts(vedio_pts, 1000);
            bool a_ret = audio_engine_->getQueueFrontPts(audio_pts, 1000);

            if (v_ret && a_ret)
            {
                if (audio_pts != 0)
                {
                    audio_pts = audio_pts * 1000000 / 48000;
                }
                if (audio_pts <= vedio_pts)
                {
                    // std::cout << " 音频audio_pts = " << audio_pts << ",   video_pts = " << vedio_pts << std::endl;
                    if (audio_engine_->getProcessedPacket(audio_out_pkt, 0))
                        rtsps_engine_->pushAudioFrame(&audio_out_pkt);
                    else
                    {
                        std::cout << "1音频获取失败" << std::endl;
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                }
                else
                {
                    // std::cout << " 视频video_pts = " << vedio_pts << ",   音频audio_pts = " << audio_pts << std::endl;
                    if (video_engine_->popEncodedPacket(video_out_pkt, 0) == 0)
                        rtsps_engine_->pushVideoFrame(video_out_pkt);
                    else
                    {
                        std::cout << "2视频获取失败" << std::endl;
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                }
            }
            // else if (v_ret && !a_ret)
            // {
            //     // std::cout << " 视频video_pts = " << vedio_pts << ",   音频audio_pts = " << audio_pts << std::endl;
            //     if (video_engine_->popEncodedPacket(video_out_pkt) == 0)
            //         rtsps_engine_->pushVideoFrame(video_out_pkt);
            //     else
            //     {
            //         std::cout << "3视频获取失败" << std::endl;
            //         std::this_thread::sleep_for(std::chrono::microseconds(100));
            //     }
            // }
            // else if (!v_ret && a_ret)
            // {
            //     // std::cout << " 音频audio_pts = " << audio_pts << ",   video_pts = " << vedio_pts << std::endl;
            //     if (audio_engine_->getProcessedPacket(audio_out_pkt))
            //         rtsps_engine_->pushAudioFrame(&audio_out_pkt);
            //     else
            //     {
            //         std::cout << "4音频获取失败" << std::endl;
            //         std::this_thread::sleep_for(std::chrono::microseconds(100));
            //     }
            // }
            else
            {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        printf("循环结束\n");

        return shutdown();
    }

    // app 层关闭：释放 core 层资源
    int AppController::shutdown()
    {
        if (!initialized_)
            return 0;

        printf("关闭video_engine_\n");
        if (video_engine_)
        {
            delete video_engine_;
            video_engine_ = nullptr;
        }

        printf("关闭audio_engine_\n");
        if (audio_engine_)
        {
            delete audio_engine_;
            audio_engine_ = nullptr;
        }

        printf("关闭rtsps_engine_\n");
        if (rtsps_engine_)
        {
            delete rtsps_engine_;
            rtsps_engine_ = nullptr;
        }

        initialized_ = false;
        return 0;
    }

} // namespace app
