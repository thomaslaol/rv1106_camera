#include "core/FFmpegStreamer.hpp"
#include <iostream>
#include <cstring>

extern "C"
{
#include "infra/logging/logger.h"
}

namespace core
{

    FFmpegStreamer::FFmpegStreamer(const Config &config) : config_(config)
    {
        // 注册所有FFmpeg组件
        avformat_network_init();
    }

    FFmpegStreamer::~FFmpegStreamer()
    {
        stop();
        avformat_network_deinit();
    }

    bool FFmpegStreamer::init()
    {
        // 初始化输出上下文
        if (!initOutputContext())
        {
            LOGE("Failed to initialize output context");
            return false;
        }

        // 初始化视频编码器
        if (!initVideoEncoder())
        {
            LOGE("Failed to initialize video encoder");
            return false;
        }

        // 初始化音频编码器
        if (!initAudioEncoder())
        {
            LOGE("Failed to initialize audio encoder");
            return false;
        }

        // 写入文件头
        int ret = avformat_write_header(output_ctx_, nullptr);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to write header: {}", error_buf);
            return false;
        }

        // 启动推流线程
        running_ = true;
        streaming_thread_ = std::thread(&FFmpegStreamer::streamingThread, this);

        LOGI("FFmpeg streamer initialized successfully");
        return true;
    }

    bool FFmpegStreamer::initOutputContext()
    {
        int ret = avformat_alloc_output_context2(&output_ctx_, nullptr, "flv", config_.output_url.c_str());
        if (!output_ctx_)
        {
            LOGE("Failed to allocate output context");
            return false;
        }

        // 打开输出URL
        if (!(output_ctx_->oformat->flags & AVFMT_NOFILE))
        {
            ret = avio_open(&output_ctx_->pb, config_.output_url.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0)
            {
                char error_buf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, error_buf, sizeof(error_buf));
                LOGE("Failed to open output URL: {}", error_buf);
                return false;
            }
        }

        return true;
    }

    bool FFmpegStreamer::initVideoEncoder()
    {
        // 根据配置选择视频编码器
        const char *codec_name = nullptr;
        switch (config_.video_codec)
        {
        case VideoCodec::H264:
            codec_name = "libx264";
            break;
        case VideoCodec::H265:
            codec_name = "libx265";
            break;
        default:
            LOGE("Unsupported video codec");
            return false;
        }

        // 查找编码器
        const AVCodec *codec = avcodec_find_encoder_by_name(codec_name);
        if (!codec)
        {
            LOGE("Failed to find video encoder: {}", codec_name);
            return false;
        }

        // 创建编码器上下文
        video_codec_ctx_ = avcodec_alloc_context3(codec);
        if (!video_codec_ctx_)
        {
            LOGE("Failed to allocate video codec context");
            return false;
        }

        // 配置编码器参数
        video_codec_ctx_->width = config_.video_width;
        video_codec_ctx_->height = config_.video_height;
        video_codec_ctx_->time_base = {1, config_.video_fps};
        video_codec_ctx_->framerate = {config_.video_fps, 1};
        video_codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        video_codec_ctx_->bit_rate = config_.video_bitrate;
        video_codec_ctx_->gop_size = config_.video_fps * 2; // 2秒一个GOP

        // 对于H.264/H.265，设置一些优化参数
        if (config_.video_codec == VideoCodec::H264 || config_.video_codec == VideoCodec::H265)
        {
            av_opt_set(video_codec_ctx_->priv_data, "preset", "fast", 0);
            av_opt_set(video_codec_ctx_->priv_data, "tune", "zerolatency", 0);
        }

        // 打开编码器
        int ret = avcodec_open2(video_codec_ctx_, codec, nullptr);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to open video codec: {}", error_buf);
            return false;
        }

        // 创建视频流
        AVStream *video_stream = avformat_new_stream(output_ctx_, codec);
        if (!video_stream)
        {
            LOGE("Failed to create video stream");
            return false;
        }

        video_stream->id = output_ctx_->nb_streams - 1;
        video_stream->time_base = video_codec_ctx_->time_base;
        video_time_base_ = video_stream->time_base;

        // 将编码器参数复制到流中
        ret = avcodec_parameters_from_context(video_stream->codecpar, video_codec_ctx_);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to copy video codec parameters: {}", error_buf);
            return false;
        }

        video_stream_index_ = video_stream->index;

        // 初始化图像转换上下文
        sws_ctx_ = sws_getContext(
            config_.video_width, config_.video_height, AV_PIX_FMT_NV12, // 假设输入是NV12格式
            config_.video_width, config_.video_height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!sws_ctx_)
        {
            LOGE("Failed to create SwsContext");
            return false;
        }

        return true;
    }

    bool FFmpegStreamer::initAudioEncoder()
    {
        // 根据配置选择音频编码器
        const char *codec_name = nullptr;
        switch (config_.audio_codec)
        {
        case AudioCodec::AAC:
            codec_name = "aac";
            break;
        case AudioCodec::OPUS:
            codec_name = "libopus";
            break;
        default:
            LOGE("Unsupported audio codec");
            return false;
        }

        // 查找编码器
        const AVCodec *codec = avcodec_find_encoder_by_name(codec_name);
        if (!codec)
        {
            LOGE("Failed to find audio encoder: {}", codec_name);
            return false;
        }

        // 创建编码器上下文
        audio_codec_ctx_ = avcodec_alloc_context3(codec);
        if (!audio_codec_ctx_)
        {
            LOGE("Failed to allocate audio codec context");
            return false;
        }

        // 配置编码器参数
        audio_codec_ctx_->sample_rate = config_.audio_sample_rate;
        audio_codec_ctx_->channels = config_.audio_channels;
        audio_codec_ctx_->channel_layout = av_get_default_channel_layout(config_.audio_channels);
        audio_codec_ctx_->bit_rate = config_.audio_bitrate;

        // 设置采样格式
        switch (config_.audio_format)
        {
        case AudioSampleFormat::S16LE:
            audio_codec_ctx_->sample_fmt = AV_SAMPLE_FMT_S16;
            break;
        case AudioSampleFormat::FLT:
            audio_codec_ctx_->sample_fmt = AV_SAMPLE_FMT_FLT;
            break;
        default:
            LOGE("Unsupported audio sample format");
            return false;
        }

        // 对于AAC编码器，需要特定的采样格式
        if (config_.audio_codec == AudioCodec::AAC && audio_codec_ctx_->sample_fmt != AV_SAMPLE_FMT_FLTP)
        {
            // 尝试使用FLTP格式
            if (codec->sample_fmts)
            {
                for (int i = 0; codec->sample_fmts[i] != -1; i++)
                {
                    if (codec->sample_fmts[i] == AV_SAMPLE_FMT_FLTP)
                    {
                        audio_codec_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
                        break;
                    }
                }
            }
        }

        // 打开编码器
        int ret = avcodec_open2(audio_codec_ctx_, codec, nullptr);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to open audio codec: {%s}", error_buf);
            return false;
        }

        // 创建音频流
        AVStream *audio_stream = avformat_new_stream(output_ctx_, codec);
        if (!audio_stream)
        {
            LOGE("Failed to create audio stream");
            return false;
        }

        audio_stream->id = output_ctx_->nb_streams - 1;
        audio_stream->time_base = {1, config_.audio_sample_rate};
        audio_time_base_ = audio_stream->time_base;

        // 将编码器参数复制到流中
        ret = avcodec_parameters_from_context(audio_stream->codecpar, audio_codec_ctx_);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to copy audio codec parameters: {}", error_buf);
            return false;
        }

        audio_stream_index_ = audio_stream->index;

        // 初始化音频重采样上下文
        swr_ctx_ = swr_alloc();
        if (!swr_ctx_)
        {
            LOGE("Failed to allocate SwrContext");
            return false;
        }

        // 设置重采样参数
        av_opt_set_int(swr_ctx_, "in_channel_count", config_.audio_channels, 0);
        av_opt_set_int(swr_ctx_, "out_channel_count", config_.audio_channels, 0);

        // 设置输入格式
        AVSampleFormat in_sample_fmt;
        switch (config_.audio_format)
        {
        case AudioSampleFormat::S16LE:
            in_sample_fmt = AV_SAMPLE_FMT_S16;
            break;
        case AudioSampleFormat::FLT:
            in_sample_fmt = AV_SAMPLE_FMT_FLT;
            break;
        default:
            in_sample_fmt = AV_SAMPLE_FMT_S16;
        }

        av_opt_set_sample_fmt(swr_ctx_, "in_sample_fmt", in_sample_fmt, 0);
        av_opt_set_sample_fmt(swr_ctx_, "out_sample_fmt", audio_codec_ctx_->sample_fmt, 0);
        av_opt_set_int(swr_ctx_, "in_sample_rate", config_.audio_sample_rate, 0);
        av_opt_set_int(swr_ctx_, "out_sample_rate", config_.audio_sample_rate, 0);

        // 初始化重采样上下文
        ret = swr_init(swr_ctx_);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to initialize SwrContext: {}", error_buf);
            return false;
        }

        return true;
    }

    bool FFmpegStreamer::pushVideoFrame(uint8_t *data, int width, int height, int64_t pts)
    {
        if (!running_ || !video_codec_ctx_)
        {
            return false;
        }

        // 创建AVFrame并设置参数
        AVFrame *frame = av_frame_alloc();
        if (!frame)
        {
            LOGE("Failed to allocate video frame");
            return false;
        }

        frame->width = width;
        frame->height = height;
        frame->format = AV_PIX_FMT_NV12; // 假设输入是NV12格式

        // 填充数据
        // av_image_fill_arrays(frame->data, frame->linesize, data,
        //                      AV_PIX_FMT_NV12, width, height, 1);

        // 直接设置数据指针（适用于NV12格式）
        frame->data[0] = data;                  // Y平面
        frame->data[1] = data + width * height; // UV平面
        frame->linesize[0] = width;             // Y平面行大小
        frame->linesize[1] = width;             // UV平面行大小

        // 设置时间戳
        frame->pts = pts;

        // 编码并发送帧
        bool result = encodeAndSendVideoFrame(frame);

        // 释放帧
        av_frame_free(&frame);

        return result;
    }

    bool FFmpegStreamer::pushAudioFrame(uint8_t *data, int samples, int64_t pts)
    {
        if (!running_ || !audio_codec_ctx_)
        {
            return false;
        }

        // 创建AVFrame并设置参数
        AVFrame *frame = av_frame_alloc();
        if (!frame)
        {
            LOGE("Failed to allocate audio frame");
            return false;
        }

        // 设置帧参数
        frame->nb_samples = samples;
        frame->format = (config_.audio_format == AudioSampleFormat::S16LE) ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLT;
        frame->channel_layout = av_get_default_channel_layout(config_.audio_channels);
        frame->sample_rate = config_.audio_sample_rate;

        // 分配缓冲区
        int ret = av_frame_get_buffer(frame, 0);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to allocate audio buffer: {}", error_buf);
            av_frame_free(&frame);
            return false;
        }

        // 复制数据
        memcpy(frame->data[0], data, samples * config_.audio_channels * ((config_.audio_format == AudioSampleFormat::S16LE) ? 2 : 4));

        // 设置时间戳
        frame->pts = pts;

        // 编码并发送帧
        bool result = encodeAndSendAudioFrame(frame);

        // 释放帧
        av_frame_free(&frame);

        return result;
    }

    bool FFmpegStreamer::pushEncodedVideoPacket(const AVPacket *pkt)
    {
        if (!running_ || !output_ctx_ || video_stream_index_ < 0)
        {
            return false;
        }

        // 创建包副本
        AVPacket *new_pkt = av_packet_alloc();
        if (!new_pkt)
        {
            LOGE("Failed to allocate packet");
            return false;
        }

        if (av_packet_ref(new_pkt, pkt) < 0)
        {
            LOGE("Failed to ref video packet");
            av_packet_free(&new_pkt);
            return false;
        }

        // 设置流索引
        new_pkt->stream_index = video_stream_index_;

        // 重新缩放时间戳
        av_packet_rescale_ts(new_pkt, video_time_base_, output_ctx_->streams[video_stream_index_]->time_base);

        // 写入包
        std::lock_guard<std::mutex> lock(mutex_);
        int ret = av_interleaved_write_frame(output_ctx_, new_pkt);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to write video packet: {}", error_buf);
            av_packet_free(&new_pkt);
            return false;
        }

        av_packet_free(&new_pkt);
        return true;
    }

    bool FFmpegStreamer::pushEncodedAudioPacket(const AVPacket *pkt)
    {
        if (!running_ || !output_ctx_ || audio_stream_index_ < 0)
        {
            return false;
        }

        // 创建包副本
        AVPacket *new_pkt = av_packet_alloc();
        if (!new_pkt)
        {
            LOGE("Failed to allocate packet");
            return false;
        }

        if (av_packet_ref(new_pkt, pkt) < 0)
        {
            LOGE("Failed to ref audio packet");
            av_packet_free(&new_pkt);
            return false;
        }

        // 设置流索引
        new_pkt->stream_index = audio_stream_index_;

        // 重新缩放时间戳
        av_packet_rescale_ts(new_pkt, audio_time_base_, output_ctx_->streams[audio_stream_index_]->time_base);

        // 写入包
        std::lock_guard<std::mutex> lock(mutex_);
        int ret = av_interleaved_write_frame(output_ctx_, new_pkt);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to write audio packet: {}", error_buf);
            av_packet_free(&new_pkt);
            return false;
        }

        av_packet_free(&new_pkt);
        return true;
    }

    bool FFmpegStreamer::encodeAndSendVideoFrame(AVFrame *frame)
    {
        // 如果输入格式与编码器要求的不一致，需要进行转换
        if (frame->format != video_codec_ctx_->pix_fmt)
        {
            AVFrame *converted_frame = av_frame_alloc();
            if (!converted_frame)
            {
                LOGE("Failed to allocate converted frame");
                return false;
            }

            converted_frame->width = frame->width;
            converted_frame->height = frame->height;
            converted_frame->format = video_codec_ctx_->pix_fmt;
            converted_frame->pts = frame->pts;

            // 分配缓冲区
            int ret = av_frame_get_buffer(converted_frame, 0);
            if (ret < 0)
            {
                char error_buf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, error_buf, sizeof(error_buf));
                LOGE("Failed to allocate converted frame buffer: {}", error_buf);
                av_frame_free(&converted_frame);
                return false;
            }

            // 转换图像格式
            sws_scale(sws_ctx_, frame->data, frame->linesize, 0, frame->height,
                      converted_frame->data, converted_frame->linesize);

            // 使用转换后的帧
            frame = converted_frame;
        }

        // 发送帧到编码器
        int ret = avcodec_send_frame(video_codec_ctx_, frame);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to send video frame to encoder: {}", error_buf);
            return false;
        }

        // 接收编码后的包
        AVPacket *pkt = av_packet_alloc();
        if (!pkt)
        {
            LOGE("Failed to allocate packet");
            return false;
        }

        while (ret >= 0)
        {
            ret = avcodec_receive_packet(video_codec_ctx_, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret < 0)
            {
                char error_buf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, error_buf, sizeof(error_buf));
                LOGE("Failed to receive video packet: {}", error_buf);
                av_packet_free(&pkt);
                return false;
            }

            // 设置包的时间基和流索引
            pkt->stream_index = video_stream_index_;
            av_packet_rescale_ts(pkt, video_codec_ctx_->time_base, output_ctx_->streams[video_stream_index_]->time_base);

            // 写入包
            std::lock_guard<std::mutex> lock(mutex_);
            ret = av_interleaved_write_frame(output_ctx_, pkt);
            if (ret < 0)
            {
                char error_buf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, error_buf, sizeof(error_buf));
                LOGE("Failed to write video packet: {}", error_buf);
                av_packet_free(&pkt);
                return false;
            }

            av_packet_unref(pkt);
        }

        av_packet_free(&pkt);
        return true;
    }

    bool FFmpegStreamer::encodeAndSendAudioFrame(AVFrame *frame)
    {
        // 如果输入格式与编码器要求的不一致，需要进行重采样
        if (frame->format != audio_codec_ctx_->sample_fmt)
        {
            AVFrame *resampled_frame = av_frame_alloc();
            if (!resampled_frame)
            {
                LOGE("Failed to allocate resampled frame");
                return false;
            }

            resampled_frame->nb_samples = frame->nb_samples;
            resampled_frame->format = audio_codec_ctx_->sample_fmt;
            resampled_frame->channel_layout = frame->channel_layout;
            resampled_frame->sample_rate = frame->sample_rate;
            resampled_frame->pts = frame->pts;

            // 分配缓冲区
            int ret = av_frame_get_buffer(resampled_frame, 0);
            if (ret < 0)
            {
                char error_buf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, error_buf, sizeof(error_buf));
                LOGE("Failed to allocate resampled frame buffer: {}", error_buf);
                av_frame_free(&resampled_frame);
                return false;
            }

            // 重采样
            ret = swr_convert_frame(swr_ctx_, resampled_frame, frame);
            if (ret < 0)
            {
                char error_buf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, error_buf, sizeof(error_buf));
                LOGE("Failed to resample audio frame: {}", error_buf);
                av_frame_free(&resampled_frame);
                return false;
            }

            // 使用重采样后的帧
            frame = resampled_frame;
        }

        // 发送帧到编码器
        int ret = avcodec_send_frame(audio_codec_ctx_, frame);
        if (ret < 0)
        {
            char error_buf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, error_buf, sizeof(error_buf));
            LOGE("Failed to send audio frame to encoder: {}", error_buf);
            return false;
        }

        // 接收编码后的包
        AVPacket *pkt = av_packet_alloc();
        if (!pkt)
        {
            LOGE("Failed to allocate packet");
            return false;
        }

        while (ret >= 0)
        {
            ret = avcodec_receive_packet(audio_codec_ctx_, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret < 0)
            {
                char error_buf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, error_buf, sizeof(error_buf));
                LOGE("Failed to receive audio packet: {}", error_buf);
                av_packet_free(&pkt);
                return false;
            }

            // 设置包的时间基和流索引
            pkt->stream_index = audio_stream_index_;
            av_packet_rescale_ts(pkt, audio_codec_ctx_->time_base, output_ctx_->streams[audio_stream_index_]->time_base);

            // 写入包
            std::lock_guard<std::mutex> lock(mutex_);
            ret = av_interleaved_write_frame(output_ctx_, pkt);
            if (ret < 0)
            {
                char error_buf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, error_buf, sizeof(error_buf));
                LOGE("Failed to write audio packet: {}", error_buf);
                av_packet_free(&pkt);
                return false;
            }

            av_packet_unref(pkt);
        }

        av_packet_free(&pkt);
        return true;
    }

    void FFmpegStreamer::streamingThread()
    {
        while (running_)
        {
            // 这里可以添加一些流控制逻辑
            // 例如检查连接状态、处理重连等

            // 短暂休眠以避免占用过多CPU
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void FFmpegStreamer::stop()
    {
        if (!running_)
        {
            return;
        }

        running_ = false;

        // 等待线程结束
        if (streaming_thread_.joinable())
        {
            streaming_thread_.join();
        }

        // 写入文件尾
        if (output_ctx_)
        {
            av_write_trailer(output_ctx_);
        }

        // 释放资源
        if (sws_ctx_)
        {
            sws_freeContext(sws_ctx_);
            sws_ctx_ = nullptr;
        }

        if (swr_ctx_)
        {
            swr_free(&swr_ctx_);
            swr_ctx_ = nullptr;
        }

        if (video_codec_ctx_)
        {
            avcodec_free_context(&video_codec_ctx_);
            video_codec_ctx_ = nullptr;
        }

        if (audio_codec_ctx_)
        {
            avcodec_free_context(&audio_codec_ctx_);
            audio_codec_ctx_ = nullptr;
        }

        if (output_ctx_ && !(output_ctx_->oformat->flags & AVFMT_NOFILE))
        {
            avio_closep(&output_ctx_->pb);
        }

        if (output_ctx_)
        {
            avformat_free_context(output_ctx_);
            output_ctx_ = nullptr;
        }

        LOGI("FFmpeg streamer stopped");
    }

} // namespace core