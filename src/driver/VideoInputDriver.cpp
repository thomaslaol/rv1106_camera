#include "driver/VideoInputDriver.hpp"
#include "cstring"
extern "C"
{
#include "rk_comm_vi.h"
#include "rk_mpi_vi.h"
#include "infra/logging/logger.h"
}

namespace driver
{

    VideoInputDriver::VideoInputDriver(){}

    VideoInputDriver::~VideoInputDriver() {}

    int VideoInputDriver::init(driver::VideoInputConfig &config)
    {
        vi_config_ = config;
        // 初始化硬件设备（Dev 层）
        vi_dev_init();

        // 初始化逻辑通道（Chn 层）
        return vi_chn_init(vi_config_);
    }

    int VideoInputDriver::start()
    {
        return RK_MPI_VI_EnableChn(vi_config_.dev_id, vi_config_.chn_id);
    }

    int VideoInputDriver::stop()
    {
        return RK_MPI_VI_DisableChn(vi_config_.dev_id, vi_config_.chn_id);
    }

    int VideoInputDriver::getFrame(VIDEO_FRAME_INFO_S &frame, int timeout)
    {

        return RK_MPI_VI_GetChnFrame(vi_config_.dev_id, vi_config_.chn_id, &frame, timeout);
    }
    
    void VideoInputDriver::releaseFrame(const VIDEO_FRAME_INFO_S &frame)
    {
        RK_MPI_VI_ReleaseChnFrame(vi_config_.dev_id, vi_config_.chn_id, &frame);
    }
    
    int VideoInputDriver::vi_dev_init()
    {
        int pipeId = vi_config_.dev_id;
        VI_DEV_ATTR_S stDevAttr;
        VI_DEV_BIND_PIPE_S stBindPipe;

        memset(&stDevAttr, 0, sizeof(stDevAttr));
        memset(&stBindPipe, 0, sizeof(stBindPipe));

        // 0. get dev config status
        int ret = RK_MPI_VI_GetDevAttr(vi_config_.dev_id, &stDevAttr);
        if (ret == RK_ERR_VI_NOT_CONFIG)
        {
            // 0-1.config dev
            ret = RK_MPI_VI_SetDevAttr(vi_config_.dev_id, &stDevAttr);
            if (ret != RK_SUCCESS)
            {
                LOGE("RK_MPI_VI_SetDevAttr");
                return -1;
            }
        }
        else
        {
            LOGI("RK_MPI_VI_SetDevAttr already\n");
        }
        // 1.get dev enable status
        ret = RK_MPI_VI_GetDevIsEnable(vi_config_.chn_id);
        if (ret != RK_SUCCESS)
        {
            // 1-2.enable dev
            ret = RK_MPI_VI_EnableDev(vi_config_.chn_id);
            if (ret != RK_SUCCESS)
            {
                LOGE("RK_MPI_VI_EnableDev %x", ret);
                return -1;
            }
            // 1-3.bind dev/pipe
            stBindPipe.u32Num = 1;
            stBindPipe.PipeId[0] = pipeId;
            ret = RK_MPI_VI_SetDevBindPipe(vi_config_.dev_id, &stBindPipe);
            if (ret != RK_SUCCESS)
            {
                LOGE("RK_MPI_VI_SetDevBindPipe %x\n", ret);
                return -1;
            }
        }
        else
        {
            LOGI("RK_MPI_VI_EnableDev already\n");
        }

        return 0;
    }

    int VideoInputDriver::vi_chn_init(driver::VideoInputConfig &config)
    {
        // 设置属性
        VI_CHN_ATTR_S vi_chn_attr = {0};
        vi_chn_attr.bFlip = RK_FALSE;   // 是否翻转
        vi_chn_attr.bMirror = RK_FALSE; // 镜像
        vi_chn_attr.enAllocBufType = VI_ALLOC_BUF_TYPE_INTERNAL;
        vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;  // 编码压缩模式
        vi_chn_attr.enDynamicRange = DYNAMIC_RANGE_SDR10; // 动态范围
        vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;      // NV12
        // vi_chn_attr.stFrameRate={30,1};//帧率
        vi_chn_attr.stIspOpt.enCaptureType = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
        vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF; // DMA加速
        vi_chn_attr.stIspOpt.stMaxSize = {config.width, config.height};
        vi_chn_attr.stIspOpt.stWindow = {0, 0, config.width, config.height};
        vi_chn_attr.stIspOpt.u32BufCount = 3;
        vi_chn_attr.stIspOpt.u32BufSize = config.width * config.height * 2;
        vi_chn_attr.stSize.u32Height = config.height;
        vi_chn_attr.stSize.u32Width = config.width;
        vi_chn_attr.u32Depth = 3;

        int ret = RK_MPI_VI_SetChnAttr(vi_config_.dev_id, vi_config_.chn_id, &vi_chn_attr);
        if (ret != 0)
        {
            LOGE("create VI error! ret=%d\n", ret);
            return ret;
        }
        return 0;
    }

} // namespace driver
