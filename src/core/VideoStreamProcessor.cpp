#include "core/VideoStreamProcessor.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono> // 用于FPS统计

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "core/RTSPStreamer.hpp"
#include "core/VPSSManager.hpp"

#include "core/RTSPEngine.hpp"
#include "infra/time/TimeUtils.h"

extern "C"
{
#include "infra/logging/logger.h"
#include "rk_mpi_mb.h"
#include "rk_comm_vpss.h"
#include "rk_mpi_vpss.h"
}

namespace core
{
    VideoStreamProcessor::VideoStreamProcessor(driver::VideoInputDriver *vi_driver,
                                               driver::VideoEncoderDriver *venc_driver,
                                               core::VPSSManager *vpss_manager)
        : vi_driver_(vi_driver), venc_driver_(venc_driver), vpss_manager_(vpss_manager)
    {
        is_inited_ = false;

        // 初始化编码流缓冲区
        memset(&venc_stream_, 0, sizeof(VENC_STREAM_S));

        // 初始化BGR帧缓冲
        m_bgrFrame = cv::Mat(cv::Size(width, height), CV_8UC3);
        LOGI("VideoStreamProcessor initialized (%dx%d)", width, height);

        // 初始化视频帧信息
        memset(&vi_frame, 0, sizeof(VIDEO_FRAME_INFO_S));

        rtsps_engine_ = new RTSPEngine();
    }

    // 析构函数：释放资源+停止循环
    VideoStreamProcessor::~VideoStreamProcessor()
    {
        releaseStreamBuffer(); // 释放malloc的VENC_PACK_S

        releasePool(); // 释放YUV内存池

        queue_cv_.notify_all();
        // std::lock_guard<std::mutex> lock(queue_mutex_);
        // while (!packet_queue_.empty())
        // {
        //     av_packet_free(&packet_queue_.front());
        //     packet_queue_.pop();
        // }
    }

    // 初始化编码流缓冲区
    int VideoStreamProcessor::initStreamBuffer()
    {
        venc_stream_.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
        if (venc_stream_.pstPack == nullptr)
        {
            printf("VideoStreamProcessor::initStreamBuffer - malloc failed!\n");
            return -1;
        }
        memset(venc_stream_.pstPack, 0, sizeof(VENC_PACK_S));
        return 0;
    }

    // 释放编码流缓冲区
    void VideoStreamProcessor::releaseStreamBuffer()
    {
        if (venc_stream_.pstPack != nullptr)
        {
            free(venc_stream_.pstPack);
            venc_stream_.pstPack = nullptr;
        }
    }

    // 初始化YUV内存池（大小为width*height*3，保留5个缓冲块）
    int VideoStreamProcessor::initPool()
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

    // 释放内存池缓冲区
    void VideoStreamProcessor::releasePool()
    {
        if (m_mb_pool != MB_INVALID_POOLID)
        {
            RK_MPI_MB_DestroyPool(m_mb_pool);
            m_mb_pool = MB_INVALID_POOLID;
        }
    }

    // 启动业务循环
    int VideoStreamProcessor::init()
    {
        if (is_inited_)
        {
            LOGE("VideoStreamProcessor::startProcess - already running!");
            return 0;
        }

        // 初始化编码流缓冲区
        if (initStreamBuffer() != 0)
        {
            LOGE("VideoStreamProcessor::initStreamBuffer - failed!");
            return -1;
        }

        // 初始化YUV内存池
        if (initPool() != 0)
        {
            LOGE("VideoStreamProcessor::initPool - failed!");
            return -1;
        }

        core::RTSPConfig rtsp_config;
        // 推流url
        {
            rtsp_config.output_url = "rtsp://192.168.251.165/live/camera";
        }
        // 视频流参数 (硬编码)
        {
            rtsp_config.video_width = 1920;
            rtsp_config.video_height = 1080;
            rtsp_config.video_bitrate = 10 * 1024 * 1000; // 10 Mbps
            rtsp_config.video_framerate = 30;
            rtsp_config.video_codec_id = AV_CODEC_ID_H265; // 与 RK_VIDEO_ID_HEVC 对应}
        }
        // 音频流参数 (硬编码)
        {
            rtsp_config.audio_sample_rate = 48000;
            rtsp_config.audio_channels = 1;
            rtsp_config.audio_bitrate = 64 * 1024; // 64 kbps
            rtsp_config.audio_codec_id = AV_CODEC_ID_AAC;
        }
        // 网络参数
        {
            rtsp_config.rw_timeout = 3000000; // 网络超时时间 (微秒)
            rtsp_config.max_delay = 500000;   // 最大延迟 (微秒)
            rtsp_config.enable_tcp = true;    // 是否强制使用TCP传输
        }
        int ret = rtsps_engine_->init(rtsp_config);
        CHECK_RET(ret, "rtsps_engine_->init");

        // 初始化FPS统计时间
        start_time_ = 0;

        is_inited_ = true;
        return 0;
    }

