#include "core/AudioEngine.hpp"
#include "core/AudioStreamProcessor.hpp"
#include "driver/AudioInputDriver.hpp"
#include "driver/AudioEncoderDriver.hpp"
#include <chrono>
#include <memory>
#include <fstream>
extern "C"
{
#include "infra/logging/logger.h"
}

namespace core
{

    AudioEngine::AudioEngine() : is_running_(false), initialized_(false)
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

    int AudioEngine::init()
    {
        if (initialized_)
        {
            LOGW("Audio engine has already been initialized");
            return 0;
        }

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
            audia_config.encode_config.bit_rate = 32000;
            audia_config.encode_config.sample_rate = 48000;
            audia_config.encode_config.channels = 1;
        }
        // 流处理器配置
        {
            audia_config.stream_config.add_adts_header = true;
            audia_config.stream_config.buffer_size = 30;
        }

        // 1. 初始化音频输入设备
        int ret = input_driver_->init(audia_config.input_config);
        CHECK_RET(ret, "input_driver_->init");

        // 2. 初始化音频编码器
        ret = encoder_driver_->init(audia_config.encode_config);
        CHECK_RET(ret, "encoder_driver_->init");

        // 3. 初始化流处理器
        ret = stream_processor_->init(audia_config.stream_config);
        CHECK_RET(ret, "stream_processor_->init");

        initialized_ = true;
        LOGI("Audio engine initialized successfully");
        return 0;
    }

    void AudioEngine::start()
    {
        if (!initialized_)
        {
            LOGW("Audio engine has not been initialized");
            return;
        }

        // 启动音频流处理器
        stream_processor_->start();
        is_running_ = true;

        // 启动工作线程
        audio_thread_ = std::thread(&AudioEngine::workerLoop, this);
    }

    void AudioEngine::stop()
    {
        if (!initialized_)
            return;

        initialized_ = false;
        input_driver_->close();
        encoder_driver_->close();

        if (!is_running_)
            return;

        is_running_ = false;
        if (audio_thread_.joinable())
            audio_thread_.join();

        stream_processor_->stop();
        stream_processor_->flush();

        LOGI("Audio engine stopped");
    }

    void AudioEngine::workerLoop()
    {
        AVPacket *input_pkt = av_packet_alloc();   // 原始PCM数据包
        AVPacket *encoded_pkt = av_packet_alloc(); // 编码后的数据包
        int ret = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));

        while (is_running_)
        {
            // 1. 从输入设备读取PCM数据
            ret = input_driver_->readFrame(*input_pkt);
            if (ret != 0)
            {
                if (ret != AVERROR_EOF)
                {
                    printf("Failed to read audio frame: %d\n", ret);
                    // 短暂休眠避免错误循环占用CPU
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                av_packet_unref(input_pkt);
                continue;
            }

            // 2. 编码PCM数据
            ret = encoder_driver_->encode(input_pkt->data, input_pkt->size, *encoded_pkt);
            if (ret == 0)
            {
                // 3. 将编码后的数据推送到流处理器
                stream_processor_->pushEncodedPacket(std::move(*encoded_pkt));
            }
            else if (ret != AVERROR(EAGAIN))
            {
                // EAGAIN是正常需要更多数据的情况，其他错误需要记录
                printf("Audio encoding failed: %d\n", ret);
                av_packet_unref(encoded_pkt);
            }

            // 释放资源
            av_packet_unref(input_pkt);

            // 频率
            // uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            //                    std::chrono::steady_clock::now().time_since_epoch())
            //                    .count();
            // static uint64_t audio_start_time_ = 0;
            // static uint64_t audio_frameCount = 0;
            // if (audio_start_time_ == 0)
            //     audio_start_time_ = now;
            // audio_frameCount++;

            // 每1秒计算一次
            // uint64_t elapsed = now - audio_start_time_;
            // if (elapsed >= 1000000)
            // {
            //     float audio_fps = (audio_frameCount * 1000000.0) / elapsed;
            //     audio_start_time_ = now;
            //     printf("音频频率: %.2f\n", audio_fps);
            //     audio_frameCount = 0;
            // }

            // std::this_thread::sleep_for(std::chrono::microseconds(800));
            // std::this_thread::sleep_for(std::chrono::microseconds(10));
        }

        av_packet_free(&input_pkt);
        av_packet_free(&encoded_pkt);
    }

    /*
    void AudioEngine::workerLoop()
    {
        AVPacket input_pkt;   // 原始PCM数据包
        AVPacket encoded_pkt; // 编码后的数据包
        int ret = 0;

        // // 1. 创建调试用的原始PCM文件
        // std::ofstream raw_pcm("raw_input.pcm", std::ios::binary);
        // // 2. 创建重采样后的PCM文件
        // std::ofstream resampled_pcm("resampled_input.f32le", std::ios::binary);

        // // 3. 创建未加ADTS头的AAC文件
        // std::ofstream raw_aac("raw_audio.aac", std::ios::binary);

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
        // if (raw_pcm.is_open())
        // {
        //     raw_pcm.write(reinterpret_cast<const char *>(input_pkt.data), input_pkt.size);
        // }

        // 2. 编码PCM数据
        ret = encoder_driver_->encode(input_pkt.data, input_pkt.size, encoded_pkt);
        if (ret == 0)
        {
            // 3. 将编码后的数据推送到音频流处理器
            stream_processor_->pushEncodedPacket(std::move(encoded_pkt));
        }
        else if (ret != AVERROR(EAGAIN))
        {
            // EAGAIN是正常需要更多数据的情况，其他错误需要记录
            printf("Audio encoding failed: %d\n", ret);
            av_packet_unref(&encoded_pkt);
        }

        // 释放资源
        av_packet_unref(&input_pkt);

        // 关闭调试文件
        // raw_pcm.close();
        // resampled_pcm.close();
        // raw_aac.close();
        // 退出前刷新编码器剩余数据
        while (encoder_driver_->flush(encoded_pkt) == 0)
        {
            stream_processor_->pushEncodedPacket(std::move(encoded_pkt));
        }
    }
*/
} // namespace core
