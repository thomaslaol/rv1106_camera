#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace core
{

    class FFmpegStreamer
    {
    public:
        // 视频编码器类型
        enum class VideoCodec
        {
            H264,
            H265
        };

        // 音频编码器类型
        enum class AudioCodec
        {
            AAC,
            OPUS
        };

        // 音频采样格式
        enum class AudioSampleFormat
        {
            S16LE, // 16位有符号整数，小端字节序
            FLT    // 32位浮点数
        };

        // 配置参数结构体
        struct Config
        {
            std::string output_url;         // 输出URL (rtmp://, udp://, etc.)
            VideoCodec video_codec;         // 视频编码器
            AudioCodec audio_codec;         // 音频编码器
            int video_bitrate;              // 视频比特率 (bps)
            int audio_bitrate;              // 音频比特率 (bps)
            int video_width;                // 视频宽度
            int video_height;               // 视频高度
            int video_fps;                  // 视频帧率
            int audio_sample_rate;          // 音频采样率
            int audio_channels;             // 音频声道数
            AudioSampleFormat audio_format; // 音频采样格式
        };

        FFmpegStreamer(const Config &config);
        ~FFmpegStreamer();

        // 初始化推流器
        bool init();

        // 推送视频帧
        bool pushVideoFrame(uint8_t *data, int width, int height, int64_t pts);

        // 推送音频帧
        bool pushAudioFrame(uint8_t *data, int samples, int64_t pts);

        // 推送编码后的视频包
        bool pushEncodedVideoPacket(const AVPacket *pkt);

        // 推送编码后的音频包
        bool pushEncodedAudioPacket(const AVPacket *pkt);

        // 停止推流
        void stop();

        // 检查是否正在运行
        bool isRunning() const { return running_; }

    private:
        // 初始化视频编码器
        bool initVideoEncoder();

        // 初始化音频编码器
        bool initAudioEncoder();

        // 初始化输出上下文
        bool initOutputContext();

        // 编码和发送视频帧
        bool encodeAndSendVideoFrame(AVFrame *frame);

        // 编码和发送音频帧
        bool encodeAndSendAudioFrame(AVFrame *frame);

        // 推流线程函数
        void streamingThread();

        Config config_;
        std::atomic<bool> running_{false};
        std::thread streaming_thread_;

        // FFmpeg 相关上下文
        AVFormatContext *output_ctx_{nullptr};
        AVCodecContext *video_codec_ctx_{nullptr};
        AVCodecContext *audio_codec_ctx_{nullptr};
        SwsContext *sws_ctx_{nullptr};
        SwrContext *swr_ctx_{nullptr};

        // 流索引
        int video_stream_index_{-1};
        int audio_stream_index_{-1};

        // 时间基
        AVRational video_time_base_;
        AVRational audio_time_base_;

        // 帧计数器
        int64_t video_frame_count_{0};
        int64_t audio_frame_count_{0};

        // 互斥锁
        std::mutex mutex_;
    };

} // namespace core