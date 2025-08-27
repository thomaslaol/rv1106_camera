#include "core/VideoStreamProcessor.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono> // 用于FPS统计

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "core/RTSPStreamer.hpp"
#include "core/VPSSManager.hpp"

#include "core/RTSPEngine.hpp"
#include "infra/time/TimeUtils.h"

extern "C"
{
#include "infra/logging/logger.h"
#include "rk_mpi_mb.h"
#include "rk_comm_vpss.h"
#include "rk_mpi_vpss.h"
}

namespace core
{
    VideoStreamProcessor::VideoStreamProcessor(driver::VideoInputDriver *vi_driver,
                                               driver::VideoEncoderDriver *venc_driver,
                                               core::VPSSManager *vpss_manager)
        : vi_driver_(vi_driver), venc_driver_(venc_driver), vpss_manager_(vpss_manager)
    {
        is_inited_ = false;

        // 初始化编码流缓冲区
        memset(&venc_stream_, 0, sizeof(VENC_STREAM_S));

        // 初始化BGR帧缓冲
        m_bgrFrame = cv::Mat(cv::Size(width, height), CV_8UC3);
        LOGI("VideoStreamProcessor initialized (%dx%d)", width, height);

        // 初始化视频帧信息
        memset(&vi_frame, 0, sizeof(VIDEO_FRAME_INFO_S));

        // rtsps_engine_ = new RTSPEngine();
    }

    // 析构函数：释放资源+停止循环
    VideoStreamProcessor::~VideoStreamProcessor()
    {
        releaseStreamBuffer(); // 释放malloc的VENC_PACK_S

        releasePool(); // 释放YUV内存池

        queue_cv_.notify_all();
        // std::lock_guard<std::mutex> lock(queue_mutex_);
        // while (!packet_queue_.empty())
        // {
        //     av_packet_free(&packet_queue_.front());
        //     packet_queue_.pop();
        // }
    }

    // 初始化编码流缓冲区
    int VideoStreamProcessor::initStreamBuffer()
    {
        venc_stream_.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
        if (venc_stream_.pstPack == nullptr)
        {
            printf("VideoStreamProcessor::initStreamBuffer - malloc failed!\n");
            return -1;
        }
        memset(venc_stream_.pstPack, 0, sizeof(VENC_PACK_S));
        return 0;
    }

    // 释放编码流缓冲区
    void VideoStreamProcessor::releaseStreamBuffer()
    {
        if (venc_stream_.pstPack != nullptr)
        {
            free(venc_stream_.pstPack);
            venc_stream_.pstPack = nullptr;
        }
    }

    // 初始化YUV内存池（大小为width*height*3，保留5个缓冲块）
    int VideoStreamProcessor::initPool()
    {
        MB_POOL_CONFIG_S pool_cfg;
        memset(&pool_cfg, 0, sizeof(pool_cfg));
        pool_cfg.u64MBSize = width * height * 3;  // YUV420SP内存大小
        pool_cfg.u32MBCnt = 5;                    // 5个缓冲块，避免帧处理阻塞
        pool_cfg.enAllocType = MB_ALLOC_TYPE_DMA; // 使用DMA内存，支持硬件编码
        m_mb_pool = RK_MPI_MB_CreatePool(&pool_cfg);
        if (m_mb_pool == MB_INVALID_POOLID)
        {
            LOGE("Failed to create YUV memory pool");
            return -1;
        }
        return 0;
    }

    // 释放内存池缓冲区
    void VideoStreamProcessor::releasePool()
    {
        if (m_mb_pool != MB_INVALID_POOLID)
        {
            RK_MPI_MB_DestroyPool(m_mb_pool);
            m_mb_pool = MB_INVALID_POOLID;
        }
    }

    // 启动业务循环
    int VideoStreamProcessor::init()
    {
        if (is_inited_)
        {
            LOGE("VideoStreamProcessor::startProcess - already running!");
            return 0;
        }

        // 初始化编码流缓冲区
        if (initStreamBuffer() != 0)
        {
            LOGE("VideoStreamProcessor::initStreamBuffer - failed!");
            return -1;
        }

        // 初始化YUV内存池
        if (initPool() != 0)
        {
            LOGE("VideoStreamProcessor::initPool - failed!");
            return -1;
        }

        // core::RTSPConfig rtsp_config;
        // // 推流url
        // {
        //     rtsp_config.output_url = "rtsp://192.168.251.165/live/camera";
        // }
        // // 视频流参数 (硬编码)
        // {
        //     rtsp_config.video_width = 1920;
        //     rtsp_config.video_height = 1080;
        //     rtsp_config.video_bitrate = 10 * 1024 * 1000; // 10 Mbps
        //     rtsp_config.video_framerate = 30;
        //     rtsp_config.video_codec_id = AV_CODEC_ID_H265; // 与 RK_VIDEO_ID_HEVC 对应}
        // }
        // // 音频流参数 (硬编码)
        // {
        //     rtsp_config.audio_sample_rate = 48000;
        //     rtsp_config.audio_channels = 1;
        //     rtsp_config.audio_bitrate = 64 * 1024; // 64 kbps
        //     rtsp_config.audio_codec_id = AV_CODEC_ID_AAC;
        // }
        // // 网络参数
        // {
        //     rtsp_config.rw_timeout = 3000000; // 网络超时时间 (微秒)
        //     rtsp_config.max_delay = 500000;   // 最大延迟 (微秒)
        //     rtsp_config.enable_tcp = true;    // 是否强制使用TCP传输
        // }
        // int ret = rtsps_engine_->init(rtsp_config);
        // CHECK_RET(ret, "rtsps_engine_->init");

        // 初始化FPS统计时间
        start_time_ = 0;

        is_inited_ = true;
        return 0;
    }

