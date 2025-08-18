#pragma once

#include "sample_comm.h"

namespace driver
{
    class ISPDriver
    {
    public:
        ISPDriver() = default;
        ~ISPDriver() = default;

        // 初始化ISP
        int init();

        // 停止ISP
        int stop()
        {
            // 调用底层ISP停止接口（如SAMPLE_COMM_ISP_Stop(0)）
            return 0;
        }

        // 提供参数配置接口（允许外部修改配置）
        void setConfig(RK_BOOL multi_sensor, const char *iq_dir, rk_aiq_working_mode_t hdr_mode)
        {
            multi_sensor_ = multi_sensor;
            iq_dir_ = iq_dir;
            hdr_mode_ = hdr_mode;
        }

    private:
        RK_S32 cam_id_ = 0;
        RK_BOOL multi_sensor_ = RK_FALSE;
        const char *iq_dir_ = "/etc/iqfiles";
        rk_aiq_working_mode_t hdr_mode_ = RK_AIQ_WORKING_MODE_NORMAL;
    };
} // namespace driver
