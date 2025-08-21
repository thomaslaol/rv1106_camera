#include "app/AppController.hpp"
#include "core/MediaEngine.hpp"
#include "core/AudioEngine.hpp"
#include <iostream>

extern "C"
{
#include "infra/logging/logger.h"
#include "sample_comm.h"
#include "rtsp_demo.h"
}

#include <signal.h>

bool g_quit_flag = false;

// 信号处理函数（收到 Ctrl+C 时触发）
static void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        printf("\n[AppController] received Ctrl+C, preparing to quit...\n");
        g_quit_flag = true;
        // app::AppController::instance().shutdown();
    }
}

namespace app
{

    AppController::AppController() : is_inited_(false)
    {
        // 初始化自定义log
        log_init("log.log", LOG_LEVEL_DEBUG);
        LOGI("init log success!");

        media_engine_ = new core::MediaEngine();
        audio_engine_ = new core::AudioEngine();
    }

    AppController::~AppController()
    {
        if (media_engine_)
        {
            delete media_engine_;
            media_engine_ = nullptr;
        }

        if (audio_engine_)
        {
            delete audio_engine_;
            audio_engine_ = nullptr;
        }

        log_close();
    }

    AppController &AppController::instance()
    {
        static AppController instance_;
        return instance_;
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

        int ret = 0;
        // 1 初始化媒体视频配置
        // int ret = media_engine_->init(RK_VIDEO_ID_HEVC, 1920, 1080);
        // CHECK_RET(ret, "media_engine_->init");

        // 2 初始化音频配置
        core::AudioEngineConfig config;
        // 输入设备配置
        config.input_config.device_name = "default";
        
        config.input_config.sample_rate = 48000;
        config.input_config.channels = 1;
        config.input_config.format = "s16le";
        // config.input_config.format = "s16_LE";
        // config.input_config.format = "s16";

        // 编码器配置
        config.encode_config.codec_name = "libfdk_aac";
        config.encode_config.bit_rate = 32000;
        config.encode_config.sample_rate = 48000; // 与输入一致
        config.encode_config.channels = 1;

        // 流处理器配置
        config.stream_config.add_adts_header = true;
        config.stream_config.buffer_size = 30;

        ret = audio_engine_->init(config);
        CHECK_RET(ret, "audio_engine_->init");

        is_inited_ = true;
        LOGI("init success!, is_inited_ = %d", is_inited_ ? 1 : 0);

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
        signal(SIGINT, signalHandler);

        // 音频

        // 3. 设置输出文件（保存为AAC）
        auto *stream_processor = audio_engine_->getStreamProcessor();
        if (stream_processor->setOutputFile("captured_audio.aac") != 0)
        {
            std::cerr << "Failed to open output file" << std::endl;
            return -1;
        }
        //  3. 启动音频流程
        audio_engine_->start();

        std::this_thread::sleep_for(std::chrono::seconds(5)); // 采集5秒

        // 5. 停止并清理
        audio_engine_->stop();
        stream_processor->closeOutputFile();
        std::cout << "Audio saved to captured_audio.aac" << std::endl;

        // // 4. 获取流处理器用于数据传输（如RTSP）
        // auto *stream_processor = audio_engine_->getStreamProcessor();
        // AVPacket pkt;
        // while (audio_engine_->isRunning())
        // {
        //     if (stream_processor->getProcessedPacket(pkt))
        //     {
        //         // 发送音频数据
        //         audio_engine_->send(pkt);
        //         av_packet_unref(&pkt);
        //     }
        // }

        // // 5. 停止并清理
        // audio_engine_->stop();

        // 1. 启动 core 层业务（采集→编码→输出）
        // int ret = media_engine_->start();
        // CHECK_RET(ret, "media_engine_->start");

        /*
        LOGI("business running... (Press Ctrl+C to stop)");

        while (!g_quit_flag)
        {
            media_engine_->run();
        }

        sleep(1);

        // 退出前调用 shutdown 释放资源
        shutdown();
*/

        return 0;
    }

    // app 层关闭：释放 core 层资源
    int AppController::shutdown()
    {
        printf("AppController::shutdown()\n");
        if (media_engine_ && is_inited_)
        {
            printf(" 停止 core 层业务,is_inited_ = %d\n", is_inited_ ? 1 : 0);
            media_engine_->stop(); // 停止 core 层业务
            LOGI("shutdown success!");
        }

        is_inited_ = false;
        return 0;
    }

} // namespace app
