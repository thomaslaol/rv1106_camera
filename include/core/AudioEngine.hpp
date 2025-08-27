#pragma once

#include "core/AudioStreamProcessor.hpp"
#include "driver/AudioInputDriver.hpp"
#include "driver/AudioEncoderDriver.hpp"
#include <thread>
#include <memory>
#include <atomic>

namespace driver
{
    struct AudioInputConfig;
    struct AudioEncodeConfig;
    class AudioInputDriver;
    class AudioEncoderDriver;
}

namespace core
{
    struct AudioStreamConfig;
    class AudioStreamProcessor;

    // 音频引擎配置（整合输入和编码配置）
    struct AudioEngineConfig
    {
        driver::AudioInputConfig input_config;   // 输入设备配置
        driver::AudioEncodeConfig encode_config; // 编码器配置
        core::AudioStreamConfig stream_config;   // 输出设备配置
    };

    class AudioEngine
    {
    public:
        AudioEngine();
        ~AudioEngine();

        // 初始化音频引擎（需传入完整配置）
        int init();

        // 开始/停止采集
        void start();
        void stop();
        // 状态查询
        bool isRunning() const;

        bool getProcessedPacket(AVPacket &out_pkt)
        {
            return stream_processor_->getProcessedPacket(out_pkt);
        }


        // 工作主循环
        void workerLoop();

    private:
        // 音频组件
        std::unique_ptr<driver::AudioInputDriver> input_driver_;
        std::unique_ptr<driver::AudioEncoderDriver> encoder_driver_;
        std::unique_ptr<AudioStreamProcessor> stream_processor_;

        bool initialized_;
        std::thread audio_thread_;
        std::atomic<bool> is_running_;
    };

} // namespace core
