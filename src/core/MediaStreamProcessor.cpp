#include "core/MediaStreamProcessor.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono> // 用于FPS统计

#include "core/MediaStreamProcessor.hpp"

extern "C"
{
    #include "infra/logging/logger.h"
}

namespace core
{

    // 构造函数：初始化依赖和资源
    MediaStreamProcessor::MediaStreamProcessor(driver::VideoInputDriver *vi_driver,
                                               driver::VideoEncoderDriver *venc_driver)
        : vi_driver_(vi_driver),
          venc_driver_(venc_driver),
          is_running_(false),
          fps_count_(0)
    {
        // 初始化编码流结构体（提前分配pstPack，避免循环内重复malloc）
        memset(&venc_stream_, 0, sizeof(VENC_STREAM_S));
        initStreamBuffer();
    }

    // 析构函数：释放资源+停止循环
    MediaStreamProcessor::~MediaStreamProcessor()
    {
        stopProcess();
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

    // 启动业务循环（建议在独立线程中运行，避免阻塞主线程）
    int MediaStreamProcessor::startProcess()
    {
        if (is_running_)
        {
            LOGE("MediaStreamProcessor::startProcess - already running!");
            return 0;
        }

        // 初始化FPS统计时间
        start_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

        is_running_ = true;
        // 启动循环（若需避免阻塞主线程，可创建线程：std::thread(&MediaStreamProcessor::processLoop, this).detach()）
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

            // 1. 从VI获取原始帧（调用driver层接口）
            ret = vi_driver_->getFrame(vi_frame, -1); // -1表示阻塞等待
            if (ret != RK_SUCCESS)
            {
                printf("MediaStreamProcessor::processLoop - get VI frame failed! ret=%d\n", ret);
                continue; // 失败重试
            }

            // 2. 将原始帧发送给VENC编码（调用driver层接口）
            ret = venc_driver_->sendFrame(vi_frame, -1);
            if (ret != RK_SUCCESS)
            {
                printf("MediaStreamProcessor::processLoop - send frame to VENC failed! ret=%d\n", ret);
                vi_driver_->releaseFrame(vi_frame); // 失败也要释放VI帧，避免泄漏
                continue;
            }

            // 3. 从VENC获取编码流（调用driver层接口）
            ret = venc_driver_->getStream(venc_stream_, -1);
            if (ret == RK_SUCCESS)
            {
                // 4. 业务输出：打印帧信息（可扩展为“保存文件”“推流”等）
                printf("获取视频数据且编码成功 当前帧数据长度==%d 当前帧时间戳==%lld\r\n",
                       venc_stream_.pstPack->u32Len,
                       venc_stream_.pstPack->u64PTS);

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