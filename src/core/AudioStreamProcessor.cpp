#include "core/AudioStreamProcessor.hpp"
#include <chrono>
#include <cstring>
extern "C"
{
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <libavutil/buffer.h>
#include "infra/logging/logger.h"
}

namespace core
{

    AudioStreamProcessor::AudioStreamProcessor()
        : last_pts_(0),
          buffer_size_(DEFAULT_BUFFER_SIZE),
          is_running_(false),
          output_file_(nullptr)
    {
    }

    AudioStreamProcessor::~AudioStreamProcessor()
    {
        stop();
        flush();
        closeOutputFile();
        // 确保队列空
        while (!packet_queue_.empty())
        {
            av_packet_unref(&packet_queue_.front());
            packet_queue_.pop();
        }
    }

    int AudioStreamProcessor::init(AudioStreamConfig &config)
    {

        config_ = config;
        buffer_size_ = config_.buffer_size > 0 ? config_.buffer_size : DEFAULT_BUFFER_SIZE;
        return 0;
    }

    void AudioStreamProcessor::start()
    {
        if (!is_running_)
        {
            is_running_ = true;
            LOGI("Audio stream processor started");
        }
    }

    void AudioStreamProcessor::stop()
    {

        if (is_running_)
        {
            is_running_ = false;
            LOGI("Audio stream processor stopped");
        }
    }

    void AudioStreamProcessor::pushEncodedPacket(AVPacket &&pkt)
    {
        if (!is_running_)
        {
            LOGW("Processor not running, dropping packet");
            av_packet_unref(&pkt);
            return;
        }

        // 添加ADTS头
        // if (config_.add_adts_header)
        // {
        //     addADTSHeader(pkt);
        // }

        // 对队列操作加锁
        std::lock_guard<std::mutex> lock(queue_mutex_);

        // 限制缓冲区大小，防止内存溢出
        if (packet_queue_.size() >= buffer_size_)
        {
            // 从队列中取出但不移动
            AVPacket *front = &packet_queue_.front();
            // 直接解引用并弹出
            av_packet_unref(front);
            packet_queue_.pop();
            printf("音频队列 已满，丢弃最早的数据包: %zu/%zu\n",
                   packet_queue_.size(), buffer_size_);
        }

        // 在队列中创建新的AVPacket
        packet_queue_.emplace();
        AVPacket *new_pkt = &packet_queue_.back();

        // printf("时间戳,pkt.pts = %lld\n", pkt.pts);

        // 复制packet内容
        if (av_packet_ref(new_pkt, &pkt) < 0)
        {
            printf("Failed to ref audio packet");
            packet_queue_.pop();
            av_packet_unref(&pkt);
            return;
        }

        // 原packet安全解引用
        av_packet_unref(&pkt);
        queue_cv_.notify_one();
    }

    // 出队操作添加锁
    bool AudioStreamProcessor::getProcessedPacket(AVPacket &out_pkt, int timeout_ms)
    {
        // 使用unique_lock配合条件变量
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 等待队列非空或处理器停止运行，最多等待timeout_ms
        bool has_data = queue_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                           [this]()
                                           { return !is_running_ || !packet_queue_.empty(); });

        // 处理退出情况
        if (!is_running_)
        {
            return false; // 处理器停止，返回失败
        }

        // 处理超时情况
        if (!has_data)
        {
            return false; // 超时且无数据
        }

        // 正常取数据
        // printf("音频队列: %zu/%zu\n",packet_queue_.size(), buffer_size_);
        AVPacket *front = &packet_queue_.front();
        if (av_packet_ref(&out_pkt, front) < 0)
        {
            printf("Failed to ref output packet");
            return false;
        }

        av_packet_unref(front);
        packet_queue_.pop();

