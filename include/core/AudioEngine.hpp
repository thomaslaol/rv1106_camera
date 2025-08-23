#pragma once

#include <thread>
#include <memory>
#include <atomic>
#include "driver/AudioInputDriver.hpp"
#include "driver/AudioEncoderDriver.hpp"
#include "AudioStreamProcessor.hpp"

namespace core
{
    // 音频引擎配置（整合输入和编码配置）
    struct AudioEngineConfig
    {
        driver::AudioInputConfig input_config;   // 输入设备配置
        driver::AudioEncodeConfig encode_config; // 编码器配置
        AudioStreamConfig stream_config;         // 流处理器配置
    };

    class AudioEngine
    {
    public:
        AudioEngine();
        ~AudioEngine();

        // 初始化音频引擎（需传入完整配置）
        int init(const AudioEngineConfig &config);

        // 开始/停止采集
        void start();
        void stop();
        // 状态查询
        bool isRunning() const;

        // 获取流处理器（供上层模块获取处理后的数据）
        AudioStreamProcessor *getStreamProcessor()
        {
            return stream_processor_.get();
        }

        // 工作主循环
        void workerLoop();

    private:
        // 音频组件
        std::unique_ptr<driver::AudioInputDriver> input_driver_;
        std::unique_ptr<driver::AudioEncoderDriver> encoder_driver_;
        std::unique_ptr<AudioStreamProcessor> stream_processor_;

        bool initialized_;
        AudioEngineConfig config_;
        std::thread worker_thread_;
        std::atomic<bool> is_running_;
    };

} // namespace core
