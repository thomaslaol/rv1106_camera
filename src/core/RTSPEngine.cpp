#include "core/RTSPEngine.hpp"

extern "C"
{
#include "infra/logging/logger.h"
}

namespace core
{
    RTSPEngine::RTSPEngine()
    {
        // 初始化FFmpeg网络
        int ret = avformat_network_init();
        if (ret < 0)
        {
            char errbuf[1024] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOGE("Failed to initialize network: %d (%s)", ret, errbuf);
            return;
        }
        LOGI("Network initialized successfully");
    }

    RTSPEngine::~RTSPEngine()
    {
        printf("~RTSPEngine()\n");
        // stop();
        cleanup();
    }

    int RTSPEngine::init(const RTSPConfig &config)
    {
        if (initialized_)
        {
            LOGW("RTSPEngine already initialized");
            return 0;
        }

        config_ = config;

        // 1. 创建输出格式上下文
        int ret = avformat_alloc_output_context2(&ofmt_ctx_, nullptr, "rtsp", config_.output_url.c_str());
        if (ret < 0 || !ofmt_ctx_)
        {
            LOGE("Failed to allocate output context: %d", ret);
            cleanup();
            return -1;
        }

        // 2. 创建视频流
        if (!createVideoStream())
        {
            LOGE("Failed to create video stream");
            cleanup();
            return -1;
        }

        // 3. 创建音频流
        if (!createAudioStream())
        {
            LOGE("Failed to create audio stream");
            cleanup();
            return -1;
        }

        // 4. 设置输出格式选项
        AVDictionary *opts = nullptr;
        // 设置超时选项
        // av_dict_set(&opts, "rw_timeout", std::to_string(config_.rw_timeout).c_str(), 0);
        // av_dict_set(&opts, "stimeout", std::to_string(config_.rw_timeout).c_str(), 0);
        // av_dict_set(&opts, "max_delay", std::to_string(config_.max_delay).c_str(), 0);

        // 强制使用 TCP 传输（更可靠）
        if (config_.enable_tcp)
        {
            av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        }
        else
        {
            av_dict_set(&opts, "rtsp_transport", "udp", 0); // udp
        }

        // 5. 写文件头 - 这会自动打开连接
        ret = avformat_write_header(ofmt_ctx_, &opts);
        av_dict_free(&opts); // 释放选项字典

        if (ret < 0)
        {
            char errbuf[1024] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            LOGE("Failed to write stream header: %d (%s)", ret, errbuf);
            cleanup();
            return -1;
        }

        // 6. 打印输出信息 (调试用)
        // av_dump_format(ofmt_ctx_, 0, config_.output_url.c_str(), 1);

        initialized_ = true;
        LOGI("RTSPEngine initialized successfully. Stream URL: %s", config_.output_url.c_str());

        // 输出配置信息
        LOGI("Video: %dx%d, %dfps, %dKbps, %s",
             config_.video_width, config_.video_height, config_.video_framerate,
             config_.video_bitrate / 1024, avcodec_get_name(config_.video_codec_id));

        LOGI("Audio: %dHz, %d channels, %dKbps, %s",
             config_.audio_sample_rate, config_.audio_channels,
             config_.audio_bitrate / 1024, avcodec_get_name(config_.audio_codec_id));

        return 0;
    }

    // int RTSPEngine::pushVideoData(uint8_t *data, int data_size, int64_t pts, int64_t dts)
    // {
    //     if (!initialized_)
    //     {
    //         LOGE("RTSPEngine not initialized");
    //         return -1;
    //     }

    //     // 1. 分配并初始化AVPacket
    //     AVPacket *pkt = av_packet_alloc();
    //     if (!pkt)
    //     {
    //         LOGE("Failed to alloc AVPacket");
    //         return -1;
    //     }

    //     // 2. 填充原始数据（注意：AVPacket的数据需要用av_packet_from_data管理，避免内存泄漏）
    //     int ret = av_packet_from_data(pkt, data, data_size);
    //     if (ret < 0)
    //     {
    //         LOGE("Failed to init AVPacket from data: %d", ret);
    //         av_packet_free(&pkt);
    //         return -1;
    //     }

    //     // 3. 填充元信息（关键步骤）
    //     pkt->stream_index = video_stream_->index; // 关联到视频流
    //     pkt->pts = pts;                           // 显示时间戳（需按视频流的time_base转换）
    //     pkt->dts = dts;                           // 解码时间戳
    //     pkt->duration = 0;                        // 帧持续时间（可选，根据实际情况设置）

    //     // 4. 若pts/dts的时间基与视频流不一致，需要转换
    //     av_packet_rescale_ts(pkt, video_time_base_, video_stream_->time_base);

