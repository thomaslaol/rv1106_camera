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

        // 启动音频流程（采集->编码->处理）
        void start();

        // 停止音频流程
        void stop();

        // 获取流处理器（供上层模块获取处理后的数据）
        AudioStreamProcessor *getStreamProcessor()
        {
            return stream_processor_.get();
        }

        // 音频引擎状态
        bool isRunning() const { return is_running_; }

    private:
        // 工作线程主循环
        void workerLoop();
        void duplicateLeftToRight(AVPacket &pkt);
        void monoToStereo(AVPacket &pkt);

        // 音频组件
        std::unique_ptr<driver::AudioInputDriver> input_driver_;
        std::unique_ptr<driver::AudioEncoderDriver> encoder_driver_;
        std::unique_ptr<AudioStreamProcessor> stream_processor_;

        // 线程与状态控制
        std::thread worker_thread_;
        std::atomic<bool> is_running_;
        AudioEngineConfig config_;
    };

} // namespace core
