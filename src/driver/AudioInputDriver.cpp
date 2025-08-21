#include "driver/AudioInputDriver.hpp"
#include <iostream>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include "infra/logging/logger.h"
}

namespace driver
{

    AudioInputDriver::AudioInputDriver()
    {
        // 注册所有输入设备（确保只注册一次，可考虑移至全局初始化）
        avdevice_register_all();
    }

    AudioInputDriver::~AudioInputDriver()
    {
        // 析构时自动关闭设备，确保资源释放
        close();
    }

    int AudioInputDriver::init(const AudioInputConfig &config)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 已初始化则先关闭
        if (m_isInitialized)
        {
            close();
        }

        // 保存配置
        m_config = config;

        // 初始化设备参数
        int ret = setupOptions(config);
        if (ret != 0)
        {
            LOGE("Failed to setup audio device options: %d", ret);
            return ret;
        }

        // 查找输入格式（ALSA为Linux默认音频输入框架）
        AVInputFormat *inputFormat = av_find_input_format("alsa");
        if (!inputFormat)
        {
            LOGE("ALSA input format not found. Check FFmpeg configuration.");
            return -1;
        }

        // 打开音频设备
        ret = avformat_open_input(&m_formatCtx, config.device_name.c_str(), inputFormat, &m_options);
        if (ret != 0)
        {
            char errbuf[1024];
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOGE("Failed to open audio device %s: %s (error code: %d)",
                 config.device_name.c_str(), errbuf, ret);
            av_dict_free(&m_options);
            return ret;
        }

        // 检查是否包含音频流
        if (m_formatCtx->nb_streams == 0)
        {
            LOGE("No audio streams found in device %s", config.device_name.c_str());
            avformat_close_input(&m_formatCtx);
            av_dict_free(&m_options);
            return -1;
        }

        // 标记为已初始化
        m_isInitialized = true;
        LOGI("Successfully initialized audio input device: %s", config.device_name.c_str());
        LOGI("Audio configuration - Sample rate: %d, Channels: %d, Format: %s",
             config.sample_rate, config.channels, config.format.c_str());

        return 0;
    }

    int AudioInputDriver::readFrame(AVPacket &pkt)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 检查初始化状态
        if (!m_isInitialized || !m_formatCtx)
        {
            LOGW("Audio device not initialized. Call init() first.");
            return -1;
        }

        // 重置数据包
        av_packet_unref(&pkt);

        // 从设备读取一帧数据
        int ret = av_read_frame(m_formatCtx, &pkt);
        if (ret != 0)
        {
            if (ret != AVERROR_EOF)
            {
                char errbuf[1024];
                av_strerror(ret, errbuf, sizeof(errbuf));
                LOGE("Failed to read audio frame: %s (error code: %d)", errbuf, ret);
            }
            return ret;
        }

        // 验证是否为音频流（虽然理论上应该只有音频流）
        if (pkt.stream_index < 0 || pkt.stream_index >= m_formatCtx->nb_streams)
        {
            LOGW("Invalid stream index in audio packet: %d", pkt.stream_index);
            av_packet_unref(&pkt);
            return -1;
        }

        return 0;
    }

    void AudioInputDriver::close()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_formatCtx)
        {
            avformat_close_input(&m_formatCtx);
            m_formatCtx = nullptr;
        }

        if (m_options)
        {
            av_dict_free(&m_options);
            m_options = nullptr;
        }

        m_isInitialized = false;
        LOGI("Audio input device closed");
    }

    int AudioInputDriver::setupOptions(const AudioInputConfig &config)
    {
        // 设置设备参数（键值对形式）
        // 采样率
        if (av_dict_set(&m_options, "sample_rate", std::to_string(config.sample_rate).c_str(), 0) < 0)
        {
            LOGE("Failed to set sample_rate");
            return -1;
        }

        // 声道数
        if (av_dict_set(&m_options, "channels", std::to_string(config.channels).c_str(), 0) < 0)
        {
            LOGE("Failed to set channels");
            av_dict_free(&m_options);
            return -1;
        }

        // 采样格式
        if (av_dict_set(&m_options, "format", config.format.c_str(), 0) < 0)
        {
            LOGE("Failed to set format");
            av_dict_free(&m_options);
            return -1;
        }

        // 缓冲区大小（可选参数）
        if (config.buffer_size > 0)
        {
            if (av_dict_set(&m_options, "buffer_size", std::to_string(config.buffer_size).c_str(), 0) < 0)
            {
                LOGW("Failed to set buffer_size, using default");
            }
        }

        return 0;
    }

} // namespace driver