        return true;
    }

    // 获取队列首元素的时间戳
    bool AudioStreamProcessor::getQueueFrontPts(int64_t &pts, int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        // 等待队列非空或处理器停止运行，最多等待timeout_ms
        bool has_data = queue_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                           [this]()
                                           { return !is_running_ || !packet_queue_.empty(); });

        // 处理退出情况
        if (!is_running_)
        {
            return false; // 处理器停止，返回失败
        }

        // 处理超时情况
        if (!has_data)
        {
            return false; // 超时且无数据
        }

        pts = packet_queue_.front().pts;
        // printf("音频队列首时间戳：%lld\n", pts);
        return true;
    }

    void AudioStreamProcessor::flush()
    {
        while (!packet_queue_.empty())
        {
            // 直接获取队首元素的引用，释放资源
            AVPacket &front_pkt = packet_queue_.front();
            av_packet_unref(&front_pkt);

            // 删除队首元素
            packet_queue_.pop();
        }

        last_pts_ = 0;
        LOGI("Audio stream buffer flushed");
    }

    // 打开输出文件
    int AudioStreamProcessor::setOutputFile(const std::string &file_path)
    {

        if (output_file_)
        {
            fclose(output_file_);
            output_file_ = nullptr;
        }

        // 以二进制写入模式打开
        output_file_ = fopen(file_path.c_str(), "wb");
        if (!output_file_)
        {
            LOGE("Failed to open output file: %s", file_path.c_str());
            return -1;
        }

        LOGI("Output file opened: %s", file_path.c_str());
        return 0;
    }

    // 关闭输出文件
    void AudioStreamProcessor::closeOutputFile()
    {

        if (output_file_)
        {
            fflush(output_file_); // 确保所有数据写入磁盘
            fclose(output_file_);
            output_file_ = nullptr;
            LOGI("Audio file saved");
        }
    }

    void AudioStreamProcessor::addADTSHeader(AVPacket &pkt)
    {
        const int ADTS_HEADER_SIZE = 7;
        // ... [缓冲区管理部分代码保持不变] ...

        // 计算ADTS头字段 - 修正后的逻辑
        uint8_t *adts = pkt.data;
        int sample_rate_index = getSampleRateIndex(config_.sample_rate);
        int channel_config = config_.channels;

        // 重要修正：使用正确的profile值（AAC-LC = 1）
        int profile = 1; // AAC-LC profile

        // 计算帧长度（包括ADTS头）
        uint16_t frame_length = ADTS_HEADER_SIZE + pkt.size; // 使用原始数据大小计算

        // 构建ADTS头 - 符合标准格式
        adts[0] = 0xFF; // 同步字高8位
        adts[1] = 0xF1; // 同步字低4位 + ID(0) + layer(00) + protection_absent(1)

        // 第二字节：profile(2位) + sample_rate_index(4位) + private_bit(1位) + channel_config高1位
        adts[2] = (profile << 6) | (sample_rate_index << 2) | (channel_config >> 2);

        // 第三字节：channel_config低2位 + original/copy + home + copyright_id_bit + copyright_id_start
        adts[3] = ((channel_config & 0x3) << 6) | (frame_length >> 11);

        // 第四字节：frame_length的中间8位
        adts[4] = (frame_length >> 3) & 0xFF;

        // 第五字节：frame_length低3位（高位） + buffer fullness高5位
        adts[5] = ((frame_length & 0x7) << 5) | 0x1F; // 0x1F = buffer fullness高5位

        // 第六字节：buffer fullness低6位 + number_of_raw_data_blocks
        adts[6] = 0xFC; // buffer full + 0 raw data blocks

        // 更新数据包大小
        pkt.size = frame_length; // 确认大小已更新
    }

    int AudioStreamProcessor::getSampleRateIndex(int sample_rate)
    {
        // 采样率索引表（ADTS标准定义）
        const int sample_rates[] = {
            96000, 88200, 64000, 48000, 44100, 32000,
            24000, 22050, 16000, 12000, 11025, 8000};

        for (int i = 0; i < sizeof(sample_rates) / sizeof(sample_rates[0]); i++)
        {
            if (sample_rates[i] == sample_rate)
            {
                return i;
            }
        }

        LOGW("Unsupported sample rate: %d, using 44100", sample_rate);
        return 4; // 44100Hz的索引
    }

} // namespace core
