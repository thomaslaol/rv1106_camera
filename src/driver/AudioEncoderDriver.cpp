#include "driver/AudioEncoderDriver.hpp"
#include <iostream>
#include <vector>
extern "C"
{
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
#include "infra/logging/logger.h"
#include "infra/time/TimeUtils.h"
}

namespace driver
{

    AudioEncoderDriver::AudioEncoderDriver() {}

    AudioEncoderDriver::~AudioEncoderDriver()
    {
        close(); // 析构时自动释放资源
    }

    int AudioEncoderDriver::init(AudioEncodeConfig &config)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 已初始化则先关闭
        if (is_initialized_)
        {
            close();
        }

        // 保存配置
        config_ = config;

        // 查找编码器（优先按名称查找，如"libfdk_aac"）
        codec_ = avcodec_find_encoder_by_name(config.codec_name.c_str());
        if (!codec_)
        {
            LOGE("Failed to find encoder '%s'. Check if FFmpeg is compiled with this codec.",
                 config.codec_name.c_str());
            // 尝试 fallback：按编码ID查找（如AAC）
            codec_ = avcodec_find_encoder(AV_CODEC_ID_AAC);
            if (!codec_)
            {
                LOGE("No AAC encoder found in FFmpeg.");
                return -1;
            }
            LOGW("Using default AAC encoder instead of '%s'", config.codec_name.c_str());
        }

        // 分配编码器上下文
        codec_ctx_ = avcodec_alloc_context3(codec_);
        if (!codec_ctx_)
        {
            LOGE("Failed to allocate codec context.");
            return -1;
        }

        // 设置编码器参数
        codec_ctx_->bit_rate = config.bit_rate;                                                    // 比特率
        codec_ctx_->sample_rate = config.sample_rate;                                              // 采样率（必须与输入一致）
        codec_ctx_->channels = config.channels;                                                    // 声道数
        codec_ctx_->channel_layout = av_get_default_channel_layout(config.channels);               // 声道布局
        codec_ctx_->sample_fmt = codec_->sample_fmts ? codec_->sample_fmts[0] : config.sample_fmt; // 采样格式
        codec_ctx_->codec_id = codec_->id;                                                         // 编码器ID
        codec_ctx_->codec_type = AVMEDIA_TYPE_AUDIO;                                               // 媒体类型：音频

        // 对于某些编码器，需要设置extradata（如AAC的ADTS头）
        if (codec_ctx_->codec_id == AV_CODEC_ID_AAC)
        {
            codec_ctx_->profile = FF_PROFILE_AAC_LOW; // AAC-LC 低复杂度 profile（通用选择）
        }

        // 打开编码器
        if (avcodec_open2(codec_ctx_, codec_, nullptr) < 0)
        {
            LOGE("Failed to open audio encoder.");
            avcodec_free_context(&codec_ctx_);
            codec_ = nullptr;
            return -1;
        }

        // 初始化待编码帧
        if (initFrame() != 0)
        {
            LOGE("Failed to initialize audio frame.");
            close();
            return -1;
        }

        start_us_ = infra::now_us(); // 获取当前时间戳

