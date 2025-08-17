#pragma once
#include "driver/VideoInputDriver.hpp"
#include "driver/VideoEncoderDriver.hpp"
#include <atomic> // 用于循环控制标志（线程安全）

extern "C"
{
#include "rk_mpi.h"
}

namespace core
{

    class MediaStreamProcessor
    {
    public:
        // 构造函数：依赖注入（传入driver层实例，解耦）
        MediaStreamProcessor(driver::VideoInputDriver *vi_driver,
                             driver::VideoEncoderDriver *venc_driver);
        ~MediaStreamProcessor();

        // 启动业务循环（采集→编码→输出）
        int startProcess();

        // 停止业务循环（支持外部控制退出）
        void stopProcess();

    private:
        // 核心循环：实际的帧处理逻辑
        void processLoop();

        // 初始化编码流缓冲区（处理 stFrame.pstPack 的 malloc）
        int initStreamBuffer();

        // 释放编码流缓冲区（处理 stFrame.pstPack 的 free）
        void releaseStreamBuffer();

    private:
        // 依赖的driver层实例（仅持有指针，不负责销毁）
        driver::VideoInputDriver *vi_driver_;
        driver::VideoEncoderDriver *venc_driver_;

        // 循环控制标志（原子变量，线程安全）
        std::atomic<bool> is_running_;

        // 编码流结构体（需长期管理，避免循环内重复malloc/free）
        VENC_STREAM_S venc_stream_;

        // FPS统计（可选，原代码中的fps_count）
        int fps_count_;
        // （可选）时间戳统计，用于计算FPS
        uint64_t start_time_;
    };

} // namespace core