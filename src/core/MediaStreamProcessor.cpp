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
#include "rga/RgaApi.h"
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

        // 初始化编码流缓冲区
        memset(&venc_stream_, 0, sizeof(VENC_STREAM_S));

        // 初始化BGR帧缓冲
        m_bgrFrame = cv::Mat(cv::Size(width, height), CV_8UC3);
        LOGI("MediaStreamProcessor initialized (%dx%d)", width, height);

        memset(&vi_frame, 0, sizeof(VIDEO_FRAME_INFO_S));

        // int ret = c_RkRgaInit();
        // if (ret != 0)
        // {
        //     printf("RGA初始化失败！ret=%d\n", ret);
        // }
        // else
        // {
        //     rga_inited_ = true;
        //     // 初始化RGA信息结构体（避免重复 memset）
        //     memset(&rga_src_, 0, sizeof(rga_info_t));
        //     memset(&rga_dst_, 0, sizeof(rga_info_t));
        //     memset(&rga_src1_, 0, sizeof(rga_info_t));
        // }
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

        // 反初始化RGA
        if (rga_inited_)
        {
            c_RkRgaDeInit();
            rga_inited_ = false;
        }
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

    // int MediaStreamProcessor::initPool()
    // {
    //     // 1. 检查系统CMA内存是否充足（调试用）
    //     FILE *cma_file = fopen("/proc/meminfo", "r");
    //     if (cma_file)
    //     {
    //         char buf[256];
    //         while (fgets(buf, sizeof(buf), cma_file))
    //         {
    //             if (strstr(buf, "CmaTotal") || strstr(buf, "CmaFree"))
    //             {
    //                 printf("系统CMA内存: %s", buf); // 确保CmaFree不为0
    //             }
    //         }
    //         fclose(cma_file);
    //     }

    //     MB_POOL_CONFIG_S pool_cfg;
    //     memset(&pool_cfg, 0, sizeof(pool_cfg));

    //     // 2. 基础配置
    //     pool_cfg.u64MBSize = width * height * 3; // BGR888单块大小（宽×高×3）
    //     pool_cfg.u32MBCnt = 5;                  // 内存块数量（避免高帧率耗尽）
    //     pool_cfg.bPreAlloc = RK_TRUE;            // 预分配所有内存块（关键：确保启动时就分配物理内存）
    //     pool_cfg.bNotDelete = RK_FALSE;          // 允许释放内存池

    //     // 3. 关键：配置为物理连续内存（根据结构体成员适配）
    //     pool_cfg.enAllocType = MB_ALLOC_TYPE_DMA;    // 强制DMA分配类型（物理连续）
    //     pool_cfg.enDmaType = MB_DMA_TYPE_CMA;        // 指定使用CMA内存（瑞芯微平台常用，需确认枚举值）
    //     pool_cfg.enRemapMode = MB_REMAP_MODE_CACHED; // 启用缓存映射（确保虚拟地址可访问）

    //     // 4. 创建内存池
    //     m_mb_pool = RK_MPI_MB_CreatePool(&pool_cfg);
    //     if (m_mb_pool == MB_INVALID_POOLID)
    //     {
    //         LOGE("创建物理内存池失败！可能CMA内存不足或配置错误");
    //         return -1;
    //     }

    //     // 5. 验证内存池是否为物理连续（通过尝试获取第一个块的物理地址）
    //     MB_BLK test_blk = RK_MPI_MB_GetMB(m_mb_pool, pool_cfg.u64MBSize, RK_TRUE);
    //     if (test_blk)
    //     {
    //         uintptr_t test_phys = (uintptr_t)RK_MPI_MB_Handle2PhysAddr(test_blk);
    //         if (test_phys == 0)
    //         {
    //             LOGE("内存池分配的是虚拟内存，无法获取物理地址！");
    //             RK_MPI_MB_ReleaseMB(test_blk);
    //             RK_MPI_MB_DestroyPool(m_mb_pool);
    //             m_mb_pool = MB_INVALID_POOLID;
    //             return -1;
    //         }
    //         else
    //         {
    //             LOGI("内存池验证成功，物理地址: 0x%lx", test_phys);
    //             RK_MPI_MB_ReleaseMB(test_blk); // 释放测试块
    //         }
    //     }
    //     else
    //     {
    //         LOGE("内存池创建成功，但无法分配测试块！");
    //         RK_MPI_MB_DestroyPool(m_mb_pool);
    //         m_mb_pool = MB_INVALID_POOLID;
    //         return -1;
    //     }

    //     return 0;
    // }

    // 释放编码流缓冲区
    void MediaStreamProcessor::releasePool()
    {
        if (m_mb_pool != MB_INVALID_POOLID)
        {
            RK_MPI_MB_DestroyPool(m_mb_pool);
            m_mb_pool = MB_INVALID_POOLID;
        }
    }

    RK_U64 MediaStreamProcessor::TEST_COMM_GetNowUs()
    {
        struct timespec time = {0, 0};
        clock_gettime(CLOCK_MONOTONIC, &time);
        return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
    }

    // 启动业务循环（建议在独立线程中运行，避免阻塞主线程）
    int MediaStreamProcessor::init()
    {
        if (is_running_)
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

        // 初始化FPS统计时间
        start_time_ = 0;

        is_running_ = true;
        // 启动循环（若需避免阻塞主线程，可创建线程：std::thread(&MediaStreamProcessor::processLoop, this).detach()）
        // 启动采集→编码→推流循环
        // processLoop();
        return 0;
    }

    // 停止业务循环（线程安全）
    void MediaStreamProcessor::stopProcess()
    {
        is_running_ = false;
        printf("MediaStreamProcessor::stopProcess - stopped!\n");
    }

    // 核心循环：采集→编码→输出
