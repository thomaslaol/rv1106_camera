#include "core/VPSSManager.hpp"

extern "C"
{
#include "infra/logging/logger.h"
#include "rk_comm_vpss.h"
#include "rk_mpi_vpss.h"
#include "rk_mpi_sys.h"
}

namespace core
{

    VPSSManager::VPSSManager(int width, int height)
        : width_(width), height_(height)
    {
        grp_id_ = 0;
        chn_id_ = 0;
    }

    VPSSManager::~VPSSManager()
    {
        disableBackupFrame();
        disableVPSSChn();
        stopVPSS();
        destroyGroup();
    }

    int VPSSManager::init()
    {
        // 1. 创建 VPSS GROUP
        int ret = createGroup();
        CHECK_RET(ret, "createGroup");

        // 2. 启动 VPSS GROUP
        ret = startVPSS();
        CHECK_RET(ret, "startVPSS");

        // 3. 配置 VPSS 通道属性
        ret = setVPSSChnAttr();
        CHECK_RET(ret, "setVPSSChnAttr");

        // 4. 启用 VPSS 通道
        ret = enableVPSSChn();
        CHECK_RET(ret, "enableVPSSChn");

        // 5. 启用备份帧防止丢帧
        enableBackupFrame();

        LOGI("VPSS init success");
        return 0;
    }

    int VPSSManager::createGroup()
    {
        VPSS_GRP_ATTR_S grpAttr = {
            .u32MaxW = width_,
            .u32MaxH = height_,
            .enPixelFormat = RK_FMT_YUV420SP, // 输入格式为 YUV420SP
            .enDynamicRange = DYNAMIC_RANGE_SDR10,
            .enCompressMode = COMPRESS_MODE_NONE};

        int ret = RK_MPI_VPSS_CreateGrp(grp_id_, &grpAttr);
        if (ret != RK_SUCCESS)
        {
            printf("[VPSS] Create group failed: 0x%X\n", ret);
            return -1;
        }
        return 0;
    }

    int VPSSManager::startVPSS()
    {
        int ret = RK_MPI_VPSS_StartGrp(grp_id_);
        if (ret != RK_SUCCESS)
        {
            printf("[VPSS] Start group failed: 0x%X\n", ret);
            return -1;
        }
        return 0;
    }

    // 配置 VPSS 通道属性
    int VPSSManager::setVPSSChnAttr()
    {
        VPSS_CHN_ATTR_S chnAttr = {
            .enChnMode = VPSS_CHN_MODE_USER,
            .u32Width = width_,
            .u32Height = height_,
            .enVideoFormat = VIDEO_FORMAT_LINEAR,  // 线性视频格式
            .enPixelFormat = RK_FMT_RGB888,        // 输出格式为 BGR888
            .enDynamicRange = DYNAMIC_RANGE_SDR10, // SDR 10位动态范围
            .enCompressMode = COMPRESS_MODE_NONE,  // 无压缩
            .stFrameRate = {
                // 帧率控制
                .s32SrcFrameRate = -1, // 源帧率 (不限制)
                .s32DstFrameRate = -1  // 目标帧率 (不限制)
            },
            .bMirror = RK_FALSE, // 镜像: 禁用
            .bFlip = RK_FALSE,   // 翻转: 禁用
            .u32Depth = 1,       // 缓冲区深度
            .stAspectRatio = {
                // 宽高比
                .enMode = ASPECT_RATIO_NONE, // 不改变宽高比
                .u32BgColor = 0x00000000     // 黑色背景
            },
            .u32FrameBufCnt = 0 // 使用默认帧缓冲区数量
        };

        int ret = RK_MPI_VPSS_SetChnAttr(grp_id_, chn_id_, &chnAttr);
        if (ret != RK_SUCCESS)
        {
            printf("[VPSS] Set channel attr failed: 0x%X\n", ret);
            return -1;
        }
        return 0;
    }

    // 启用 VPSS 通道
    int VPSSManager::enableVPSSChn()
    {
        int ret = RK_MPI_VPSS_EnableChn(grp_id_, chn_id_);
        if (ret != RK_SUCCESS)
        {
            printf("[VPSS] Enable channel failed: 0x%X\n", ret);
            return -1;
        }
        return 0;
    }

    // 启用备份帧防止丢帧
    int VPSSManager::enableBackupFrame()
    {
        int ret = RK_MPI_VPSS_EnableBackupFrame(grp_id_);
        if (ret != RK_SUCCESS)
        {
            printf("[VPSS] Enable backup frame failed: 0x%X\n", ret);
            return -1;
        }
        return 0;
    }

    int VPSSManager::disableBackupFrame()
    {
        int ret = RK_MPI_VPSS_DisableBackupFrame(grp_id_);
        if (ret != RK_SUCCESS)
        {
            printf("[VPSS] Disable backup frame failed: 0x%X\n", ret);
            return -1;
        }
        return 0;
    }

    int VPSSManager::disableVPSSChn()
    {
        int ret = RK_MPI_VPSS_DisableChn(grp_id_, chn_id_);
        if (ret != RK_SUCCESS)
        {
            printf("[VPSS] Disable channel failed: 0x%X\n", ret);
            return -1;
        }
        return 0;
    }
    int VPSSManager::stopVPSS()
    {
        int ret = RK_MPI_VPSS_StopGrp(grp_id_);
        if (ret != RK_SUCCESS)
        {
            printf("[VPSS] Stop group failed: 0x%X\n", ret);
            return -1;
        }
        return 0;
    }
    int VPSSManager::destroyGroup()
    {
        int ret = RK_MPI_VPSS_DestroyGrp(grp_id_);
        if (ret != RK_SUCCESS)
        {
            printf("[VPSS] Destroy group failed: 0x%X\n", ret);
            return -1;
        }
        return 0;
    }

} // namespace driver
