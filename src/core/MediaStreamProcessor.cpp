#include "core/MediaStreamProcessor.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono> // 用于FPS统计

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "core/MediaStreamProcessor.hpp"
#include "core/RTSPStreamer.hpp"

extern "C"
{
#include "infra/logging/logger.h"
#include "rk_mpi_mb.h"
}

namespace core
{
    MediaStreamProcessor::MediaStreamProcessor(driver::VideoInputDriver *vi_driver,
                                               driver::VideoEncoderDriver *venc_driver,
                                               int rtsp_port,
                                               const char *rtsp_path,
                                               int rtsp_codec)
        : vi_driver_(vi_driver), venc_driver_(venc_driver)
    {
        is_running_ = false;
        // 创建RTSPStreamer实例（传入配置）
        LOGD("MediaStreamProcessor rtsp_port: %d, rtsp_path: %s, rtsp_codec: %d", rtsp_port, rtsp_path, rtsp_codec);
        rtsp_streamer_ = new core::RTSPStreamer(rtsp_port, rtsp_path, (rtsp_codec_id)rtsp_codec);
        memset(&venc_stream_, 0, sizeof(VENC_STREAM_S));
        initStreamBuffer();

        // 初始化BGR帧缓冲（用于水印绘制）
        m_bgrFrame = cv::Mat(cv::Size(width, height), CV_8UC3);
        LOGI("MediaStreamProcessor initialized ({}x{})", width, height);
    }

    // 析构函数：释放资源+停止循环
    MediaStreamProcessor::~MediaStreamProcessor()
    {
        stopProcess();
        // 释放RTSP资源（自动调用RTSPStreamer析构函数）
        if (rtsp_streamer_)
        {
            delete rtsp_streamer_;
            rtsp_streamer_ = nullptr;
        }
        releaseStreamBuffer(); // 释放malloc的VENC_PACK_S
    }

    // 初始化编码流缓冲区（封装原代码的 malloc）
    int MediaStreamProcessor::initStreamBuffer()
    {
        venc_stream_.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
        if (venc_stream_.pstPack == nullptr)
        {
            printf("MediaStreamProcessor::initStreamBuffer - malloc failed!\n");
            return -1;
        }
        memset(venc_stream_.pstPack, 0, sizeof(VENC_PACK_S));
        return 0;
    }

    // 释放编码流缓冲区
    void MediaStreamProcessor::releaseStreamBuffer()
    {
        if (venc_stream_.pstPack != nullptr)
        {
            free(venc_stream_.pstPack);
            venc_stream_.pstPack = nullptr;
        }
    }

    RK_U64 MediaStreamProcessor::TEST_COMM_GetNowUs()
    {
        struct timespec time = {0, 0};
        clock_gettime(CLOCK_MONOTONIC, &time);
        return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
    }

    // 启动业务循环（建议在独立线程中运行，避免阻塞主线程）
    int MediaStreamProcessor::startProcess()
    {
        if (is_running_)
        {
            LOGE("MediaStreamProcessor::startProcess - already running!");
            return 0;
        }

        // 先初始化RTSP服务（必须在推流前完成）
        if (rtsp_streamer_->init() != true)
        {
            LOGE("RTSPStreamer init failed");
            return -1;
        }

        // 初始化FPS统计时间
        start_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

        is_running_ = true;
        // 启动循环（若需避免阻塞主线程，可创建线程：std::thread(&MediaStreamProcessor::processLoop, this).detach()）
        // 启动采集→编码→推流循环
        processLoop();
        return 0;
    }

    // 停止业务循环（线程安全）
    void MediaStreamProcessor::stopProcess()
    {
        is_running_ = false;
        printf("MediaStreamProcessor::stopProcess - stopped!\n");
    }

