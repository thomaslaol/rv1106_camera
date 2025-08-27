#pragma once

#include <string>
#include <memory>
#include <mutex>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

namespace driver
{
    // 音频输入设备配置参数
    struct AudioInputConfig
    {
        std::string device_name;    // 设备名称，如"hw:0"（ALSA）或"default"
        int sample_rate = 48000;    // 采样率，默认48000Hz
        int channels = 2;           // 声道数，默认立体声
        std::string format = "s16"; // 采样格式，默认16位有符号整数
        int buffer_size = 4096;     // 缓冲区大小
    };

    class AudioInputDriver
    {
    public:
        AudioInputDriver();
        ~AudioInputDriver();

        // 禁止拷贝构造和赋值，避免资源管理问题
        AudioInputDriver(const AudioInputDriver &) = delete;
        AudioInputDriver &operator=(const AudioInputDriver &) = delete;

        // 初始化音频设备
        // 返回值：0表示成功，非0表示失败（负值为FFmpeg错误码）
        int init(driver::AudioInputConfig &config);

        // 读取一帧音频数据（PCM格式）
        // pkt: 输出参数，存储读取到的音频数据
        // 返回值：0表示成功，AVERROR_EOF表示结束，其他值表示错误
        int readFrame(AVPacket &pkt);

        // 关闭音频设备并释放资源
        void close();

        // 获取当前设备状态
        bool isInitialized() const { return m_isInitialized; }

        // 获取音频流信息（初始化后有效）
        AVStream *getStream() const { return m_formatCtx ? m_formatCtx->streams[0] : nullptr; }

    private:
        // 初始化设备参数
        int setupOptions(const AudioInputConfig &config);

        // 内部状态变量
        AVFormatContext *m_formatCtx = nullptr; // FFmpeg格式上下文
        AVDictionary *m_options = nullptr;      // 设备参数选项
        AudioInputConfig *m_config;             // 配置参数
        bool m_isInitialized = false;           // 初始化状态标志
        mutable std::mutex m_mutex;             // 线程安全锁
    };

} // namespace drover