    //     // 5. 写入数据包
    //     ret = av_interleaved_write_frame(ofmt_ctx_, pkt);
    //     if (ret < 0)
    //     {
    //         LOGE("Failed to write video frame: %d", ret);
    //         av_packet_free(&pkt);
    //         return -1;
    //     }

    //     // 6. 释放AVPacket（注意：av_packet_free会自动释放其内部data，若data是外部管理的，需提前处理）
    //     av_packet_free(&pkt);
    //     return 0;
    // }

    int RTSPEngine::pushVideoFrame(AVPacket *pkt)
    {
        if (!initialized_)
        {
            LOGE("RTSPEngine not initialized");
            return -1;
        }

        if (video_stream_->index < 0)
        {
            printf("video_stream_ index 无效！可能未正确创建流\n");
            return -1;
        }

        pkt->stream_index = video_stream_->index;
        // printf("视频video_stream_->index = %d\n",video_stream_->index);
        av_packet_rescale_ts(pkt, (AVRational){1, 1000000}, (AVRational){1, 90000});

        // 写入数据包
        // printf("pkt->pts = %lld\n",pkt->pts);
        int ret = av_interleaved_write_frame(ofmt_ctx_, pkt);
        if (ret == 0)
        {
            // printf("成功发送帧：PTS=%lld, 大小=%d, 关键帧=%d\n",
            //        pkt->pts, pkt->size, (pkt->flags & AV_PKT_FLAG_KEY) ? 1 : 0);
        }
        else
        {
            char errbuf[512] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            printf("发送失败：%d，错误原因：%s\n", ret, errbuf);
            return -1;
        }
        return 0;
    }

    int RTSPEngine::pushAudioFrame(AVPacket *pkt)
    {
        if (!initialized_)
        {
            printf("[ERROR]RTSPEngine not initialized");
            return -1;
        }

        // 设置时间戳
        pkt->stream_index = audio_stream_->index;
        // printf("音频audio_stream_->index = %d\n",audio_stream_->index);
        av_packet_rescale_ts(pkt, audio_time_base_, audio_stream_->time_base);

        // 写入数据包
        int ret = av_interleaved_write_frame(ofmt_ctx_, pkt);
        if (ret < 0)
        {
            printf("[ERROR] Failed to write audio frame: %d", ret);
            return -1;
        }
        return 0;
    }

    bool RTSPEngine::createVideoStream()
    {
        video_stream_ = avformat_new_stream(ofmt_ctx_, nullptr);
        if (!video_stream_)
        {
            LOGE("Failed to create video stream");
            return false;
        }

        // 设置视频流参数
        video_stream_->id = ofmt_ctx_->nb_streams - 1;
        video_stream_->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        video_stream_->codecpar->codec_id = config_.video_codec_id;
        video_stream_->codecpar->width = config_.video_width;
        video_stream_->codecpar->height = config_.video_height;
        video_stream_->codecpar->format = AV_PIX_FMT_YUV420P;
        video_stream_->codecpar->bit_rate = config_.video_bitrate;

        // 设置时间基
        video_time_base_ = (AVRational){1, 90000};
        video_stream_->time_base = video_time_base_;

        // 对于H264/H265，可能需要设置profile和level
        if (config_.video_codec_id == AV_CODEC_ID_H264)
        {
            video_stream_->codecpar->profile = FF_PROFILE_H264_HIGH;
            video_stream_->codecpar->level = 40; // Level 4.0
        }
        else if (config_.video_codec_id == AV_CODEC_ID_H265)
        {
            video_stream_->codecpar->profile = FF_PROFILE_HEVC_MAIN;
        }

        return true;
    }

    bool RTSPEngine::createAudioStream()
    {
        audio_stream_ = avformat_new_stream(ofmt_ctx_, nullptr);
        if (!audio_stream_)
        {
            LOGE("Failed to create audio stream");
            return false;
        }

        // 设置音频流参数
        audio_stream_->id = ofmt_ctx_->nb_streams - 1;
        audio_stream_->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        audio_stream_->codecpar->codec_id = config_.audio_codec_id;
        audio_stream_->codecpar->sample_rate = config_.audio_sample_rate;
        audio_stream_->codecpar->channels = config_.audio_channels;
        audio_stream_->codecpar->channel_layout =
            av_get_default_channel_layout(config_.audio_channels);
        audio_stream_->codecpar->bit_rate = config_.audio_bitrate;

        // 设置音频格式
        if (config_.audio_codec_id == AV_CODEC_ID_AAC)
        {
            audio_stream_->codecpar->format = AV_SAMPLE_FMT_FLTP;
            audio_stream_->codecpar->frame_size = 1024; // AAC帧大小

            // 添加全局头信息（AudioSpecificConfig）
            // 创建2字节的extradata
            uint8_t asc[2];
            // AAC-LC profile (2), 采样率索引 (4=44.1kHz, 3=48kHz), 声道配置
            asc[0] = 0x12; // 二进制：00010 010 -> profile=2 (AAC-LC), 采样率索引=4 (44.1kHz)
            asc[1] = 0x10; // 二进制：000 1000 -> 声道配置=1 (单声道)

            // 如果是48kHz单声道：
            if (config_.audio_sample_rate == 48000)
            {
                asc[0] = 0x13; // 二进制：00010 011 -> profile=2, 采样率索引=3 (48kHz)
            }

            // 分配并设置extradata
            audio_stream_->codecpar->extradata = (uint8_t *)av_malloc(2);
            if (!audio_stream_->codecpar->extradata)
            {
                LOGE("Failed to allocate extradata for AAC");
                return false;
            }
            memcpy(audio_stream_->codecpar->extradata, asc, 2);
            audio_stream_->codecpar->extradata_size = 2;
        }
        else
        {
            audio_stream_->codecpar->format = AV_SAMPLE_FMT_S16;
        }

        // 设置时间基
        audio_time_base_ = av_make_q(1, config_.audio_sample_rate);
        audio_stream_->time_base = audio_time_base_;

        return true;
    }

