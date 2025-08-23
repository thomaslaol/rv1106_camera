#include "app/AppController.hpp"
#include "core/VideoEngine.hpp"
#include "core/AudioEngine.hpp"
#include "core/RTSPEngine.hpp"

extern "C"
{
#include "infra/logging/logger.h"
    // #include "sample_comm.h"
    // #include "rtsp_demo.h"
}

#include <signal.h>
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

        int ret = 0;
        // 2. 初始化视频引擎
        // ret = video_engine_->init(RK_VIDEO_ID_HEVC, 1920, 1080);
        // CHECK_RET(ret, "video_engine_->init");

        // 3. 初始化音频引擎
        core::AudioEngineConfig audia_config;
        // 输入设备配置
        {
            audia_config.input_config.device_name = "default";
            audia_config.input_config.sample_rate = 48000;
            audia_config.input_config.channels = 1;
            audia_config.input_config.format = "s16le";
        }
        // 编码器配置
        {
            audia_config.encode_config.codec_name = "libfdk_aac";
            audia_config.encode_config.bit_rate = 64000;
            audia_config.encode_config.sample_rate = 48000;
            audia_config.encode_config.channels = 1;
        }
        // 流处理器配置
        {
            audia_config.stream_config.add_adts_header = true;
            audia_config.stream_config.buffer_size = 30;
        }

        ret = audio_engine_->init(audia_config);
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
            rtsp_config.video_bitrate = 10 * 1024 * 1024; // 10 Mbps
            rtsp_config.video_framerate = 30;
            rtsp_config.video_codec_id = AV_CODEC_ID_H265; // 与 RK_VIDEO_ID_HEVC 对应}
        }
        // 音频流参数 (硬编码)
        {
            rtsp_config.audio_sample_rate = 48000;
            rtsp_config.audio_channels = 1;
            rtsp_config.audio_bitrate = 64 * 1024; // 64 kbps
            rtsp_config.audio_codec_id = AV_CODEC_ID_AAC;
        }
        // 网络参数
        {
            rtsp_config.rw_timeout = 3000000; // 网络超时时间 (微秒)
            rtsp_config.max_delay = 500000;   // 最大延迟 (微秒)
            rtsp_config.enable_tcp = false;   // 是否强制使用TCP传输
        }

        ret = rtsps_engine_->init(rtsp_config);
        CHECK_RET(ret, "rtsps_engine_->init");

        // 3. 设置输出文件（保存为AAC）
        auto *stream_processor = audio_engine_->getStreamProcessor();
        if (stream_processor->setOutputFile("captured_audio.aac") != 0)
        {
            std::cerr << "Failed to open output file" << std::endl;
            return -1;
        }

        initialized_ = true;
        LOGI("init success!, initialized_ = %d", initialized_ ? 1 : 0);

        return 0;
    }

    // app 层运行
    int AppController::run()
    {
        if (!initialized_)
        {
            LOGE("not inited! call init first");
            return -1;
        }
        printf("应用层运行\n");

        // 注册信号监听（监听 Ctrl+C）
        signal(SIGINT, signalHandler);

        // 启动推流
        // rtsps_engine_->start();

        // 启动视频采集
        // video_engine_->start();

        printf("启动音频采集\n");
        // 启动音频采集
        audio_engine_->start();

        printf("主线程运行\n");
        while (!g_quit_flag)
        {
            printf("test111\n");
            // 延时
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
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
