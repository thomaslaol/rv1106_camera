#pragma once

#include <string>
#include <atomic>
#include <thread>

// FFmpeg 头文件
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace core
{

    // 推流引擎配置结构体
    struct RTSPConfig
    {
        std::string output_url = "rtsp://192.168.251.165:554/live/camera";

        // 视频流参数 (硬编码)
        int video_width = 1920;
        int video_height = 1080;
        int video_bitrate = 10 * 1024 * 1024; // 10 Mbps
        int video_framerate = 30;
        AVCodecID video_codec_id = AV_CODEC_ID_H265; // 与 RK_VIDEO_ID_HEVC 对应

        // 音频流参数 (硬编码)
        int audio_sample_rate = 48000;
        int audio_channels = 1;
        int audio_bitrate = 64 * 1024; // 64 kbps
        AVCodecID audio_codec_id = AV_CODEC_ID_AAC;

        // 网络参数
        int rw_timeout = 3000000; // 网络超时时间 (微秒)
        int max_delay = 500000;   // 最大延迟 (微秒)
        bool enable_tcp = true;   // 是否强制使用TCP传输
    };

    class RTSPEngine
    {
    public:
        RTSPEngine();
        ~RTSPEngine();

        // 初始化推流引擎 (使用硬编码参数)
        int init(const RTSPConfig &config = RTSPConfig());

        // 启动/停止推流
        bool start();
        void stop();

        void workLoop();

        int pushAudioFrame(AVPacket *pkt);
        int pushVideoFrame(AVPacket *pkt);

        // 状态查询
        bool isInitialized() const { return initialized_; }
        bool isStreaming() const { return streaming_; }
        std::string getStreamUrl() const { return config_.output_url; }

    private:
        // 推流线程函数
        void streamingThread();

        // 初始化输出格式上下文
        bool initOutputContext();

        // 创建视频流
        bool createVideoStream();

        // 创建音频流
        bool createAudioStream();

        // 重连逻辑
        bool reconnect();

        // 清理资源
        void cleanup();

    private:
        RTSPConfig config_;
        std::atomic<bool> initialized_{false};
        std::atomic<bool> streaming_{false};
        std::atomic<bool> running_{false};

        // FFmpeg 输出上下文
        AVFormatContext *ofmt_ctx_{nullptr};
        AVStream *video_stream_{nullptr};
        AVStream *audio_stream_{nullptr};

        // 时间基
        AVRational video_time_base_;
        AVRational audio_time_base_;

        // 推流线程
        std::thread streaming_thread_;
    };
}
