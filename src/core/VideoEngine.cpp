#include "core/VideoEngine.hpp"
#include "core/VPSSManager.hpp"
#include "core/VideoStreamProcessor.hpp"
#include "driver/VideoInputDriver.hpp"

extern "C"
{
#include "infra/logging/logger.h"
}

namespace core
{
    VideoEngine::VideoEngine()
    {
        mpi_manager_ = new driver::MPIManager();
        isp_driver_ = new driver::ISPDriver();
        vi_driver_ = new driver::VideoInputDriver();
        venc_driver_ = new driver::VideoEncoderDriver();
        vpss_manager_ = new core::VPSSManager();
        // stream_processor_ = new core::VideoStreamProcessor(vi_driver_, venc_driver_, vpss_manager_);
    }

    VideoEngine::~VideoEngine()
    {
        stop();
    }

    // 内部统一完成：硬件初始化 + 业务处理器创建
    int VideoEngine::init()
    {
        if (is_inited_)
        {
            LOGW("already inited!");
            return 0;
        }

        int ret = 0;

        // 初始化MPI
        ret = mpi_manager_->init();
        CHECK_RET(ret, "mpi_manager_->init()");

        // 初始化ISP
        ret = isp_driver_->init();
        CHECK_RET(ret, "isp_driver_->init()");

        core::VedioEngineConfig vedio_config = {
            .input_config = {
                .dev_id = 0,
                .chn_id = 0,
                .width = 1920,
                .height = 1080,
            },
            .encode_config = {
                .chn_id = 0,
                .width = 1920,
                .height = 1080,
                .en_type = RK_VIDEO_ID_HEVC,
            },
        };

        // 初始化视频输入
        ret = vi_driver_->init(vedio_config.input_config);
        CHECK_RET(ret, "vi_driver_->init()");

        // 初始化VPSS
        ret = vpss_manager_->init();
        CHECK_RET(ret, "vpss_manager_->init()");

        // 初始化视频编码器
        ret = venc_driver_->init(vedio_config.encode_config);
        if (ret != RK_SUCCESS)
        {
            LOGE("MediaDeviceManager - VENC init failed!");
            return ret;
        }

        // 1. 检查硬件资源是否已初始化（必须在 initAllDevices 之后调用）
        if (vi_driver_ == nullptr || venc_driver_ == nullptr)
        {
            LOGE("createStreamProcessor failed: VI/VENC driver not initialized! Call initAllDevices first.");
            return -1;
        }
        // 传入VI/VENC实例和RTSP参数，创建业务处理器
        stream_processor_ = new core::VideoStreamProcessor(vi_driver_, venc_driver_, vpss_manager_);

        // 3. 初始化业务处理器
        ret = stream_processor_->init();
        CHECK_RET(ret, "stream_processor_->init");

        is_inited_ = true;
        LOGI("VideoEngine::init() - success!");
        return 0;
    }

    // 启动业务流程
    int VideoEngine::start()
    {
        if (!is_inited_ || !stream_processor_)
        {
            LOGE("start - not inited!");
            return -1;
        }

        int ret = stream_processor_->start();
        CHECK_RET(ret, "stream_processor_->start");

        is_running_ = true;
        video_thread_ = std::thread(&VideoEngine::videoThread, this);
        return 0;
    }

    // 停止业务流程
    void VideoEngine::stop()
    {
        // 1. 发退出信号给线程
        is_running_ = false;

        // 2. 等待线程安全退出（避免资源泄漏）
        if (video_thread_.joinable())
        {
            video_thread_.join();
        }

        if (stream_processor_)
        {
            stream_processor_->stop();
        }

        if (stream_processor_)
        {
            delete stream_processor_;
            stream_processor_ = nullptr;
        }
        if (venc_driver_)
        {
            delete venc_driver_;
            venc_driver_ = nullptr;
        }
        if (vi_driver_)
        {
            delete vi_driver_;
            vi_driver_ = nullptr;
        }
        if (isp_driver_)
        {
            delete isp_driver_;
            isp_driver_ = nullptr;
        }
        if (mpi_manager_)
        {
            delete mpi_manager_;
            mpi_manager_ = nullptr;
        }
        is_inited_ = false;
    }

    void VideoEngine::videoThread()
    {
        printf("开始视频处理线程\n");
        while (is_running_)
        {
            stream_processor_->loopProcess();
        }
    }
#if 0
    void VideoEngine::videoThread()
    {
        printf("开始视频处理线程\n");
        int ret = 0;
        while (1)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(16666)); // 60fps为例
            // 步骤1：从VI拿数据→送VPSS
            ret = stream_processor_->getFromVIAndsendToVPSS();
            if (ret != 0)
            {
                LOGW("VI get frame failed, retry");
                continue;
            }
            printf("拿取VI数据成功\n");

            // 步骤2：从VPSS拿数据→送OpenCV处理
            VIDEO_FRAME_INFO_S process_frame = {0};
            ret = stream_processor_->getFromVPSSAndProcessWithOpenCV(process_frame);
            if (ret != 0)
            {
                LOGW("OpenCV process failed, retry");
                continue;
            }
            printf("拿取VPSS数据成功\n");

            // 步骤3：发送OpenCV处理后的数据→送VENC编码
            VENC_STREAM_S encode_frame = {0};
            ret = stream_processor_->sendToVENCAndGetEncodedPacket(process_frame, encode_frame);
            if (ret != 0 || stream_processor_ == nullptr)
            {
                LOGW("VENC encode failed, retry");
                continue;
            }
            printf("送VENC编码成功\n");
            printf("视频处理线程循环中\n");

            // 步骤4：编码后的数据→存入队列
            // ret = stream_processor_->pushEncodedPacketToQueue(encode_frame);
            // if (ret != 0)
            // {
            //     LOGE("Push to queue failed");
            // }

            // （可选）控制帧率：如果处理太快，休眠避免CPU占用过高
            std::this_thread::sleep_for(std::chrono::microseconds(16666)); // 60fps为例
            printf("视频处理线程循环中\n");
        }
    }
#endif

} // namespace core