    int VideoStreamProcessor::start()
    {
        if (!is_inited_)
        {
            LOGE("VideoStreamProcessor::run - not initialized!");
            return -1;
        }

        int ret = vi_driver_->start();
        ret |= venc_driver_->start();

        // 绑定VI和VPSS
        // ret |= vpss_manager_->bindViToVpss();
        // CHECK_RET(ret, "vpss_manager_->bindViToVpss()");

        is_running_ = true;

        return ret;
    }

    void VideoStreamProcessor::stop()
    {
        if (!is_inited_)
        {
            LOGE("VideoStreamProcessor::stopProcess - not initialized!");
            return;
        }

        is_inited_ = false;
        vi_driver_->stop();
        venc_driver_->stop();
    }

    int VideoStreamProcessor::loopProcess()
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
        static uint64_t start_time_pts = 0;
        if (start_time_pts == 0)
        {
            start_time_pts = now;
        }

        VIDEO_FRAME_INFO_S encode_frame = bgr_frame;         // 拷贝帧信息（浅拷贝，共享内存块）
        encode_frame.stVFrame.enPixelFormat = RK_FMT_RGB888; // 匹配编码器格式（需与VPSS输出兼容）
        encode_frame.stVFrame.u64PTS = now - start_time_pts; // 更新时间戳

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
            // printf("获取视频数据且编码成功 当前帧数据长度==%d 当前帧时间戳==%lld\r\n",
            //        venc_stream_.pstPack->u32Len,
            //        venc_stream_.pstPack->u64PTS);

            AVPacket *pkt = av_packet_alloc();

            pkt->data = (uint8_t *)streamData;
            pkt->size = venc_stream_.pstPack->u32Len;
            pkt->pts = venc_stream_.pstPack->u64PTS;
            pkt->dts = pkt->pts; // 无B帧时DTS=PTS

            pkt->duration = 33333;
            printf("当前帧PTS：%lld（目标时间基），持续时间：%lld\n", pkt->pts, pkt->duration);



            // 设置关键帧标志
            H265E_NALU_TYPE_E nalu_type = venc_stream_.pstPack->DataType.enH265EType;
            const char *nalu_type_desc = "";
            bool is_key_frame = false;

            // 根据官方枚举判断帧类型
            switch (nalu_type)
            {
            case H265E_NALU_BSLICE:
                nalu_type_desc = "B 帧（非关键帧）";
                is_key_frame = false;
                break;
            case H265E_NALU_PSLICE:
                nalu_type_desc = "P 帧（非关键帧）";
                is_key_frame = false;
                break;
            case H265E_NALU_ISLICE:
                nalu_type_desc = "普通 I 帧（非关键帧，无 SPS/PPS）";
                is_key_frame = false;
                break;
            case H265E_NALU_IDRSLICE: // 关键帧！
                nalu_type_desc = "IDR 帧（关键帧，含 SPS/PPS）";
                is_key_frame = true;
                break;
            case H265E_NALU_VPS:
                nalu_type_desc = "VPS（视频参数集，初始化必需）";
                is_key_frame = false;
                break;
            case H265E_NALU_SPS:
                nalu_type_desc = "SPS（序列参数集，初始化必需）";
                is_key_frame = false;
                break;
            case H265E_NALU_PPS:
                nalu_type_desc = "PPS（图像参数集，初始化必需）";
                is_key_frame = false;
                break;
            case H265E_NALU_SEI:
                nalu_type_desc = "SEI（补充增强信息，可选）";
                is_key_frame = false;
                break;
            default:
                nalu_type_desc = "未知类型";
                is_key_frame = false;
                break;
            }

