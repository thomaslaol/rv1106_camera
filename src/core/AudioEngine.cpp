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

    int AudioEngine::init(const AudioEngineConfig &config)
    {
        if (initialized_)
        {
            LOGW("Audio engine has already been initialized");
            return 0;
        }

        // 保存配置
        config_ = config;

        // 1. 初始化音频输入设备
        int ret = input_driver_->init(config.input_config);
        CHECK_RET(ret, "input_driver_->init");

        // 2. 初始化音频编码器
        ret = encoder_driver_->init(config.encode_config);
        CHECK_RET(ret, "encoder_driver_->init");

        // 3. 初始化流处理器
        ret = stream_processor_->init(config.stream_config);
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
        worker_thread_ = std::thread(&AudioEngine::workerLoop, this);
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
        if (worker_thread_.joinable())
            worker_thread_.join();

        stream_processor_->stop();
        stream_processor_->flush();

        LOGI("Audio engine stopped");
    }

    void AudioEngine::workerLoop()
    {
        AVPacket *input_pkt = av_packet_alloc();   // 原始PCM数据包
        AVPacket *encoded_pkt = av_packet_alloc(); // 编码后的数据包
        int ret = 0;

        // // 1. 创建调试用的原始PCM文件
        // std::ofstream raw_pcm("raw_input.pcm", std::ios::binary);
        // // 2. 创建重采样后的PCM文件
        // std::ofstream resampled_pcm("resampled_input.f32le", std::ios::binary);

        // // 3. 创建未加ADTS头的AAC文件
        // std::ofstream raw_aac("raw_audio.aac", std::ios::binary);

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

            // === 保存原始PCM数据 ===
            // if (raw_pcm.is_open())
            // {
            //     raw_pcm.write(reinterpret_cast<const char *>(input_pkt.data), input_pkt.size);
            // }

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
        }

        // // 关闭调试文件
        // raw_pcm.close();
        // resampled_pcm.close();
        // raw_aac.close();

        // 退出前刷新编码器剩余数据
        while (encoder_driver_->flush(*encoded_pkt) == 0)
        {
            stream_processor_->pushEncodedPacket(std::move(*encoded_pkt));
            av_packet_unref(encoded_pkt);
        }

        av_packet_free(&input_pkt); // 注意：av_packet_free 会先 unref 再释放指针
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
