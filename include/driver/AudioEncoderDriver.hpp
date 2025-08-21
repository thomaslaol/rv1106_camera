#pragma once
#include <mutex>
#include <string>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace driver
{
    /**
     * 音频编码器配置参数
     */
    struct AudioEncodeConfig
    {
        int sample_rate = 48000;                       // 采样率（Hz）
        int channels = 2;                              // 声道数（1=单声道，2=立体声）
        int bit_rate = 64000;                          // 比特率（bps，64000=64kbps）
        std::string codec_name = "libfdk_aac";         // 编码器名称（默认使用高质量的libfdk_aac）
        AVSampleFormat sample_fmt = AV_SAMPLE_FMT_S16; // 采样格式（与输入设备一致）
    };

    /**
     * 音频编码器驱动类
     * 负责初始化编码器、将PCM数据编码为压缩格式（如AAC）
     */
    class AudioEncoderDriver
    {
    public:
        AudioEncoderDriver();
        ~AudioEncoderDriver();

        // 禁止拷贝构造和赋值（避免资源重复释放）
        AudioEncoderDriver(const AudioEncoderDriver &) = delete;
        AudioEncoderDriver &operator=(const AudioEncoderDriver &) = delete;

        /**
         * 初始化编码器
         * @param config 编码配置参数
         * @return 0=成功，非0=失败
         */
        int init(const AudioEncodeConfig &config);

        /**
         * 编码PCM数据
         * @param pcm_data 原始PCM数据指针
         * @param data_size PCM数据大小（字节）
         * @param out_pkt 输出的编码后数据包（需调用者手动释放av_packet_unref）
         * @return 0=成功，非0=失败（AVERROR(EAGAIN)表示需要更多输入）
         */
        int encode(const uint8_t *pcm_data, int data_size, AVPacket &out_pkt);

        /**
         * 刷新编码器（处理剩余缓存数据）
         * @param out_pkt 输出的编码后数据包
         * @return 0=成功，非0=失败（AVERROR_EOF表示无更多数据）
         */
        int flush(AVPacket &out_pkt);

        /**
         * 关闭编码器，释放资源
         */
        void close();

        /**
         * 获取编码器上下文（用于高级操作）
         * @return 编码器上下文指针， nullptr=未初始化
         */
        AVCodecContext *getCodecContext() const { return codec_ctx_; }

    private:
        uint64_t getAudioTimestampUs();
        std::mutex mutex_;                    // 线程安全锁
        AudioEncodeConfig config_;            // 编码配置
        AVCodec *codec_ = nullptr;            // 编码器
        AVCodecContext *codec_ctx_ = nullptr; // 编码器上下文
        AVFrame *frame_ = nullptr;            // 用于存放待编码的PCM帧
        bool is_initialized_ = false;         // 初始化状态标记
        uint64_t start_us_ = 0;

        /**
         * 初始化编码帧（分配缓冲区等）
         * @return 0=成功，非0=失败
         */
        int initFrame();
    };

} // namespace driver