            if (nalu_type == H265E_NALU_IDRSLICE ||
                nalu_type == H265E_NALU_VPS ||
                nalu_type == H265E_NALU_SPS ||
                nalu_type == H265E_NALU_PPS)
            {
                pkt->flags |= AV_PKT_FLAG_KEY; // 标记为关键帧
                printf("关键帧或参数集：类型=%d（%s），长度=%d\n",
                       nalu_type, nalu_type_desc, venc_stream_.pstPack->u32Len);
            }

            // 退流
            rtsps_engine_->pushVideoFrame(pkt);
            av_packet_unref(pkt);

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

#if 0
    int VideoStreamProcessor::loopProcess()
    {
        printf("VideoStreamProcessor::loopProcess\n");

        int ret = 0;
        VPSS_GRP vpss_grp = 0;       // 确保已初始化的VPSS组号
        VPSS_GRP_PIPE vpss_pipe = 0; // 固定管道号0

        // 1. 从VI获取原始帧
        VIDEO_FRAME_INFO_S vi_frame;
        ret = vi_driver_->getFrame(vi_frame, 1000); // 阻塞等待获取VI帧
        if (ret != RK_SUCCESS)
        {
            printf("VPSS发送帧失败！ret = %d\n", ret);
            vi_driver_->releaseFrame(vi_frame);
            return -1;
        }

        // 2. 发送VI帧到VPSS进行硬件格式转换
        ret = RK_MPI_VPSS_SendFrame(vpss_grp, vpss_pipe, &vi_frame, 1000);
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
            start_time_ = now;
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

        // 5. 绘制FPS文本
        cv::Mat bgr_mat(
            bgr_frame.stVFrame.u32Height,
            bgr_frame.stVFrame.u32Width,
            CV_8UC3,
            RK_MPI_MB_Handle2VirAddr(bgr_frame.stVFrame.pMbBlk));
        cv::putText(bgr_mat, m_fpsText, cv::Point(40, 40),
                    cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

        // 6. 准备编码帧（直接复用VPSS输出的帧数据，零拷贝）
        static uint64_t encode_frame_time_ref = 0;
        VIDEO_FRAME_INFO_S encode_frame = bgr_frame;                // 拷贝帧信息
        encode_frame.stVFrame.enPixelFormat = RK_FMT_RGB888;        // 匹配编码器格式
        encode_frame.stVFrame.u64PTS = infra::TEST_COMM_GetNowUs(); // 更新时间戳
        encode_frame.stVFrame.u32TimeRef = encode_frame_time_ref++;

        // printf("编码后的帧时间戳：%lld\n", encode_frame.stVFrame.u64PTS);

        // 7. 发送到编码器
        if (venc_driver_->sendFrame(encode_frame) != 0)
        {
            printf("Failed to send frame to encoder\n");
            RK_MPI_VPSS_ReleaseGrpFrame(vpss_grp, vpss_pipe, &bgr_frame); // 释放VPSS帧
            return -1;
        }

        // 8. 从VENC获取编码流
        ret = venc_driver_->getStream(venc_stream_, 1000);
        if (ret != RK_SUCCESS)
        {
            printf("Failed to get stream from encoder\n");
            return -1;
        }

        // 7. 发送到编码器
        if (venc_driver_->sendFrame(encode_frame) != 0)
        {
            printf("Failed to send frame to encoder\n");
            RK_MPI_VPSS_ReleaseGrpFrame(vpss_grp, vpss_pipe, &bgr_frame); // 释放VPSS帧
            return -1;
        }

        // 8. 从VENC获取编码流
        ret = venc_driver_->getStream(venc_stream_, 1000);
        if (ret == RK_SUCCESS)
        {
            void *streamData = RK_MPI_MB_Handle2VirAddr(venc_stream_.pstPack->pMbBlk);

            // 打印帧信息
            printf("获取视频数据且编码成功 当前帧数据长度==%d 当前帧时间戳==%lld\r\n",
                   venc_stream_.pstPack->u32Len,
                   venc_stream_.pstPack->u64PTS);

            //=========================================================
#if 0
            // AVPacket *pkt = av_packet_alloc();

            // if (pkt->pts != AV_NOPTS_VALUE)
            // {
            //     // 使用帧持续时间而不是固定增量
            //     int64_t increment = pkt->duration;
            //     if (increment <= 0)
            //     {
            //         // 默认
            //         increment = (1000000LL * 1) / 30;
            //     }

            //     if (pkt->pts < last_pts_)
            //     {
            //         printf("Adjusting non-monotonic PTS: %lld -> %lld", pkt->pts, last_pts_);
            //         pkt->pts = last_pts_ + increment;
            //     }
            //     last_pts_ = pkt->pts;
            // }
            // else
            // {
            //     int64_t increment = (1000000LL * 1) / 30;
            //     pkt->pts = last_pts_ + increment;
            //     last_pts_ = pkt->pts;
            // }

            // pkt->data = (uint8_t *)streamData;
            // pkt->size = venc_stream_.pstPack->u32Len;
            // pkt->pts = venc_stream_.pstPack->u64PTS;
            // pkt->dts = pkt->pts; // 无B帧时DTS=PTS

            // pkt->duration = 33333;
            // printf("当前帧PTS：%lld（目标时间基），持续时间：%lld\n", pkt->pts, pkt->duration);

            // static uint64_t test_long_time = 0;
            // if (test_long_time == 0)
            // {
            //     test_long_time = pkt->pts;
            // }
            // else
            // {
            //     if (test_long_time + 666666 < pkt->pts)
            //     {
            //         printf("丢帧了！\n");
            //     }
            //     test_long_time = pkt->pts;
            // }

            // printf("当前帧PTS：%lld（目标时间基），持续时间：%lld\n", pkt->pts, pkt->duration);

            // if ( pkt->pts )
            // {
            //     /* code */
            // }

            // 设置关键帧标志
            // H265E_NALU_TYPE_E nalu_type = venc_stream_.pstPack->DataType.enH265EType;
            // const char *nalu_type_desc = "";
            // bool is_key_frame = false;

            // 3. 根据官方枚举判断帧类型
            // switch (nalu_type)
            // {
            // case H265E_NALU_BSLICE:
            //     nalu_type_desc = "B 帧（非关键帧）";
            //     is_key_frame = false;
            //     break;
            // case H265E_NALU_PSLICE:
            //     nalu_type_desc = "P 帧（非关键帧）";
            //     is_key_frame = false;
            //     break;
            // case H265E_NALU_ISLICE:
            //     nalu_type_desc = "普通 I 帧（非关键帧，无 SPS/PPS）";
            //     is_key_frame = false;
            //     break;
            // case H265E_NALU_IDRSLICE: // 关键帧！
            //     nalu_type_desc = "IDR 帧（关键帧，含 SPS/PPS）";
            //     is_key_frame = true;
            //     break;
            // case H265E_NALU_VPS:
            //     nalu_type_desc = "VPS（视频参数集，初始化必需）";
            //     is_key_frame = false;
            //     break;
            // case H265E_NALU_SPS:
            //     nalu_type_desc = "SPS（序列参数集，初始化必需）";
            //     is_key_frame = false;
            //     break;
            // case H265E_NALU_PPS:
            //     nalu_type_desc = "PPS（图像参数集，初始化必需）";
            //     is_key_frame = false;
            //     break;
            // case H265E_NALU_SEI:
            //     nalu_type_desc = "SEI（补充增强信息，可选）";
            //     is_key_frame = false;
            //     break;
            // default:
            //     nalu_type_desc = "未知类型";
            //     is_key_frame = false;
            //     break;
            // }

            // 4. 打印官方标记的帧类型（验证是否正确）
            // if (H265E_NALU_IDRSLICE == nalu_type)
            // {
            //     pkt->flags |= AV_PKT_FLAG_KEY;
            //     printf("瑞芯微官方标记：NALU 类型=%d（%s），长度=%d，时间戳=%lld\n",
            //            nalu_type, nalu_type_desc, venc_stream_.pstPack->u32Len, venc_stream_.pstPack->u64PTS);
            // }

            // if (nalu_type == H265E_NALU_IDRSLICE ||
            //     nalu_type == H265E_NALU_VPS ||
            //     nalu_type == H265E_NALU_SPS ||
            //     nalu_type == H265E_NALU_PPS)
            // {
            //     pkt->flags |= AV_PKT_FLAG_KEY; // 标记为关键帧
            //     printf("关键帧或参数集：类型=%d（%s），长度=%d\n",
            //            nalu_type, nalu_type_desc, venc_stream_.pstPack->u32Len);
            // }

            // static FILE *h265_file = fopen("raw_stream.h265", "wb");
            // if (h265_file)
            // {
            //     fwrite(streamData, 1, venc_stream_.pstPack->u32Len, h265_file);
            //     fflush(h265_file);
            // }

            // 退流
            // rtsps_engine_->pushVideoFrame(pkt);
            // av_packet_unref(pkt);

            // 将 AVPacket 添加到队列
            // 入队时加锁
            // {
            //     std::lock_guard<std::mutex> lock(queue_mutex_); // 自动加锁/解锁

            //     // 队列满时丢弃旧帧（同样在锁保护下）
            //     if (packet_queue_.size() >= 10)
            //     {
            //         AVPacket *old_pkt = packet_queue_.front();
            //         av_free(old_pkt->data); // 释放拷贝的内存
            //         av_packet_free(&old_pkt);
            //         packet_queue_.pop();
            //         // printf("队列满，丢弃旧帧，当前大小：%d\n", packet_queue_.size());
            //     }

            //     // 加入新帧
            //     packet_queue_.push(pkt);
            //     queue_cv_.notify_one(); // 通知等待的出队线程
            //     // printf("入队后，队列大小：%d\n", packet_queue_.size());
            // }

            // printf("保存时：队列的个数是%d\n", packet_queue_.size());

            // if (ret != 0)
            // {
            //     printf("保存帧数据到队列失败！ret=%d\n", ret);
            // }

            // 推流（若已初始化）
            // if (rtsp_streamer_->isInited())
            // {
            //     rtsp_streamer_->pushFrame(
            //         (uint8_t *)streamData,
            //         venc_stream_.pstPack->u32Len,
            //         now);
            //     rtsp_streamer_->handleEvents();
            // }

#endif

            // 释放编码器流资源
            venc_driver_->releaseStream(venc_stream_);
        }
        else
        {
            printf("VideoStreamProcessor::run - get VENC stream failed! ret=%d\n", ret);
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
#endif

    // RK码流转换为AVPacket（核心函数）
    AVPacket *VideoStreamProcessor::rk_stream_to_avpacket(void *stream_data, int stream_size, int64_t pts)
    {
        if (!stream_data || stream_size <= 0)
        {
            LOGE("Invalid RK stream data");
            return nullptr;
        }

        // 1. 分配AVPacket
        AVPacket *pkt = av_packet_alloc();
        if (!pkt)
        {
            LOGE("Failed to alloc AVPacket");
            return nullptr;
        }

        // 2. 复制RK码流数据（关键：避免原始数据被RK释放后失效）
        uint8_t *data = (uint8_t *)av_malloc(stream_size);
        if (!data)
        {
            LOGE("Failed to alloc data buffer");
            av_packet_free(&pkt);
            return nullptr;
        }
        memcpy(data, stream_data, stream_size);

        pkt->data = data;
        pkt->size = stream_size;
        pkt->pts = pts;
        pkt->dts = pts;                                                                       // H.265通常pts=dts
        pkt->duration = av_rescale_q(1, (AVRational){1, 30}, av_make_q(1, 10 * 1024 * 1000)); // 假设30fps
        pkt->pos = -1;

        return pkt;
    }

    int VideoStreamProcessor::getFromVIAndsendToVPSS()
    {
        VIDEO_FRAME_INFO_S vi_frame;
        int ret = vi_driver_->getFrame(vi_frame, -1);
        if (ret != RK_SUCCESS)
        {
            printf("VI获取帧失败！ret=%d\n", ret);
            return -1;
        }
        // 2. 发送VI帧到VPSS进行硬件格式转换
        ret = RK_MPI_VPSS_SendFrame(0, 0, &vi_frame, -1);
        if (ret != RK_SUCCESS)
        {
            printf("VPSS发送帧失败！ret=%d\n", ret);
            vi_driver_->releaseFrame(vi_frame); // 释放VI帧
            return -1;
        }
        return 0;
    }

    int VideoStreamProcessor::getFromVPSSAndProcessWithOpenCV(VIDEO_FRAME_INFO_S &process_frame)
    {
        // 从VPSS获取转换后的BGR帧
        VIDEO_FRAME_INFO_S bgr_frame;
        int ret = RK_MPI_VPSS_GetChnFrame(0, 0, &bgr_frame, 1000);
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
        process_frame = bgr_frame;                            // 拷贝帧信息（浅拷贝，共享内存块）
        process_frame.stVFrame.enPixelFormat = RK_FMT_RGB888; // 匹配编码器格式（需与VPSS输出兼容）
        process_frame.stVFrame.u64PTS = now;                  // 更新时间戳

        RK_MPI_VPSS_ReleaseGrpFrame(0, 0, &bgr_frame); // 释放VPSS帧

        return 0;
    }

    int VideoStreamProcessor::sendToVENCAndGetEncodedPacket(VIDEO_FRAME_INFO_S &process_frame, VENC_STREAM_S &encode_frame)
    {
        int ret = 0;
        // 发送到编码器
        if (venc_driver_->sendFrame(process_frame) != 0)
        {
            printf("Failed to send frame to encoder\n");
            return -1;
        }

        // 从编码器获取编码流
        ret = venc_driver_->getStream(encode_frame, -1);
        if (ret != RK_SUCCESS)
        {
            printf("VideoStreamProcessor::run - get VENC stream failed! ret=%d\n", ret);
            return -1;
        }

        void *streamData = RK_MPI_MB_Handle2VirAddr(encode_frame.pstPack->pMbBlk);

        // 打印帧信息
        printf("获取视频数据且编码成功 当前帧数据长度==%d 当前帧时间戳==%lld\r\n",
               venc_stream_.pstPack->u32Len,
               venc_stream_.pstPack->u64PTS);

        // 释放编码器流资源
        // venc_driver_->releaseStream(venc_stream_);

        return 0;
    }

    /**
     * H.265专用：将VENC_STREAM_S转换为AVPacket并放入队列
     * @param encode_frame 编码器输出的H.265流
     * @return 0成功，非0失败
     */
    int VideoStreamProcessor::pushEncodedPacketToQueue(uint8_t *data, int data_size, int64_t pts)
    {
        if (!is_running_)
        {
            printf("ERROR: VideoStreamProcessor is not running\n");
            return -1;
        }

        // 1. 分配并初始化AVPacket
        AVPacket *pkt = av_packet_alloc();
        if (!pkt)
        {
            LOGE("Failed to alloc AVPacket");
            return -1;
        }

        // 2. 填充原始数据（注意：AVPacket的数据需要用av_packet_from_data管理，避免内存泄漏）
        int ret = av_packet_from_data(pkt, data, data_size);
        if (ret < 0)
        {
            LOGE("Failed to init AVPacket from data: %d", ret);
            av_packet_free(&pkt);
            return -1;
        }

        // 3. 填充元信息（关键步骤）
        pkt->pts = pts; // 显示时间戳（需按视频流的time_base转换）
        pkt->dts = pts; // 解码时间戳

        // 将帧数据放入队列
        {
            // std::unique_lock<std::mutex> lock(queue_mutex_);
            packet_queue_.push(pkt);
        }

        printf("DEBUG: Frame enqueued - PTS: %ld, Size: %d, Keyframe: %s\n",
               pkt->pts, pkt->size);

        return 0;
    }

    /**
     * 取出H.265的AVPacket（供推流线程）
     * @param out_pkt 输出的AVPacket（需用av_packet_free释放）
     * @param timeout_ms 超时时间
     * @return 0成功，-1超时，-2停止
     */
    int VideoStreamProcessor::popEncodedPacket(AVPacket **out_pkt, int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(queue_mutex_); // 加锁保护队列操作

        // 等待队列非空或停止运行，超时返回
        bool has_data = queue_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                           [this]()
                                           { return !is_running_ || !packet_queue_.empty(); });

        if (!has_data)
        {
            *out_pkt = nullptr;
            return -1; // 超时
        }
        if (!is_running_)
        {
            *out_pkt = nullptr;
            return -2; // 停止运行
        }

        // 取出队首包（此时队列非空，且被锁保护，安全访问）
        *out_pkt = packet_queue_.front();
        packet_queue_.pop();

        printf("出队后，队列大小：%d\n", packet_queue_.size());
        return 0;
    }

} // namespace core