    void RTSPEngine::cleanup()
    {
        if (ofmt_ctx_)
        {
            if (initialized_)
            {
                av_write_trailer(ofmt_ctx_);
            }

            if (ofmt_ctx_->pb)
            {
                avio_close(ofmt_ctx_->pb);
            }

            avformat_free_context(ofmt_ctx_);
            ofmt_ctx_ = nullptr;
        }

        video_stream_ = nullptr;
        audio_stream_ = nullptr;
        initialized_ = false;
        streaming_ = false;

        avformat_network_deinit();

        LOGD("RTSPEngine resources cleaned up");
    }

    void RTSPEngine::workLoop()
    {
        //     while (!g_quit_flag)
        //     {
        //         int ret = 0;
        //         // // 推音频
        //         // if (audio_engine_->getProcessedPacket(audio_out_pkt,20))
        //         // {
        //         //     rtsps_engine_->pushAudioFrame(&audio_out_pkt);
        //         //     av_packet_unref(&audio_out_pkt);
        //         // }
        //         // else
        //         // {
        //         //     printf("test111,\n");
        //         // }
        //         // std::this_thread::sleep_for(std::chrono::microseconds(15000));

        //         // 推视频
        //         // int v_ret = 0;
        //         // v_ret = video_engine_->popEncodedPacket(video_out_pkt);
        //         // if (v_ret == 0)
        //         // {
        //         //     rtsps_engine_->pushVideoFrame(video_out_pkt);
        //         //     av_packet_unref(video_out_pkt);
        //         //     // printf("已退流\n");
        //         // }
        //         // else
        //         // {
        //         //     // printf("test111,ret = %d\n", v_ret);
        //         //     std::this_thread::sleep_for(std::chrono::microseconds(100));
        //         // }
        //         // std::this_thread::sleep_for(std::chrono::microseconds(10000));

        //         // 音视频同步退流
        //         bool v_ret = video_engine_->getQueueFrontPts(vedio_pts, 20);
        //         bool a_ret = audio_engine_->getQueueFrontPts(audio_pts, 20);

        //         if (v_ret && a_ret)
        //         {
        //             if (audio_pts != 0)
        //             {
        //                 audio_pts = audio_pts * 1000000 / 48000;
        //             }
        //             if (audio_pts <= vedio_pts)
        //             {
        //                 // std::cout << " 音频audio_pts = " << audio_pts << ",   video_pts = " << vedio_pts << std::endl;
        //                 audio_engine_->getProcessedPacket(audio_out_pkt);
        //                 rtsps_engine_->pushAudioFrame(&audio_out_pkt);
        //             }
        //             else
        //             {
        //                 // std::cout << " 视频video_pts = " << vedio_pts << ",   音频audio_pts = " << audio_pts << std::endl;
        //                 video_engine_->popEncodedPacket(video_out_pkt);
        //                 rtsps_engine_->pushVideoFrame(video_out_pkt);
        //             }
        //         }
        //         else if (v_ret && !a_ret)
        //         {
        //             // std::cout << " 视频video_pts = " << vedio_pts << ",   音频audio_pts = " << audio_pts << std::endl;
        //             video_engine_->popEncodedPacket(video_out_pkt);
        //             rtsps_engine_->pushVideoFrame(video_out_pkt);
        //         }
        //         else if (!v_ret && a_ret)
        //         {
        //             // std::cout << " 音频audio_pts = " << audio_pts << ",   video_pts = " << vedio_pts << std::endl;
        //             audio_engine_->getProcessedPacket(audio_out_pkt);
        //             rtsps_engine_->pushAudioFrame(&audio_out_pkt);
        //         }
        //         std::this_thread::sleep_for(std::chrono::microseconds(10));
        //     }
    }

} // namespace core
