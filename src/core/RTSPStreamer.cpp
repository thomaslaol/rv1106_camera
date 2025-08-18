#if 0

#include "core/RTSPStreamer.hpp"
#include <cstdio>

extern "C"
{
#include "infra/logging/logger.h"
#include "rk_mpi_mb.h"
}

namespace core
{

    // 构造函数：初始化RTSP配置参数
    RTSPStreamer::RTSPStreamer(int rtsp_port,
                               const char *session_path,
                               rtsp_codec_id video_codec)
        : rtsp_port_(rtsp_port),
          session_path_(session_path),
          video_codec_(video_codec),
          rtsp_handle_(nullptr),
          rtsp_session_(nullptr),
          is_inited_(false) {}

    RTSPStreamer::~RTSPStreamer()
    {
        // 1. 释放会话
        if (rtsp_session_ != nullptr)
        {
            rtsp_del_session(rtsp_session_);
            rtsp_session_ = nullptr;
        }
        // 2. 释放RTSP实例
        if (rtsp_handle_ != nullptr)
        {
            rtsp_del_demo(rtsp_handle_);
            rtsp_handle_ = nullptr;
        }
        is_inited_ = false;
    }

    // 初始化RTSP服务：创建实例、会话并配置视频参数
    int RTSPStreamer::init()
    {
        if (is_inited_)
        {
            LOGW("Already initialized!");
            return 0;
        }

        // 1. 创建RTSP实例
        rtsp_handle_ = create_rtsp_demo(rtsp_port_);
        if (rtsp_handle_ == nullptr)
        {
            LOGE("Failed to create RTSP demo (port=%d)", rtsp_port_);
            return -1;
        }

        // 2. 创建RTSP会话
        rtsp_session_ = rtsp_new_session(rtsp_handle_, session_path_);
        if (rtsp_session_ == nullptr)
        {
            LOGE("Failed to create RTSP session (path=%s)", session_path_);
            rtsp_del_demo(rtsp_handle_); // 失败时释放已创建的实例
            rtsp_handle_ = nullptr;
            return -1;
        }

        // 3. 设置视频编码类型
        // 注意：codec_data需传入SPS/PPS（H264）或VPS/SPS/PPS（H265），此处暂时传空（后续可从编码流中提取）
        int ret = rtsp_set_video(rtsp_session_, video_codec_, nullptr, 0);
        if (ret != 0)
        {
            LOGE("Failed to set video codec (codec=%d)\n", video_codec_);
            rtsp_del_session(rtsp_session_);
            rtsp_del_demo(rtsp_handle_);
            rtsp_session_ = nullptr;
            rtsp_handle_ = nullptr;
            return -1;
        }

        // 4. 同步视频时间戳（初始化时校准一次）
        rtsp_sync_video_ts(rtsp_session_, rtsp_get_reltime(), rtsp_get_ntptime());

        is_inited_ = true;
        LOGI("Init success! RTSP URL: rtsp://<ip>:%d%s", rtsp_port_, session_path_);
        return 0;
    }

    // 新增：查找H.265 NALU（VPS=32、SPS=33、PPS=34）
    int RTSPStreamer::findH265Nalu(uint8_t *data, int len, int nalu_type)
    {
        for (int i = 4; i < len; i++)
        {
            // H.265 NALU起始码：0x00000001
            if (data[i - 4] == 0 && data[i - 3] == 0 && data[i - 2] == 0 && data[i - 1] == 1)
            {
                // H.265 NALU类型计算：(data[i] & 0x7E) >> 1
                int type = (data[i] & 0x7E) >> 1;
                if (type == nalu_type)
                {
                    return i - 4; // 返回起始码位置（便于提取完整NALU）
                }
            }
        }
        return -1;
    }

    // 新增：计算H.265 NALU长度
    int RTSPStreamer::getH265NaluLen(uint8_t *nalu_start, int max_len)
    {
        for (int i = 4; i < max_len; i++)
        {
            if (nalu_start[i - 4] == 0 && nalu_start[i - 3] == 0 && nalu_start[i - 2] == 0 && nalu_start[i - 1] == 1)
            {
                return i; // 当前NALU长度（到下一个起始码）
            }
        }
        return max_len; // 到帧末尾
    }

