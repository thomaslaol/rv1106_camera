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
#include "rk_comm_vpss.h"
#include "rk_mpi_vpss.h"
#include "rk_mpi_sys.h"
    // #include "rk_comm_vo.h"
    // #include "rk_mpi_vo.h"
    // #include "rga/RgaApi.h"
}

// 全局变量
VPSS_GRP_ATTR_S grp_attr;
VPSS_CHN_ATTR_S chn_attr;
VPSS_GRP vpss_grp = 0;
VPSS_CHN vpss_chn = 0;
MB_POOL m_mb_pool; // 全局MB池（用于VPSS缓存帧）

// 1. 创建MB池（关键：为VPSS提供内存缓冲）
// 修正MB池创建函数（匹配结构体定义）
int createMBPool(int width, int height)
{
    // YUV420SP单帧大小：width*height*3/2
    int frame_size = width * height * 3 / 2;
    // 池大小：至少能容纳5帧（总大小=单帧大小×5）
    RK_U64 total_pool_size = (RK_U64)frame_size * 3;

    // 定义并初始化内存池配置结构体（匹配实际成员）
    MB_POOL_CONFIG_S mb_config = {0};
    mb_config.u64MBSize = total_pool_size;      // 总内存大小（结构体中实际成员）
    mb_config.u32MBCnt = 3;                     // 内存块数量（5块对应5帧）
    mb_config.enRemapMode = MB_REMAP_MODE_NONE; // 不启用地址重映射（默认值）
    mb_config.enAllocType = MB_ALLOC_TYPE_DMA;  //
    mb_config.enDmaType = MB_DMA_TYPE_NONE;     // 不指定DMA类型（默认值）
    mb_config.bPreAlloc = RK_TRUE;              // 预分配内存（立即分配）
    mb_config.bNotDelete = RK_FALSE;            // 允许销毁（程序退出时释放）

    // 创建MB池（传入正确配置）
    m_mb_pool = RK_MPI_MB_CreatePool(&mb_config);
    if (m_mb_pool == 0 || m_mb_pool == (MB_POOL)-1)
    {
        // printf("DMA分配失败，尝试MALLOC方式...\n");
        // mb_config.enAllocType = MB_ALLOC_TYPE_MALLOC;  // 改用标准内存分配
        // m_mb_pool = RK_MPI_MB_CreatePool(&mb_config);
        // if (m_mb_pool == 0 || m_mb_pool == (MB_POOL)-1) {
        //     printf("MALLOC分配也失败！\n");
        //     return -1;
        // }
        printf("MB池创建失败！\n");
        return -1;
    }
    printf("MB池创建成功！地址=%p，总大小=%llu字节\n",
           m_mb_pool, mb_config.u64MBSize);
    return 0;
}

// // 2. 初始化VPSS（包含官方API要求的关键步骤）
// int initVPSS(int width, int height)
// {
//     int ret = 0;

//     // 步骤1：创建MB池（必须在VPSS初始化前）
//     if (createMBPool(width, height) != 0)
//     {
//         return -1;
//     }

//     // 步骤2：配置并创建VPSS组
//     grp_attr.u32MaxW = width;
//     grp_attr.u32MaxH = height;
//     grp_attr.enPixelFormat = RK_FMT_YUV420SP; // 与VI格式一致
//     grp_attr.enDynamicRange = DYNAMIC_RANGE_SDR10;
//     grp_attr.enCompressMode = COMPRESS_MODE_NONE;

//     ret = RK_MPI_VPSS_CreateGrp(vpss_grp, &grp_attr);
//     if (ret != RK_SUCCESS)
//     {
//         printf("创建VPSS组失败！ret=0x%X\n", ret);
//         RK_MPI_MB_DestroyPool(m_mb_pool); // 清理MB池
//         return ret;
//     }

//     // 步骤3：使能backup帧（关键：确保帧被缓存）
//     ret = RK_MPI_VPSS_EnableBackupFrame(vpss_grp);
//     if (ret != RK_SUCCESS)
//     {
//         printf("使能backup帧失败！ret=0x%X\n", ret);
//         RK_MPI_VPSS_DestroyGrp(vpss_grp);
//         RK_MPI_MB_DestroyPool(m_mb_pool);
//         return ret;
//     }

//     // 步骤4：配置VPSS通道（PASSTHROUGH模式，无转换）
//     chn_attr.enChnMode = VPSS_CHN_MODE_PASSTHROUGH; // 基础转发模式
//     chn_attr.enPixelFormat = RK_FMT_YUV420SP;       // 与输入一致
//     chn_attr.u32Width = width;
//     chn_attr.u32Height = height;
//     chn_attr.u32FrameBufCnt = 3; // 不超过最大限制

//     ret = RK_MPI_VPSS_SetChnAttr(vpss_grp, vpss_chn, &chn_attr);
//     if (ret != RK_SUCCESS)
//     {
//         printf("设置通道属性失败！ret=0x%X\n", ret);
//         RK_MPI_VPSS_DisableBackupFrame(vpss_grp);
//         RK_MPI_VPSS_DestroyGrp(vpss_grp);
//         RK_MPI_MB_DestroyPool(m_mb_pool);
//         return ret;
//     }