    int VideoStreamProcessor::start()
    {
        if (!is_inited_)
        {
            LOGE("VideoStreamProcessor::run - not initialized!");
            return -1;
        }

        int ret = vi_driver_->start();
        ret |= venc_driver_->start();

        is_running_ = true;
        return ret;
    }

    void VideoStreamProcessor::stop()
    {
        if (!is_inited_)
        {
            LOGE("VideoStreamProcessor::stopProcess - not initialized!");
            return;
        }

        is_inited_ = false;
        is_running_ = false;
        vi_driver_->stop();
        venc_driver_->stop();
    }

    int VideoStreamProcessor::loopProcess()
    {
        int ret = 0;
        if (getFromVIAndsendToVPSS() != 0)
        {
            printf("getFromVIAndsendToVPSS失败！ret=%d\n", ret);
            return -1;
        }

        VIDEO_FRAME_INFO_S bgr_frame;
        if (getFromVPSSAndProcessWithOpenCV(bgr_frame) != 0)
        {
            printf("getFromVPSSAndProcessWithOpenCV失败！ret=%d\n", ret);
            return -1;
        }

        if (sendToVENCAndGetEncodedPacket(bgr_frame) != 0)
        {
            printf("Failed to send frame to encoder\n");
            return -1;
        }

        pushEncodedPacketToQueue();
        releaseStreamAndFrame();

        return 0;
    }

    int VideoStreamProcessor::getFromVIAndsendToVPSS()
    {
        VIDEO_FRAME_INFO_S vi_frame;
        int ret = vi_driver_->getFrame(vi_frame, -1);
        if (ret != RK_SUCCESS)
        {
            printf("VI获取帧失败！ret=%d\n", ret);
            return -1;
        }
        // 2. 发送VI帧到VPSS进行硬件格式转换
        ret = RK_MPI_VPSS_SendFrame(0, 0, &vi_frame, -1);
        if (ret != RK_SUCCESS)
        {
            printf("VPSS发送帧失败！ret=%d\n", ret);
            vi_driver_->releaseFrame(vi_frame); // 释放VI帧
            return -1;
        }
        vi_driver_->releaseFrame(vi_frame);
        return 0;
    }