/*
    int MediaStreamProcessor::run()
    {
        int ret = 0;
printf("MediaStreamProcessor::run - start!\n");

        // 新增：先检查内存池是否有效
        if (m_mb_pool == MB_INVALID_POOLID)
        {
            printf("内存池无效（ID=-1），无法分配内存块！\n");
            // 若未初始化，尝试重新初始化（可选）
            if (initPool() != 0)
            {
                return -1; // 重新初始化失败，退出
            }
        }
printf("MediaStreamProcessor::run - 内存池有效，继续处理\n");

        // 1. 从VI获取原始帧
        printf("MediaStreamProcessor::run - 开始获取VI帧\n");
        ret = vi_driver_->getFrame(vi_frame, 1000); // -1表示阻塞等待
        printf("MediaStreamProcessor::run - 获取VI帧结束，ret=%d\n", ret);
        if (ret != RK_SUCCESS)
        {
            printf("processLoop - get VI frame failed! ret=%d\n", ret);
            return -1;
        }

        printf("VI帧获取成功，宽=%d，高=%d，格式=%d\n", vi_frame.stVFrame.u32Width, vi_frame.stVFrame.u32Height, vi_frame.stVFrame.enPixelFormat);

        // 2. 准备RGA转换（YUV420SP→BGR888，硬件加速）
        if (!rga_inited_)
        {
            printf("RGA未初始化，无法进行硬件转换\n");
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        printf("RGA初始化成功，开始转换\n");

        // 2.1 获取VI的YUV数据物理地址
        uintptr_t yuv_phys = (uintptr_t)RK_MPI_MB_Handle2PhysAddr(vi_frame.stVFrame.pMbBlk);
        if (yuv_phys == 0)
        {
            printf("无法获取YUV帧的物理地址\n");
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }
        printf("YUV物理地址: 0x%lx\n", yuv_phys);

        // 2.2 分配BGR内存块（无需手动Unmap，直接释放即可）
        MB_BLK bgr_blk = RK_MPI_MB_GetMB(m_mb_pool, width * height * 3, RK_TRUE);
        if (!bgr_blk)
        {
            printf("Failed to allocate BGR memory block（内存池耗尽）\n");
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        // 部分平台无需手动Map，直接通过Handle2VirAddr获取地址
        void *bgr_vir = RK_MPI_MB_Handle2VirAddr(bgr_blk);
        if (!bgr_vir)
        {
            printf("BGR虚拟地址获取失败！\n");
            RK_MPI_MB_ReleaseMB(bgr_blk); // 直接释放，无需Unmap
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        // 获取BGR物理地址
        uintptr_t bgr_phys = (uintptr_t)RK_MPI_MB_Handle2PhysAddr(bgr_blk);
        if (bgr_phys == 0)
        {
            printf("无法获取BGR内存块的物理地址（phys=0x%lx）\n", bgr_phys);
            RK_MPI_MB_ReleaseMB(bgr_blk); // 直接释放
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }
        printf("BGR物理地址: 0x%lx\n", bgr_phys);

        // 2.3 配置RGA源（YUV420SP）
        rga_src_.fd = -1;
        rga_src_.virAddr = RK_MPI_MB_Handle2VirAddr(vi_frame.stVFrame.pMbBlk);
        rga_src_.phyAddr = (void *)yuv_phys;
        rga_src_.format = RK_FMT_YUV420SP;

        rga_src_.rect.xoffset = 0;
        rga_src_.rect.yoffset = 0;
        rga_src_.rect.width = width;
        rga_src_.rect.height = height;
        rga_src_.rect.wstride = width;

        // 2.4 配置RGA目标（BGR888）
        rga_dst_.fd = -1;
        rga_dst_.virAddr = bgr_vir;
        rga_dst_.phyAddr = (void *)bgr_phys;
        rga_dst_.format = RK_FMT_BGR888;

        rga_dst_.rect.xoffset = 0;
        rga_dst_.rect.yoffset = 0;
        rga_dst_.rect.width = width;
        rga_dst_.rect.height = height;
        rga_dst_.rect.wstride = width * 3;

        // 2.5 执行YUV→BGR转换
        ret = c_RkRgaBlit(&rga_src_, &rga_dst_, &rga_src1_);
        if (ret != 0)
        {
            printf("RGA YUV→BGR转换失败！ret=%d\n", ret);
            RK_MPI_MB_ReleaseMB(bgr_blk); // 直接释放
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        // 3. 绘制FPS
        cv::Mat bgrFrame(height, width, CV_8UC3, bgr_vir);

        uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();

        if (start_time_ == 0)
            start_time_ = now;
        m_frameCount++;

        // 计算FPS
        uint64_t elapsed = now - start_time_;
        if (elapsed >= 1000000)
        {
            m_fps = (m_frameCount * 1000000.0) / elapsed;
            start_time_ = now;
            printf("FPS: %.2f\n", m_fps);
            snprintf(m_fpsText, sizeof(m_fpsText), "fps = %.1f", m_fps);
            m_frameCount = 0;
        }
        cv::putText(bgrFrame, m_fpsText, cv::Point(40, 40),
                    cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

        // 4. RGA将BGR888转换为RGB888
        // 4.1 分配RGB内存块
        MB_BLK rgb_blk = RK_MPI_MB_GetMB(m_mb_pool, width * height * 3, RK_TRUE);
        if (!rgb_blk)
        {
            printf("Failed to allocate RGB memory block\n");
            RK_MPI_MB_ReleaseMB(bgr_blk);
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        void *rgb_vir = RK_MPI_MB_Handle2VirAddr(rgb_blk);
        if (!rgb_vir)
        {
            printf("RGB虚拟地址获取失败！\n");
            RK_MPI_MB_ReleaseMB(rgb_blk);
            RK_MPI_MB_ReleaseMB(bgr_blk);
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        uintptr_t rgb_phys = (uintptr_t)RK_MPI_MB_Handle2PhysAddr(rgb_blk);
        if (rgb_phys == 0)
        {
            printf("无法获取RGB内存块的物理地址\n");
            RK_MPI_MB_ReleaseMB(rgb_blk);
            RK_MPI_MB_ReleaseMB(bgr_blk);
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        // 4.2 重新配置RGA（BGR→RGB）
        rga_src_.virAddr = bgr_vir;
        rga_src_.phyAddr = (void *)bgr_phys;
        rga_src_.format = RK_FMT_BGR888;

        rga_src_.rect.xoffset = 0;
        rga_src_.rect.yoffset = 0;
        rga_src_.rect.width = width;
        rga_src_.rect.height = height;
        rga_src_.rect.wstride = width * 3;

        rga_dst_.virAddr = rgb_vir;
        rga_dst_.phyAddr = (void *)rgb_phys;
        rga_dst_.format = RK_FMT_RGB888;

        rga_dst_.rect.xoffset = 0;
        rga_dst_.rect.yoffset = 0;
        rga_dst_.rect.width = width;
        rga_dst_.rect.height = height;
        rga_dst_.rect.wstride = width * 3;

        // 4.3 执行BGR→RGB转换
        ret = c_RkRgaBlit(&rga_src_, &rga_dst_, &rga_src1_);
        if (ret != 0)
        {
            printf("RGA BGR→RGB转换失败！ret=%d\n", ret);
            RK_MPI_MB_ReleaseMB(rgb_blk);
            RK_MPI_MB_ReleaseMB(bgr_blk);
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        // 5. 准备编码帧
        VIDEO_FRAME_INFO_S encode_frame;
        memset(&encode_frame, 0, sizeof(encode_frame));
        encode_frame.stVFrame.enPixelFormat = RK_FMT_RGB888;
        encode_frame.stVFrame.u32Width = width;
        encode_frame.stVFrame.u32Height = height;
        encode_frame.stVFrame.pMbBlk = rgb_blk;
        encode_frame.stVFrame.u64PTS = now;

        // 6. 发送到编码器
        if (venc_driver_->sendFrame(encode_frame) != 0)
        {
            printf("Failed to send frame to encoder\n");
            RK_MPI_MB_ReleaseMB(rgb_blk);
            RK_MPI_MB_ReleaseMB(bgr_blk);
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        // 7. 获取编码流并推流
        printf("等待编码流...\n");
        ret = venc_driver_->getStream(venc_stream_, -1);
        printf("getStream ret=%d\n", ret);
        if (ret == RK_SUCCESS)
        {
            void *streamData = RK_MPI_MB_Handle2VirAddr(venc_stream_.pstPack->pMbBlk);
            printf("编码成功 长度=%d 时间戳=%lld\n",
                   venc_stream_.pstPack->u32Len, venc_stream_.pstPack->u64PTS);

            if (rtsp_streamer_->isInited())
            {
                rtsp_streamer_->pushFrame((uint8_t *)streamData,
                                          venc_stream_.pstPack->u32Len, now);
                rtsp_streamer_->handleEvents();
            }

            // 8. 释放资源（直接释放内存块，无需Unmap）
            vi_driver_->releaseFrame(vi_frame);
            venc_driver_->releaseStream(venc_stream_);
            RK_MPI_MB_ReleaseMB(rgb_blk);
            RK_MPI_MB_ReleaseMB(bgr_blk);
        }
        else
        {
            printf("获取编码流失败！ret=%d\n", ret);
            vi_driver_->releaseFrame(vi_frame);
            RK_MPI_MB_ReleaseMB(rgb_blk);
            RK_MPI_MB_ReleaseMB(bgr_blk);
            return -1;
        }
        return 0;
    }
    */

    
    int MediaStreamProcessor::run()
    {
        int ret = 0;

        // 1. 从VI获取原始帧
        ret = vi_driver_->getFrame(vi_frame, -1); // -1表示阻塞等待
        if (ret != RK_SUCCESS)
        {
            printf("processLoop - get VI frame failed! ret=%d\n", ret);
            return -1;
        }

        // rga_set_format();

        // 1. 转换YUV420SP到BGR
        void *viData = RK_MPI_MB_Handle2VirAddr(vi_frame.stVFrame.pMbBlk);
        if (!viData)
        {
            printf("Failed to get VI frame data\n");
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        // YUV420SP帧内存大小：width*height*1.5（Y: width*height，UV: width*height/2）
        cv::Mat yuvFrame(height + height / 2, width, CV_8UC1, viData);
        cv::cvtColor(yuvFrame, m_bgrFrame, cv::COLOR_YUV420sp2BGR);

        uint64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();

        // 初始化起始时间
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

            // 显示正确的FPS值（用m_fps而非now）
            snprintf(m_fpsText, sizeof(m_fpsText), "fps = %.1f", m_fps);

            m_frameCount = 0;
        }

        cv::putText(m_bgrFrame, m_fpsText, cv::Point(40, 40),
                    cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

        // 3. 准备编码帧
        VIDEO_FRAME_INFO_S encode_frame;
        memset(&encode_frame, 0, sizeof(encode_frame));
        encode_frame.stVFrame.enPixelFormat = RK_FMT_RGB888; // 匹配编码器格式
        encode_frame.stVFrame.u32Width = width;
        encode_frame.stVFrame.u32Height = height;
        encode_frame.stVFrame.u32VirWidth = width;
        encode_frame.stVFrame.u32VirHeight = height;

        // a. 分配BGR内存块（大小：width*height*3，每个像素3字节）
        MB_BLK bgr_blk = RK_MPI_MB_GetMB(m_mb_pool, width * height * 3, RK_TRUE);
        if (!bgr_blk)
        {
            printf("Failed to allocate BGR memory block\n");
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        // b. 直接拷贝BGR数据
        unsigned char *bgr_data = (unsigned char *)RK_MPI_MB_Handle2VirAddr(bgr_blk);
        memcpy(bgr_data, m_bgrFrame.data, width * height * 3); // 直接拷贝BGR数据

        // c. 更新编码帧信息
        encode_frame.stVFrame.pMbBlk = bgr_blk;
        encode_frame.stVFrame.u64PTS = now; // 微秒级时间戳

        // 4. 发送到编码器
        if (venc_driver_->sendFrame(encode_frame) != 0)
        {
            printf("Failed to send frame to encoder\n");
            vi_driver_->releaseFrame(vi_frame);
            RK_MPI_MB_ReleaseMB(bgr_blk);
            return -1;
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

            //  推流
            if (rtsp_streamer_->isInited())
            {
                rtsp_streamer_->pushFrame((uint8_t *)streamData,
                                          venc_stream_.pstPack->u32Len,
                                          now);
                rtsp_streamer_->handleEvents();
            }

            // 5. 释放资源 先释放VI帧，再释放VENC流
            vi_driver_->releaseFrame(vi_frame);
            venc_driver_->releaseStream(venc_stream_);
            RK_MPI_MB_ReleaseMB(bgr_blk);
        }
        else
        {
            printf("MediaStreamProcessor::processLoop - get VENC stream failed! ret=%d\n", ret);
            vi_driver_->releaseFrame(vi_frame); // 失败释放VI帧
            RK_MPI_MB_ReleaseMB(bgr_blk);
            return -1;
        }
        return 0;
    }


} // namespace core