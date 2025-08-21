// #ifndef AUDIO_STREAM_PROCESSOR_HPP
// #define AUDIO_STREAM_PROCESSOR_HPP

// #include <queue>
// #include <mutex>
// #include <condition_variable>
// extern "C"
// {
// #include <libavcodec/avcodec.h>
// }

// namespace core
// {
//     // 音频流处理配置
//     struct AudioStreamConfig
//     {
//         int sample_rate = 44100;     // 采样率
//         int channels = 2;            // 声道数
//         bool add_adts_header = true; // 是否为AAC添加ADTS头
//         size_t buffer_size = 30;     // 缓冲区大小（帧数）
//     };

//     class AudioStreamProcessor
//     {
//     public:
//         static constexpr size_t DEFAULT_BUFFER_SIZE = 30; // 默认缓冲区大小
//         static constexpr int ADTS_HEADER_SIZE = 7;        // AAC ADTS头大小
//         static constexpr int MAX_PACKET_SIZE = 1024 * 10; // 最大数据包大小

//         AudioStreamProcessor();
//         ~AudioStreamProcessor();

//         // 初始化处理器
//         int init(const AudioStreamConfig &config);

//         // 启动/停止处理器
//         void start();
//         void stop();

//         // 推送编码后的数据包到缓冲区
//         void pushEncodedPacket(AVPacket &&pkt);

//         // 获取处理后的数据包（供传输模块使用）
//         bool getProcessedPacket(AVPacket &out_pkt);

//         // 清空缓冲区
//         void flush();

//         // 设置输出文件（用于保存AAC）
//         int setOutputFile(const std::string &file_path);
//         // 关闭输出文件
//         void closeOutputFile();

//     private:
//         // 为AAC数据包添加ADTS头（用于裸流传输）
//         void addADTSHeader(AVPacket &pkt);

//         // 将采样率转换为ADTS标准索引
//         int getSampleRateIndex(int sample_rate);

//         AudioStreamConfig config_;          // 配置参数
//         std::queue<AVPacket> packet_queue_; // 数据包缓冲区
//         std::mutex mutex_;                  // 线程安全锁
//         int64_t last_pts_;                  // 上一帧时间戳（用于同步）
//         size_t buffer_size_;                // 缓冲区大小限制
//         bool is_running_;                   // 处理器运行状态

//         FILE *output_file_ = nullptr;
//     };

// } // namespace core

// #endif // AUDIO_STREAM_PROCESSOR_HPP

#ifndef AUDIO_STREAM_PROCESSOR_HPP
#define AUDIO_STREAM_PROCESSOR_HPP

#include <queue>
#include <mutex>
#include <cstdio>
extern "C"
{
#include <libavcodec/avcodec.h>
}

namespace core
{

    struct AudioStreamConfig
    {
        bool add_adts_header = true; // 是否添加ADTS头
        int sample_rate = 44100;     // 采样率
        int channels = 2;            // 声道数
        size_t buffer_size = 30;     // 缓冲区大小
    };

    class AudioStreamProcessor
    {
    public:
        AudioStreamProcessor();
        ~AudioStreamProcessor();

        // 初始化处理器
        int init(const AudioStreamConfig &config);

        // 启动/停止处理器
        void start();
        void stop();

        // 推送编码后的数据包到队列
        void pushEncodedPacket(AVPacket &&pkt);

        // 获取处理后的数据包
        bool getProcessedPacket(AVPacket &out_pkt);

        // 清空缓冲区
        void flush();

        // 文件操作
        int setOutputFile(const std::string &file_path);
        void closeOutputFile();

    private:
        // 添加ADTS头（AAC格式需要）
        void addADTSHeader(AVPacket &pkt);

        // 获取采样率索引（ADTS标准）
        int getSampleRateIndex(int sample_rate);

        static constexpr size_t DEFAULT_BUFFER_SIZE = 30; // 默认缓冲区大小
        static constexpr int ADTS_HEADER_SIZE = 7;        // ADTS头大小（字节）
        static constexpr int MAX_PACKET_SIZE = 2048;      // 最大包大小（字节）

        AudioStreamConfig config_;          // 配置参数
        std::queue<AVPacket> packet_queue_; // 数据包队列
        std::mutex mutex_;                  // 互斥锁
        size_t buffer_size_;                // 缓冲区大小
        bool is_running_;                   // 运行状态
        int64_t last_pts_;                  // 上一个时间戳
        FILE *output_file_ = nullptr;       // 输出文件句柄
    };

} // namespace core

#endif // AUDIO_STREAM_PROCESSOR_HPP