    int VideoStreamProcessor::getFromVPSSAndProcessWithOpenCV(VIDEO_FRAME_INFO_S &bgr_frame)
    {
        // 从VPSS获取转换后的BGR帧
        // VIDEO_FRAME_INFO_S bgr_frame;
        int ret = RK_MPI_VPSS_GetChnFrame(0, 0, &bgr_frame, 1000);
        if (ret != RK_SUCCESS)
        {
            printf("VPSS获取BGR帧失败！ret=%d\n", ret);
            return -1;
        }

        // 4. FPS计算与时间戳处理
        uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();

        if (start_time_ == 0)
            start_time_ = now;
        m_frameCount++;

        // 每1秒计算一次FPS
        uint64_t elapsed = now - start_time_;
        if (elapsed >= 1000000)
        {
            m_fps = (m_frameCount * 1000000.0) / elapsed;
            start_time_ = now;
            printf("FPS: %.2f\n", m_fps);

            // 更新时间字符串
            // time_t t = time(NULL);
            // struct tm *p = localtime(&t);
            // char timeStr[20];
            // strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", p);
            snprintf(m_fpsText, sizeof(m_fpsText), "%.2f fps", m_fps);

            m_frameCount = 0;
        }

        // 5. 绘制FPS文本（基于VPSS输出的BGR帧，无需中间Mat）
        cv::Mat bgr_mat(
            bgr_frame.stVFrame.u32Height,
            bgr_frame.stVFrame.u32Width,
            CV_8UC3,
            RK_MPI_MB_Handle2VirAddr(bgr_frame.stVFrame.pMbBlk));
        cv::putText(bgr_mat, m_fpsText, cv::Point(40, 40),
                    cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

        // 6. 准备编码帧
        static uint64_t start_time_pts = 0;
        if (start_time_pts == 0)
            start_time_pts = now;
        // process_frame = bgr_frame;                            // 拷贝帧信息（浅拷贝，共享内存块）
        bgr_frame.stVFrame.enPixelFormat = RK_FMT_RGB888; // 匹配编码器格式（需与VPSS输出兼容）
        bgr_frame.stVFrame.u64PTS = now - start_time_pts; // 更新时间戳

        return 0;
    }

    int VideoStreamProcessor::sendToVENCAndGetEncodedPacket(VIDEO_FRAME_INFO_S &process_frame)
    {
        int ret = 0;
        // 发送到编码器
        if (venc_driver_->sendFrame(process_frame) != 0)
        {
            printf("Failed to send frame to encoder\n");
            RK_MPI_VPSS_ReleaseGrpFrame(0, 0, &process_frame); 
            return -1;
        }

        // 从编码器获取编码流
        ret = venc_driver_->getStream(venc_stream_, -1);
        if (ret != RK_SUCCESS)
        {
            printf("VideoStreamProcessor::run - get VENC stream failed! ret=%d\n", ret);
            return -1;
        }

        // void *streamData = RK_MPI_MB_Handle2VirAddr(encode_frame.pstPack->pMbBlk);

        // 打印帧信息
        // printf("获取视频数据且编码成功 当前帧数据长度==%d 当前帧时间戳==%lld\r\n",
        //        venc_stream_.pstPack->u32Len,
        //        venc_stream_.pstPack->u64PTS);

        // 释放编码器流资源
        // venc_driver_->releaseStream(venc_stream_);

        RK_MPI_VPSS_ReleaseGrpFrame(0, 0, &process_frame); 

        return 0;
    }

    /**
     * 将VENC_STREAM_S转换为AVPacket并放入队列
     * @param encode_frame 编码器输出的H.265流
     * @return 0成功，非0失败
     */
    int VideoStreamProcessor::pushEncodedPacketToQueue()
    {
        if (!is_running_)
        {
            printf("ERROR: VideoStreamProcessor is not running\n");
            return -1;
        }

        void *streamData = RK_MPI_MB_Handle2VirAddr(venc_stream_.pstPack->pMbBlk);

        AVPacket *pkt = av_packet_alloc();

        pkt->data = (uint8_t *)streamData;
        pkt->size = venc_stream_.pstPack->u32Len;
        pkt->pts = venc_stream_.pstPack->u64PTS;
        pkt->dts = pkt->pts;

        pkt->duration = 33333;

        // 设置关键帧标志
        H265E_NALU_TYPE_E nalu_type = venc_stream_.pstPack->DataType.enH265EType;
        if (nalu_type == H265E_NALU_IDRSLICE)
        {
            pkt->flags |= AV_PKT_FLAG_KEY; // 标记为关键帧
            printf("关键帧或参数集：类型=%d,长度=%u\n",
                   nalu_type, venc_stream_.pstPack->u32Len);
        }

        std::unique_lock<std::mutex> lock(queue_mutex_);
        // 队列满时丢最早的包
        if (packet_queue_.size() >= 10)
        {
            AVPacket *old_pkt = packet_queue_.front();
            av_packet_free(&old_pkt);
            packet_queue_.pop();
            printf("队列已满，丢弃帧,剩余{%lld/10}\n", (long long)packet_queue_.size());
        }

        packet_queue_.push(pkt); // 转移所有权到队列
        queue_cv_.notify_one();  // 通知等待的出队线程

        return 0;
    }

    /**
     * 取出H.265的AVPacket（供推流线程）
     * @param out_pkt 输出的AVPacket（需用av_packet_free释放）
     * @param timeout_ms 超时时间
     * @return 0成功，-1超时
     */
    /**
     * 取出队列中的AVPacket（供推流线程）
     * @param out_pkt 输出参数（用于接收AVPacket指针，需用av_packet_free释放）
     * @param timeout_ms 超时时间（毫秒）
     * @return 0成功，-1超时，-2线程需退出
     */
    int VideoStreamProcessor::popEncodedPacket(AVPacket *&out_pkt, int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 等待条件：队列非空 或 线程需退出
        bool has_data = queue_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                           [this]()
                                           { return !is_running_ || !packet_queue_.empty(); });

        if (!has_data)
        {
            out_pkt = nullptr;
            return -1; // 超时
        }

        // 线程需退出时，无论队列是否有数据都返回退出信号
        if (!is_running_)
        {
            out_pkt = nullptr;
            return -2; // 线程需退出
        }

        // 正常取出数据（转移所有权给out_pkt）
        out_pkt = packet_queue_.front();
        packet_queue_.pop();
        return 0;
    }

    void VideoStreamProcessor::releaseStreamAndFrame()
    {
        venc_driver_->releaseStream(venc_stream_);
    }

} // namespace core