#include "core/MediaStreamProcessor.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono> // 用于FPS统计

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "core/MediaStreamProcessor.hpp"
#include "core/RTSPStreamer.hpp"
#include "core/VPSSManager.hpp"

extern "C"
{
#include "infra/logging/logger.h"
#include "rk_mpi_mb.h"
#include "rk_comm_vpss.h"
#include "rk_mpi_vpss.h"
}

namespace core
{
    RK_U64 TEST_COMM_GetNowUs()
    {
        struct timespec time = {0, 0};
        clock_gettime(CLOCK_MONOTONIC, &time);
        return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
    }

    MediaStreamProcessor::MediaStreamProcessor(driver::VideoInputDriver *vi_driver,
                                               driver::VideoEncoderDriver *venc_driver,
                                               int rtsp_port,
                                               const char *rtsp_path,
                                               int rtsp_codec)
        : vi_driver_(vi_driver), venc_driver_(venc_driver)
    {
        is_inited_ = false;

        // 创建RTSPStreamer实例（传入配置）
        LOGD("MediaStreamProcessor rtsp_port: %d, rtsp_path: %s, rtsp_codec: %d", rtsp_port, rtsp_path, rtsp_codec);
        rtsp_streamer_ = new core::RTSPStreamer(rtsp_port, rtsp_path, (rtsp_codec_id)rtsp_codec);

        // 创建VPSS实例
        vpss_manager_ = new core::VPSSManager(width, height);

        // 初始化编码流缓冲区
        memset(&venc_stream_, 0, sizeof(VENC_STREAM_S));

        // 初始化BGR帧缓冲
        m_bgrFrame = cv::Mat(cv::Size(width, height), CV_8UC3);
        LOGI("MediaStreamProcessor initialized (%dx%d)", width, height);

        // 初始化视频帧信息
        memset(&vi_frame, 0, sizeof(VIDEO_FRAME_INFO_S));
    }

