#include "app/AppController.hpp"
#include "core/VideoEngine.hpp"
#include "core/AudioEngine.hpp"
#include "core/RTSPEngine.hpp"
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
            rtsp_config.video_bitrate = 10 * 1024 * 1000; // 10 Mbps
            rtsp_config.video_framerate = 30;
            rtsp_config.video_codec_id = AV_CODEC_ID_H265; // 与 RK_VIDEO_ID_HEVC 对应}
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
            // rtsp_config.rw_timeout = 3000000; // 网络超时时间 (微秒)
            // rtsp_config.max_delay = 500000;   // 最大延迟 (微秒)
            rtsp_config.enable_tcp = true; // 是否强制使用TCP传输
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
        while (!g_quit_flag)
        {
            int ret = 0;
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
            bool v_ret = video_engine_->getQueueFrontPts(vedio_pts, 20);
            bool a_ret = audio_engine_->getQueueFrontPts(audio_pts, 20);

            if (v_ret && a_ret)
            {
                if (audio_pts != 0)
                {
                    audio_pts = audio_pts * 1000000 / 48000;
                }
                if (audio_pts <= vedio_pts)
                {
                    // std::cout << " 音频audio_pts = " << audio_pts << ",   video_pts = " << vedio_pts << std::endl;
                    audio_engine_->getProcessedPacket(audio_out_pkt);
                    rtsps_engine_->pushAudioFrame(&audio_out_pkt);
                }
                else
                {
                    // std::cout << " 视频video_pts = " << vedio_pts << ",   音频audio_pts = " << audio_pts << std::endl;
                    video_engine_->popEncodedPacket(video_out_pkt);
                    rtsps_engine_->pushVideoFrame(video_out_pkt);
                }
            }
            else if (v_ret && !a_ret)
            {
                // std::cout << " 视频video_pts = " << vedio_pts << ",   音频audio_pts = " << audio_pts << std::endl;
                video_engine_->popEncodedPacket(video_out_pkt);
                rtsps_engine_->pushVideoFrame(video_out_pkt);
            }
            else if (!v_ret && a_ret)
            {
                // std::cout << " 音频audio_pts = " << audio_pts << ",   video_pts = " << vedio_pts << std::endl;
                audio_engine_->getProcessedPacket(audio_out_pkt);
                rtsps_engine_->pushAudioFrame(&audio_out_pkt);
            }

            // 既有视频又有音频
            // if (v_ret && a_ret)
            // {
            //     // 0 = 0 第一帧
            //     if (vedio_pts == 0)
            //     {
            //         ret = video_engine_->popEncodedPacket(video_out_pkt);
            //         if (ret == 0)
            //         {
            //             printf("第一正 ---已退流音频，时间戳为：%lld\n", vedio_pts);
            //             rtsps_engine_->pushVideoFrame(video_out_pkt);
            //             av_packet_unref(video_out_pkt);
            //         }
            //     }
            //     if (audio_pts == 0)
            //     {
            //         ret = audio_engine_->getProcessedPacket(audio_out_pkt);
            //         if (ret == 0)
            //         {
            //             printf("第一正 ---已退流音频，时间戳为：%lld\n", audio_pts);
            //             rtsps_engine_->pushAudioFrame(&audio_out_pkt);
            //             av_packet_unref(&audio_out_pkt);
            //         }
            //     }
            //     // 不是第一正
            //     else
            //     {
            //         audio_pts = audio_pts * 1000000 / 48000;
            //         // 音频小
            //         if (audio_pts <= vedio_pts)
            //         {
            //             ret = audio_engine_->getProcessedPacket(audio_out_pkt);
            //             if (ret == 0)
            //             {
            //                 printf("已退流音频时间戳为：%lld,视频时间戳为：%lld\n", audio_pts, vedio_pts);
            //                 rtsps_engine_->pushAudioFrame(&audio_out_pkt);
            //                 av_packet_unref(&audio_out_pkt);
            //             }
            //         }
            //         // 视频小
            //         else
            //         {
            //             ret = video_engine_->popEncodedPacket(video_out_pkt);
            //             if (ret == 0)
            //             {
            //                 printf("已退流视频时间戳为：%lld,音频时间戳为：%lld\n", vedio_pts, audio_pts);
            //                 rtsps_engine_->pushVideoFrame(video_out_pkt);
            //                 av_packet_unref(video_out_pkt);
            //             }
            //         }
            //     }
            // }
            // // 只有视频
            // else if (v_ret && !a_ret)
            // {
            //     ret = video_engine_->popEncodedPacket(video_out_pkt);
            //     if (ret == 0)
            //     {
            //         printf("只有视频已退流时间戳为：%lld\n", vedio_pts);
            //         rtsps_engine_->pushVideoFrame(video_out_pkt);
            //         av_packet_unref(video_out_pkt);
            //     }
            //     std::this_thread::sleep_for(std::chrono::microseconds(100));
            // }
            // // 只有音频
            // else if (!v_ret && a_ret)
            // {
            //     ret = audio_engine_->getProcessedPacket(audio_out_pkt);
            //     if (ret == 0)
            //     {
            //         printf("只有音频已退流时间戳为：%lld\n", audio_pts);
            //         rtsps_engine_->pushAudioFrame(&audio_out_pkt);
            //         av_packet_unref(&audio_out_pkt);
            //     }
            //     else
            //     {
            //         std::this_thread::sleep_for(std::chrono::microseconds(100));
            //     }
            // }
            // // 音视频都没有
            // else
            // {
            //     printf("音视频都没有\n");
            // }
            std::this_thread::sleep_for(std::chrono::microseconds(10));
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
