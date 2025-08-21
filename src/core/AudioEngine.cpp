#include "core/AudioEngine.hpp"
#include <chrono>
#include <thread>
#include <memory>
#include <fstream>
extern "C"
{
#include "infra/logging/logger.h"
}

namespace core
{

    AudioEngine::AudioEngine()
        : is_running_(false)
    {
        // 初始化组件实例
        input_driver_ = std::make_unique<driver::AudioInputDriver>();
        encoder_driver_ = std::make_unique<driver::AudioEncoderDriver>();
        stream_processor_ = std::make_unique<AudioStreamProcessor>();
    }

    AudioEngine::~AudioEngine()
    {
        // 析构时确保停止所有流程
        stop();
    }

    int AudioEngine::init(const AudioEngineConfig &config)
    {
        // 保存配置
        config_ = config;

        // 1. 初始化音频输入设备
        int ret = input_driver_->init(config.input_config);
        if (ret != 0)
        {
            LOGE("Failed to initialize audio input driver");
            return ret;
        }

        // 2. 初始化音频编码器
        ret = encoder_driver_->init(config.encode_config);
        if (ret != 0)
        {
            LOGE("Failed to initialize audio encoder driver");
            return ret;
        }

        // 3. 初始化流处理器
        ret = stream_processor_->init(config.stream_config);
        if (ret != 0)
        {
            LOGE("Failed to initialize audio stream processor");
            return ret;
        }

        LOGI("Audio engine initialized successfully");
        return 0;
    }

    void AudioEngine::start()
    {
        if (is_running_)
        {
            LOGW("Audio engine is already running");
            return;
        }

        // 启动各个组件
        stream_processor_->start();
        is_running_ = true;

        // 启动工作线程
        worker_thread_ = std::thread(&AudioEngine::workerLoop, this);
        LOGI("Audio engine started");
    }

    void AudioEngine::stop()
    {
        if (!is_running_)
        {
            return;
        }

        // 停止工作线程
        is_running_ = false;
        if (worker_thread_.joinable())
        {
            worker_thread_.join();
        }

        // 停止各个组件
        stream_processor_->stop();
        stream_processor_->flush();
        input_driver_->close();
        encoder_driver_->close();

        LOGI("Audio engine stopped");
    }

#if 0
    void AudioEngine::workerLoop()
    {
        AVPacket input_pkt;   // 原始PCM数据包
        AVPacket encoded_pkt; // 编码后的数据包
        int ret = 0;

        while (is_running_)
        {
            // 1. 从输入设备读取PCM数据
            ret = input_driver_->readFrame(input_pkt);
            if (ret != 0)
            {
                if (ret != AVERROR_EOF)
                {
                    printf("Failed to read audio frame: %d\n", ret);
                    // 短暂休眠避免错误循环占用CPU
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                av_packet_unref(&input_pkt);
                continue;
            }

            // 2. 编码PCM数据
            ret = encoder_driver_->encode(input_pkt.data, input_pkt.size, encoded_pkt);
            if (ret == 0)
            {
                // 3. 将编码后的数据推送到流处理器
                stream_processor_->pushEncodedPacket(std::move(encoded_pkt));
            }
            else if (ret != AVERROR(EAGAIN))
            {
                // EAGAIN是正常需要更多数据的情况，其他错误需要记录
                printf("Audio encoding failed: %d\n", ret);
            }

            // 释放资源
            av_packet_unref(&input_pkt);
            av_packet_unref(&encoded_pkt);
        }

        // 退出前刷新编码器剩余数据
        while (encoder_driver_->flush(encoded_pkt) == 0)
        {
            stream_processor_->pushEncodedPacket(std::move(encoded_pkt));
            av_packet_unref(&encoded_pkt);
        }
    }
#endif

    void AudioEngine::workerLoop()
    {
        AVPacket input_pkt;   // 原始PCM数据包
        AVPacket encoded_pkt; // 编码后的数据包
        int ret = 0;

        // 1. 创建调试用的原始PCM文件
        std::ofstream raw_pcm("raw_input.pcm", std::ios::binary);
        // 2. 创建重采样后的PCM文件
        std::ofstream resampled_pcm("resampled_input.f32le", std::ios::binary);

        // 3. 创建未加ADTS头的AAC文件
        std::ofstream raw_aac("raw_audio.aac", std::ios::binary);

        while (is_running_)
        {
            // 1. 从输入设备读取PCM数据
            ret = input_driver_->readFrame(input_pkt);
            if (ret != 0)
            {
                if (ret != AVERROR_EOF)
                {
                    printf("Failed to read audio frame: %d\n", ret);
                    // 短暂休眠避免错误循环占用CPU
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                av_packet_unref(&input_pkt);
                continue;
            }

            // === 保存原始PCM数据 ===
            if (raw_pcm.is_open())
            {
                raw_pcm.write(reinterpret_cast<const char *>(input_pkt.data), input_pkt.size);
            }

            // 2. 编码PCM数据
            ret = encoder_driver_->encode(input_pkt.data, input_pkt.size, encoded_pkt);
            if (ret == 0)
            {
                // 3. 将编码后的数据推送到流处理器
                stream_processor_->pushEncodedPacket(std::move(encoded_pkt));
            }
            else if (ret != AVERROR(EAGAIN))
            {
                // EAGAIN是正常需要更多数据的情况，其他错误需要记录
                printf("Audio encoding failed: %d\n", ret);
            }

            // 释放资源
            av_packet_unref(&input_pkt);
            av_packet_unref(&encoded_pkt);
        }

        // 关闭调试文件
        raw_pcm.close();
        resampled_pcm.close();
        raw_aac.close();

        // 退出前刷新编码器剩余数据
        // while (encoder_driver_->flush(encoded_pkt) == 0)
        // {
        //     stream_processor_->pushEncodedPacket(std::move(encoded_pkt));
        //     av_packet_unref(&encoded_pkt);
        // }
    }

    // 简单复制左声道到右声道
    void AudioEngine::duplicateLeftToRight(AVPacket &pkt)
    {
        int16_t *samples = reinterpret_cast<int16_t *>(pkt.data);
        int sample_count = pkt.size / sizeof(int16_t);
        for (int i = 0; i < sample_count; i += 2)
        {
            samples[i + 1] = samples[i]; // 复制左声道到右声道
        }
    }

    void AudioEngine::monoToStereo(AVPacket &pkt)
    {
        // 核心：需要完全重建数据包
        const int orig_size = pkt.size;
        const int new_size = orig_size * 2; // 双声道数据量翻倍

        // 创建新缓冲区
        uint8_t *new_data = (uint8_t *)av_malloc(new_size);
        int16_t *new_samples = (int16_t *)new_data;
        int16_t *orig_samples = (int16_t *)pkt.data;

        // 重建交错数据：L -> [L, L]
        for (int i = 0; i < orig_size / 2; i++)
        {
            new_samples[2 * i] = orig_samples[i];     // 左声道
            new_samples[2 * i + 1] = orig_samples[i]; // 右声道
        }

        // 替换原始数据包（释放旧内存）
        av_packet_unref(&pkt);
        pkt.data = new_data;
        pkt.size = new_size;
    }

} // namespace core