        is_initialized_ = true;
        LOGI("Audio encoder initialized successfully. Codec: %s, Sample rate: %d, Bitrate: %d",
             codec_->name, config.sample_rate, config.bit_rate);
        return 0;
    }

    int AudioEncoderDriver::initFrame()
    {
        // 分配帧结构
        frame_ = av_frame_alloc();
        if (!frame_)
        {
            LOGE("Failed to allocate AVFrame.");
            return -1;
        }

        // 设置帧参数（必须与编码器一致）
        frame_->format = codec_ctx_->sample_fmt;
        frame_->nb_samples = codec_ctx_->frame_size; // 每帧样本数（由编码器决定）
        frame_->channel_layout = codec_ctx_->channel_layout;

        // 为帧分配数据缓冲区
        if (av_frame_get_buffer(frame_, 0) < 0)
        {
            LOGE("Failed to allocate buffer for AVFrame.");
            av_frame_free(&frame_);
            frame_ = nullptr;
            return -1;
        }

        return 0;
    }

    int AudioEncoderDriver::encode(const uint8_t *pcm_data, int data_size, AVPacket &out_pkt)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 检查初始化状态
        if (!is_initialized_ || !codec_ctx_ || !frame_)
        {
            LOGW("Audio encoder not initialized. Call init() first.");
            return -1;
        }

        // 重置输出数据包
        av_packet_unref(&out_pkt);

        // 计算每帧需要的字节数
        int bytes_per_sample = av_get_bytes_per_sample(codec_ctx_->sample_fmt);
        int bytes_per_frame = frame_->nb_samples * codec_ctx_->channels * bytes_per_sample;

        // === 关键修复1：添加帧缓冲区 ===
        static std::vector<uint8_t> frame_buffer;
        frame_buffer.insert(frame_buffer.end(), pcm_data, pcm_data + data_size);

        // 检查是否有足够数据组成完整帧
        if (frame_buffer.size() < bytes_per_frame)
        {
            return AVERROR(EAGAIN); // 需要更多数据
        }

        // === 关键修复2：使用基于采样率的PTS ===
        static int64_t sample_count = 0;

        // 填充帧
        memcpy(frame_->data[0], frame_buffer.data(), bytes_per_frame);

        // 设置帧时间戳（基于采样点）
        frame_->pts = sample_count;
        sample_count += frame_->nb_samples;

        // 移除已使用的数据
        if (frame_buffer.size() > bytes_per_frame)
        {
            memmove(frame_buffer.data(),
                    frame_buffer.data() + bytes_per_frame,
                    frame_buffer.size() - bytes_per_frame);
        }
        frame_buffer.resize(frame_buffer.size() - bytes_per_frame);

        // 发送帧到编码器
        int ret = avcodec_send_frame(codec_ctx_, frame_);
        if (ret < 0)
        {
            char errbuf[1024];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOGE("Failed to send frame to encoder: %s", errbuf);
            return ret;
        }

        // 接收编码后的数据包
        ret = avcodec_receive_packet(codec_ctx_, &out_pkt);
        if (ret == AVERROR(EAGAIN))
        {
            // 需要更多输入数据（正常情况）
            return ret;
        }
        else if (ret < 0)
        {
            char errbuf[1024];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOGE("Failed to receive packet from encoder: %s", errbuf);
            return ret;
        }

        // === 关键修复3：正确设置时间戳 ===
        // 转换时间戳到编码器的时间基
        out_pkt.pts = av_rescale_q(frame_->pts,
                                   (AVRational){1, codec_ctx_->sample_rate},
                                   codec_ctx_->time_base);
        out_pkt.dts = out_pkt.pts;

        // printf("Encoded audio packet - Size: %d bytes, PTS: %" PRId64 "\n",
        //        out_pkt.size, out_pkt.pts);
        return 0;
    }

    int AudioEncoderDriver::flush(AVPacket &out_pkt)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!is_initialized_ || !codec_ctx_)
        {
            return -1;
        }

        // 发送NULL帧触发编码器刷新
        int ret = avcodec_send_frame(codec_ctx_, nullptr);
        if (ret < 0)
        {
            return ret;
        }

        // 接收剩余的编码数据
        ret = avcodec_receive_packet(codec_ctx_, &out_pkt);
        return ret;
    }

    void AudioEncoderDriver::close()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (frame_)
        {
            av_frame_free(&frame_);
            frame_ = nullptr;
        }

        if (codec_ctx_)
        {
            avcodec_close(codec_ctx_);
            avcodec_free_context(&codec_ctx_);
            codec_ctx_ = nullptr;
        }

        codec_ = nullptr;
        is_initialized_ = false;
        LOGI("Audio encoder closed.");
    }
    // 获取当前时间（微秒级，单调递增
    uint64_t AudioEncoderDriver::getAudioTimestampUs()
    {
        // steady_clock 是 C++ 标准的单调时钟，等价于 C 的 CLOCK_MONOTONIC
        auto now = std::chrono::steady_clock::now();
        // 转换为自时钟起点以来的微秒数
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch());
        return static_cast<uint64_t>(us.count());
    }

} // namespace driver
