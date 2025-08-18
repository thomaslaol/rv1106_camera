#include "driver/MediaDeviceManager.hpp"
#include "driver/ISPDriver.hpp"
#include "driver/VideoInputDriver.hpp"
#include "driver/MPIManager.hpp"
#include "driver/VideoEncoderDriver.hpp"
#include "core/MediaStreamProcessor.hpp"

extern "C"
{
#include "infra/logging/logger.h"
}

namespace driver
{

    MediaDeviceManager::MediaDeviceManager()
        : mpi_manager_(nullptr),
          isp_driver_(nullptr),
          vi_driver_(nullptr),
          venc_driver_(nullptr) {}

    MediaDeviceManager::~MediaDeviceManager()
    {
        if (venc_driver_)
        {
            delete venc_driver_; // 触发VideoEncoderDriver的析构函数，释放编码资源
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
    }

    int MediaDeviceManager::initAllDevices(int width, int height, RK_CODEC_ID_E en_codec_type)
    {
        int ret = 0;
        // 初始化MPI
        mpi_manager_ = new MPIManager();
        ret = mpi_manager_->init();
        CHECK_RET(ret, "mpi_manager_->init()");

        // 初始化ISP
        isp_driver_ = new ISPDriver();
        ret |= isp_driver_->init();
        CHECK_RET(ret, "isp_driver_->init()");

        // 初始化视频输入
        vi_driver_ = new VideoInputDriver(0, 0);
        ret |= vi_driver_->init();
        CHECK_RET(ret, "vi_driver_->init()");

        // 初始化视频编码器
        venc_driver_ = new VideoEncoderDriver(0, width, height, RK_VIDEO_ID_HEVC);//H265
        ret = venc_driver_->init();
        if (ret != RK_SUCCESS)
        {
            LOGE("MediaDeviceManager - VENC init failed!");
            return ret;
        }

        return ret;
    }

    core::MediaStreamProcessor *driver::MediaDeviceManager::createStreamProcessor(int rtsp_port, const char *rtsp_path, int rtsp_codec)
    {
        // 1. 检查硬件资源是否已初始化（必须在 initAllDevices 之后调用）
        if (vi_driver_ == nullptr || venc_driver_ == nullptr)
        {
            LOGE("createStreamProcessor failed: VI/VENC driver not initialized! Call initAllDevices first.");
            return nullptr;
        }
        // 传入VI/VENC实例和RTSP参数，创建业务处理器
        return new core::MediaStreamProcessor(vi_driver_, venc_driver_, rtsp_port, rtsp_path, rtsp_codec);
    }


}