    // 新增：提取VPS/SPS/PPS并设置到RTSP服务
    int RTSPStreamer::extractAndSetH265CodecData(uint8_t *frame_data, int frame_len)
    {
        LOGD("Trying to extract H.265 Codec Data (len=%d)", frame_len);
        // 查找VPS（32）和PPS（34）的位置
        int vps_pos = findH265Nalu(frame_data, frame_len, 32);
        int pps_pos = findH265Nalu(frame_data, frame_len, 34);

        if (vps_pos == -1 || pps_pos == -1)
        {
            LOGW("H.265 VPS/SPS/PPS not found (vps_pos=%d, pps_pos=%d)", vps_pos, pps_pos);
            return -1;
        }

        // 计算Codec Data长度：从VPS起始到PPS结束
        int pps_total_len = getH265NaluLen(frame_data + pps_pos, frame_len - pps_pos);
        int codec_data_len = pps_pos + pps_total_len - vps_pos;

        // 调用RTSP库设置Codec Data（VPS+SPS+PPS）
        int ret = rtsp_set_video(rtsp_session_, video_codec_, frame_data + vps_pos, codec_data_len);
        if (ret != 0)
        {
            LOGE("rtsp_set_video failed (ret=%d, codec_data_len=%d)", ret, codec_data_len);
            return -1;
        }

        // 同步时间戳
        rtsp_sync_video_ts(rtsp_session_, rtsp_get_reltime(), rtsp_get_ntptime());
        is_codec_data_sent_ = true;
        LOGI("H.265 Codec Data set success (len=%d)", codec_data_len);
        return 0;
    }

    // 修改sendVideoFrame：新增“第一次推流时提取Codec Data”的逻辑
    int RTSPStreamer::sendVideoFrame(const VENC_STREAM_S &encoded_frame)
    {
        if (!is_inited_)
        {
            LOGE("Not initialized! Call init() first");
            return -1;
        }

        // 1. 检查编码帧有效性
        if (encoded_frame.pstPack == nullptr || encoded_frame.pstPack->u32Len == 0)
        {
            LOGE("Invalid encoded frame (length=0)");
            return -1;
        }

        // 3. 硬件缓冲区转虚拟地址（仅一次转换，复用结果）
        uint8_t *frame_data = reinterpret_cast<uint8_t *>(
            RK_MPI_MB_Handle2VirAddr(encoded_frame.pstPack->pMbBlk));
        if (frame_data == nullptr)
        {
            LOGE("Failed to convert MB_BLK to virtual address");
            return -1;
        }
        int frame_len = encoded_frame.pstPack->u32Len;

        // 4. 关键：第一次推流时，自动提取并设置H.265 Codec Data
        if (!is_codec_data_sent_ && video_codec_ == RTSP_CODEC_ID_VIDEO_H265)
        {
            extractAndSetH265CodecData(frame_data, frame_len);
            // 即使提取失败，也继续尝试推流（后续帧可能包含Codec Data）
        }

        // 5. 发送视频帧（只有Codec Data设置成功后才推流，避免客户端解码失败）
        int ret = -1;
        if (is_codec_data_sent_)
        {
            rtsp_tx_video(
                rtsp_session_,
                frame_data,
                frame_len,
                encoded_frame.pstPack->u64PTS);

            // 2. 处理RTSP事件
            rtsp_do_event(rtsp_handle_);
        }
        else
        {
            LOGW("Skip send frame: H.265 Codec Data not set yet");
        }

        return ret;
    }

    // 发送编码后的视频帧到RTSP客户端
    // int RTSPStreamer::sendVideoFrame(const VENC_STREAM_S &encoded_frame)
    // {
    //     if (!is_inited_)
    //     {
    //         LOGE("Not initialized! Call init() first");
    //         return -1;
    //     }

    //     // 1. 检查编码帧有效性
    //     if (encoded_frame.pstPack == nullptr || encoded_frame.pstPack->u32Len == 0)
    //     {
    //         LOGE("Invalid encoded frame (length=0)");
    //         return -1;
    //     }

    //     // 2. 处理RTSP事件（必须定期调用，否则客户端无法连接/断开）
    //     rtsp_do_event(rtsp_handle_);

    //     // 3. 关键修正：将硬件缓冲区句柄（pMbBlk）转换为虚拟地址（uint8_t*）
    //     // RK_MPI_MB_Handle2VirAddr：瑞芯微MPI接口，专门用于MB_BLK到虚拟地址的转换
    //     uint8_t *frame_data = reinterpret_cast<uint8_t *>(
    //         RK_MPI_MB_Handle2VirAddr(encoded_frame.pstPack->pMbBlk));
    //     if (frame_data == nullptr)
    //     {
    //         LOGE("Failed to convert MB_BLK to virtual address\n");
    //         return -1;
    //     }