//     // 步骤5：绑定VPSS通道到MB池（核心：为通道分配内存）
//     ret = RK_MPI_VPSS_AttachMbPool(vpss_grp, vpss_chn, m_mb_pool);
//     if (ret != RK_SUCCESS)
//     {
//         printf("绑定MB池失败！ret=0x%X（无内存则无法缓存帧）\n", ret);
//         RK_MPI_VPSS_DisableBackupFrame(vpss_grp);
//         RK_MPI_VPSS_DestroyGrp(vpss_grp);
//         RK_MPI_MB_DestroyPool(m_mb_pool);
//         return ret;
//     }

//     // 步骤6：启用通道并启动组
//     ret = RK_MPI_VPSS_EnableChn(vpss_grp, vpss_chn);
//     if (ret != RK_SUCCESS)
//     {
//         printf("启用通道失败！ret=0x%X\n", ret);
//         // 清理绑定和资源...
//         return ret;
//     }

//     ret = RK_MPI_VPSS_StartGrp(vpss_grp);
//     if (ret != RK_SUCCESS)
//     {
//         printf("启动组失败！ret=0x%X\n", ret);
//         // 清理通道和资源...
//         return ret;
//     }

//     printf("VPSS初始化完成！grp=%d, chn=%d（已绑定MB池和使能backup）\n", vpss_grp, vpss_chn);
//     return 0;
// }
#define VPSS_GRP_ID 0 // 使用 GROUP 0
#define VPSS_CHN_ID 0 // 使用通道 0
#define MAX_WIDTH 1920
#define MAX_HEIGHT 1080

// 初始化 VPSS 用于 YUV420SP 转 BGR888
// 返回: 0 成功, -1 失败
int initVPSS()
{
    RK_S32 s32Ret = RK_SUCCESS;

    // 1. 创建 VPSS GROUP
    VPSS_GRP_ATTR_S grpAttr = {
        .u32MaxW = MAX_WIDTH,
        .u32MaxH = MAX_HEIGHT,
        .enPixelFormat = RK_FMT_YUV420SP, // 输入格式为 YUV420SP
        .enDynamicRange = DYNAMIC_RANGE_SDR10,
        .enCompressMode = COMPRESS_MODE_NONE
    };

    s32Ret = RK_MPI_VPSS_CreateGrp(VPSS_GRP_ID, &grpAttr);
    if (s32Ret != RK_SUCCESS)
    {
        printf("[VPSS] Create group failed: 0x%X\n", s32Ret);
        return -1;
    }

    // 2. 启动 VPSS GROUP
    s32Ret = RK_MPI_VPSS_StartGrp(VPSS_GRP_ID);
    if (s32Ret != RK_SUCCESS)
    {
        printf("[VPSS] Start group failed: 0x%X\n", s32Ret);
        return -1;
    }

    // 3. 配置 VPSS 通道属性 (输出格式为 BGR888)
    // 正确按顺序初始化的 VPSS_CHN_ATTR_S 结构体
    VPSS_CHN_ATTR_S chnAttr = {
        .enChnMode = VPSS_CHN_MODE_USER,
        .u32Width = MAX_WIDTH,
        .u32Height = MAX_HEIGHT,
        .enVideoFormat = VIDEO_FORMAT_LINEAR, // 线性视频格式
        .enPixelFormat = RK_FMT_RGB888,       // 输出格式为 BGR888
        .enDynamicRange = DYNAMIC_RANGE_SDR10, // SDR 10位动态范围
        .enCompressMode = COMPRESS_MODE_NONE, // 无压缩
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

    s32Ret = RK_MPI_VPSS_SetChnAttr(VPSS_GRP_ID, VPSS_CHN_ID, &chnAttr);
    if (s32Ret != RK_SUCCESS)
    {
        printf("[VPSS] Set channel attr failed: 0x%X\n", s32Ret);
        return -1;
    }

    // 4. 启用 VPSS 通道
    s32Ret = RK_MPI_VPSS_EnableChn(VPSS_GRP_ID, VPSS_CHN_ID);
    if (s32Ret != RK_SUCCESS)
    {
        printf("[VPSS] Enable channel failed: 0x%X\n", s32Ret);
        return -1;
    }

    // 5. 可选: 启用备份帧防止丢帧
    s32Ret = RK_MPI_VPSS_EnableBackupFrame(VPSS_GRP_ID);
    if (s32Ret != RK_SUCCESS)
    {
        printf("[VPSS] Enable backup frame failed: 0x%X\n", s32Ret);
    }

    printf("[VPSS] Initialized: YUV420SP → BGR888\n");
    return 0;
}

// 配套的资源释放函数（程序退出时调用）
void deinitVPSS()
{
    // 先禁用通道
    RK_MPI_VPSS_DisableChn(vpss_grp, vpss_chn);
    // 再销毁组
    RK_MPI_VPSS_DestroyGrp(vpss_grp);
    printf("VPSS资源已释放！grp=%d, chn=%d\n", vpss_grp, vpss_chn);
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

        if (::initVPSS() != 0)
        {
            printf("VPSS初始化失败，程序退出！\n");
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

    int MediaStreamProcessor::run()
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

        // printf("VI帧成功发送到VPSS（格式：%d，宽高：%dx%d）\n",
        //        vi_frame.stVFrame.enPixelFormat,
        //        vi_frame.stVFrame.u32Width,
        //        vi_frame.stVFrame.u32Height);
        // printf("VI帧格式值: %d, RK_FMT_YUV420SP定义值: %d\n",
        //        vi_frame.stVFrame.enPixelFormat,
        //        RK_FMT_YUV420SP);


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