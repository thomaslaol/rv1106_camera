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
        {
            rtsp_del_session(m_rtspSession);
            m_rtspSession = nullptr;
        }  

        if (m_rtspServer)
        {
            rtsp_del_demo(m_rtspServer);
            m_rtspServer = nullptr;
        }
            
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