    //     // 4. 发送视频帧（此时传入的是转换后的真实数据地址，类型匹配）
    //     int ret = rtsp_tx_video(
    //         rtsp_session_,
    //         frame_data,                    // 转换后的const uint8_t*类型数据地址
    //         encoded_frame.pstPack->u32Len, // 数据长度（不变）
    //         encoded_frame.pstPack->u64PTS  // 时间戳（不变）
    //     );

    //     if (ret != 0)
    //     {
    //         LOGE("Failed to send video frame (ret=%d)\n", ret);
    //     }
    //     return ret;
    // }

    int RTSPStreamer::setH265CodecData(const uint8_t *codec_data, int data_len)
    {
        if (!is_inited_ || !rtsp_session_)
        {
            LOGE("RTSP not initialized!");
            return -1;
        }
        // 调用RTSP库接口，设置H.265编码及Codec Data
        int ret = rtsp_set_video(rtsp_session_, video_codec_, codec_data, data_len);
        if (ret != 0)
        {
            LOGE("rtsp_set_video failed (ret=%d)", ret);
            return -1;
        }
        LOGI("H.265 codec data set success (len=%d)", data_len);
        return 0;
    }

    bool RTSPStreamer::init()
    {
        m_rtspServer = create_rtsp_demo(m_port);
        if (!m_rtspServer)
        {
            LOG_ERROR("Failed to create RTSP server on port {}", m_port);
            return false;
        }
        m_rtspSession = rtsp_new_session(m_rtspServer, m_path.c_str());
        if (!m_rtspSession)
        {
            LOG_ERROR("Failed to create RTSP session on path {}", m_path);
            return false;
        }
        // 设置为H.265编码
        rtsp_set_video(m_rtspSession, RTSP_CODEC_ID_VIDEO_H265, nullptr, 0);
        rtsp_sync_video_ts(m_rtspSession, rtsp_get_reltime(), rtsp_get_ntptime());
        LOG_INFO("RTSP server initialized (rtsp://localhost:{}{})", m_port, m_path);
        return true;
    }

    bool RTSPStreamer::pushFrame(uint8_t *data, int len, RK_U64 pts)
    {
        if (!m_rtspSession)
        {
            LOG_ERROR("RTSP session not initialized");
            return false;
        }
        rtsp_tx_video(m_rtspSession, data, len, pts);
        return true;
    }

    void RTSPStreamer::handleEvents()
    {
        if (m_rtspServer)
        {
            rtsp_do_event(m_rtspServer);
        }
    }

} // namespace core

#endif

#include "core/RTSPStreamer.hpp"
#include <cstdio>

extern "C"
{
#include "infra/logging/logger.h"
#include "rk_mpi_mb.h"
}

namespace core
{

    RTSPStreamer::RTSPStreamer(int rtsp_port,
                     const char *session_path,
                     rtsp_codec_id video_codec)
        : rtsp_port_(rtsp_port), session_path_(session_path),video_codec_(video_codec),
          m_rtspServer(nullptr), m_rtspSession(nullptr) {}

    RTSPStreamer::~RTSPStreamer()
    {
        if (m_rtspSession)
            rtsp_del_session(m_rtspSession);
        if (m_rtspServer)
            rtsp_del_demo(m_rtspServer);
        LOGI("RTSPStreamer destroyed (port {})", rtsp_port_);
    }

    bool RTSPStreamer::init()
    {
        m_rtspServer = create_rtsp_demo(rtsp_port_);
        if (!m_rtspServer)
        {
            LOGE("Failed to create RTSP server on port {}", rtsp_port_);
            return false;
        }
        m_rtspSession = rtsp_new_session(m_rtspServer, session_path_);
        if (!m_rtspSession)
        {
            LOGE("Failed to create RTSP session on path {}", session_path_);
            return false;
        }
        // 设置为H.265编码
        rtsp_set_video(m_rtspSession, video_codec_, nullptr, 0);
        rtsp_sync_video_ts(m_rtspSession, rtsp_get_reltime(), rtsp_get_ntptime());
        LOGI("RTSP server initialized (rtsp://localhost:%d%s)", rtsp_port_, session_path_);
        is_inited_ = true;
        return is_inited_;
    }

    bool RTSPStreamer::pushFrame(uint8_t *data, int len, RK_U64 pts)
    {
        if (!m_rtspSession)
        {
            LOGE("RTSP session not initialized");
            return false;
        }
        rtsp_tx_video(m_rtspSession, data, len, pts);
        return true;
    }

    void RTSPStreamer::handleEvents()
    {
        if (m_rtspServer)
        {
            rtsp_do_event(m_rtspServer);
        }
    }

} // namespace core