    // 核心循环：采集→编码→输出
    void MediaStreamProcessor::processLoop()
    {
        VIDEO_FRAME_INFO_S vi_frame; // VI原始帧
        memset(&vi_frame, 0, sizeof(VIDEO_FRAME_INFO_S));

        while (is_running_)
        {
            int ret = 0;

            // 1. 从VI获取原始帧
            ret = vi_driver_->getFrame(vi_frame, -1); // -1表示阻塞等待
            if (ret != RK_SUCCESS)
            {
                printf("processLoop - get VI frame failed! ret=%d\n", ret);
                continue; // 失败重试
            }

            // 1. 转换YUV420SP到BGR
            void *viData = RK_MPI_MB_Handle2VirAddr(vi_frame.stVFrame.pMbBlk);
            if (!viData)
            {
                printf("Failed to get VI frame data\n");
                vi_driver_->releaseFrame(vi_frame);
                continue;
            }

            // YUV420SP帧内存大小：width*height*1.5（Y: width*height，UV: width*height/2）
            // cv::Mat yuvFrame(height + height / 2, width, CV_8UC1, viData);
            // cv::cvtColor(yuvFrame, m_bgrFrame, cv::COLOR_YUV420sp2BGR);

            // 2. 计算并绘制FPS
            RK_U64 nowUs = TEST_COMM_GetNowUs();
            // if (m_lastTimeUs != 0)
            // {
            //     m_fps = 1000000.0 / (nowUs - m_lastTimeUs);
            // }
            // m_lastTimeUs = nowUs;
            // snprintf(m_fpsText, sizeof(m_fpsText), "fps = %.2f", m_fps);
            // cv::putText(m_bgrFrame, m_fpsText, cv::Point(40, 40),
            //             cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

            // 3. 准备编码帧（填充信息）
            VIDEO_FRAME_INFO_S encode_frame = vi_frame; // 复制原始帧信息
            encode_frame.stVFrame.u64PTS = nowUs;       // 更新时间戳
            // 注意：如果需要修改帧数据（如使用水印后的BGR），需将m_bgrFrame数据拷贝到编码帧的内存
            // 此处假设直接使用原始帧编码，仅添加水印用于预览（如需编码水印帧需额外处理内存）

            // 4. 发送到编码器
            if (venc_driver_->sendFrame(encode_frame) != 0)
            {
                printf("Failed to send frame to encoder\n");
                vi_driver_->releaseFrame(vi_frame);
                continue;
            }

            // 3. 从VENC获取编码流
            ret = venc_driver_->getStream(venc_stream_, -1);
            if (ret == RK_SUCCESS)
            {
                void *streamData = RK_MPI_MB_Handle2VirAddr(venc_stream_.pstPack->pMbBlk);

                // 打印帧信息
                printf("获取视频数据且编码成功 当前帧数据长度==%d 当前帧时间戳==%lld\r\n",
                       venc_stream_.pstPack->u32Len,
                       venc_stream_.pstPack->u64PTS);

                //  推流（若已初始化RTSPStreamer）
                if (rtsp_streamer_->isInited())
                {
                    rtsp_streamer_->pushFrame((uint8_t *)streamData,
                                              venc_stream_.pstPack->u32Len,
                                              nowUs);
                    rtsp_streamer_->handleEvents();
                }

                // 5. 释放资源（顺序：先释放VI帧，再释放VENC流）
                vi_driver_->releaseFrame(vi_frame);
                venc_driver_->releaseStream(venc_stream_);

                // （可选）FPS统计
                fps_count_++;
                uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
                if (now - start_time_ >= 1000)
                { // 每1秒打印一次FPS
                    printf("当前FPS: %d\n", fps_count_);
                    fps_count_ = 0;
                    start_time_ = now;
                }
            }
            else
            {
                printf("MediaStreamProcessor::processLoop - get VENC stream failed! ret=%d\n", ret);
                vi_driver_->releaseFrame(vi_frame); // 失败释放VI帧
                continue;
            }
        }
    }

} // namespace core