    // 析构函数：释放资源+停止循环
    MediaStreamProcessor::~MediaStreamProcessor()
    {
        // 释放RTSP资源
        if (rtsp_streamer_)
        {
            delete rtsp_streamer_;
            rtsp_streamer_ = nullptr;
        }

        releaseStreamBuffer(); // 释放malloc的VENC_PACK_S

        releasePool(); // 释放YUV内存池
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

    // 初始化YUV内存池（大小为width*height*3，保留5个缓冲块）
    int MediaStreamProcessor::initPool()
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

    // 释放编码流缓冲区
    void MediaStreamProcessor::releasePool()
    {
        if (m_mb_pool != MB_INVALID_POOLID)
        {
            RK_MPI_MB_DestroyPool(m_mb_pool);
            m_mb_pool = MB_INVALID_POOLID;
        }
    }

    // RK_U64 MediaStreamProcessor::TEST_COMM_GetNowUs()
    // {
    //     struct timespec time = {0, 0};
    //     clock_gettime(CLOCK_MONOTONIC, &time);
    //     return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
    // }

    // 启动业务循环（建议在独立线程中运行，避免阻塞主线程）
    int MediaStreamProcessor::init()
    {
        if (is_inited_)
        {
            LOGE("MediaStreamProcessor::startProcess - already running!");
            return 0;
        }

        // 初始化编码流缓冲区
        if (initStreamBuffer() != 0)
        {
            LOGE("MediaStreamProcessor::initStreamBuffer - failed!");
            return -1;
        }

        // 初始化YUV内存池
        if (initPool() != 0)
        {
            LOGE("MediaStreamProcessor::initPool - failed!");
            return -1;
        }

        // 初始化RTSP服务
        if (rtsp_streamer_->init() != true)
        {
            LOGE("RTSPStreamer init failed");
            return -1;
        }

        // 初始化VPSS
        if (vpss_manager_->init() != 0)
        {
            LOGE("VPSSManager init failed");
            return -1;
        }

        // 初始化FPS统计时间
        start_time_ = 0;

        is_inited_ = true;
        // 启动循环（若需避免阻塞主线程，可创建线程：std::thread(&MediaStreamProcessor::processLoop, this).detach()）
        // 启动采集→编码→推流循环
        // processLoop();
        return 0;
    }

    // 停止业务循环（线程安全）
    void MediaStreamProcessor::stop()
    {
        is_inited_ = false;
        printf("MediaStreamProcessor::stopProcess - stopped!\n");
    }

    int MediaStreamProcessor::run()
    {
        if (!is_inited_)
        {
            LOGE("MediaStreamProcessor::run - not initialized!");
            return -1;
        }
        return loopProcess();
    }
    
    int MediaStreamProcessor::loopProcess()
    {
        int ret = 0;
        VPSS_GRP vpss_grp = 0;       // 确保已初始化的VPSS组号
        VPSS_GRP_PIPE vpss_pipe = 0; // 固定管道号0

        // 1. 从VI获取原始帧
        VIDEO_FRAME_INFO_S vi_frame;
        ret = vi_driver_->getFrame(vi_frame, -1); // 阻塞等待获取VI帧
        if (ret != RK_SUCCESS)
        {
            printf("VPSS发送帧失败！ret = %d\n", ret);
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        // 2. 发送VI帧到VPSS进行硬件格式转换
        ret = RK_MPI_VPSS_SendFrame(vpss_grp, vpss_pipe, &vi_frame, -1);
        if (ret != RK_SUCCESS)
        {
            printf("VPSS发送帧失败！ret=%d\n", ret);
            vi_driver_->releaseFrame(vi_frame); // 释放VI帧
            return -1;
        }
        vi_driver_->releaseFrame(vi_frame); // VPSS已接管，释放VI帧（后续不再操作vi_frame）

        // 3. 从VPSS获取转换后的BGR帧（硬件转换结果）
        VIDEO_FRAME_INFO_S bgr_frame;
        ret = RK_MPI_VPSS_GetChnFrame(vpss_grp, 0, &bgr_frame, 1000);
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
        {
            start_time_ = now;
        }
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

        // 6. 准备编码帧（直接复用VPSS输出的帧数据，零拷贝）
        VIDEO_FRAME_INFO_S encode_frame = bgr_frame;         // 拷贝帧信息（浅拷贝，共享内存块）
        encode_frame.stVFrame.enPixelFormat = RK_FMT_RGB888; // 匹配编码器格式（需与VPSS输出兼容）
        encode_frame.stVFrame.u64PTS = now;                  // 更新时间戳

        // 7. 发送到编码器
        if (venc_driver_->sendFrame(encode_frame) != 0)
        {
            printf("Failed to send frame to encoder\n");
            RK_MPI_VPSS_ReleaseGrpFrame(vpss_grp, vpss_pipe, &bgr_frame); // 释放VPSS帧
            return -1;
        }

        // 8. 从VENC获取编码流
        ret = venc_driver_->getStream(venc_stream_, -1);
        if (ret == RK_SUCCESS)
        {
            void *streamData = RK_MPI_MB_Handle2VirAddr(venc_stream_.pstPack->pMbBlk);

            // 打印帧信息
            printf("获取视频数据且编码成功 当前帧数据长度==%d 当前帧时间戳==%lld\r\n",
                   venc_stream_.pstPack->u32Len,
                   venc_stream_.pstPack->u64PTS);

            // 推流（若已初始化）
            if (rtsp_streamer_->isInited())
            {
                rtsp_streamer_->pushFrame(
                    (uint8_t *)streamData,
                    venc_stream_.pstPack->u32Len,
                    now);
                rtsp_streamer_->handleEvents();
            }

            // 释放编码器流资源
            venc_driver_->releaseStream(venc_stream_);
        }
        else
        {
            printf("MediaStreamProcessor::run - get VENC stream failed! ret=%d\n", ret);
            RK_MPI_VPSS_ReleaseGrpFrame(vpss_grp, vpss_pipe, &bgr_frame); // 释放VPSS帧
            return -1;
        }

        // 9. 释放VPSS帧（编码和推流完成后释放）
        ret = RK_MPI_VPSS_ReleaseGrpFrame(vpss_grp, vpss_pipe, &bgr_frame);
        if (ret != RK_SUCCESS)
        {
            printf("VPSS释放帧失败！ret=%d\n", ret);
            return -1;
        }

        return 0;
    }

} // namespace core